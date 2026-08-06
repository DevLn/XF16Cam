#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "audio/manager/audio_manager.h"
#include "audio/pcm/audio_pcm.h"
#include "kernel/os/os.h"
#include "lwip/sockets.h"

#include "xf16cam_audio.h"

#define XF16CAM_AUDIO_RATE          (8000)
#define XF16CAM_AUDIO_RING_PACKETS  (8)
#define XF16CAM_AUDIO_STACK_SIZE    (1536)
#define XF16CAM_AUDIO_HTTP_STACK    (1536)
#define XF16CAM_AUDIO_MIC_LEVEL     (VOLUME_LEVEL3)

typedef struct {
	uint8_t pcmu[XF16CAM_AUDIO_SAMPLES_PER_PACKET];
	uint32_t timestamp;
	volatile uint32_t generation;
} XF16CamAudioPacket;

static OS_Thread_t g_audio_thread;
static OS_Thread_t g_audio_http_thread;
static XF16CamAudioPacket g_audio_ring[XF16CAM_AUDIO_RING_PACKETS];
static volatile uint32_t g_audio_packets;
static XF16CamAudioInfo g_audio_info;
static volatile int g_audio_http_active;

static int xf16cam_audio_send_all(int fd, const void *data, uint32_t length)
{
	const uint8_t *cursor = data;

	while (length > 0) {
		int sent = send(fd, cursor, length, 0);
		if (sent <= 0)
			return -1;
		cursor += sent;
		length -= (uint32_t)sent;
	}
	return 0;
}

/* ITU-T G.711 mu-law encoder. */
static uint8_t xf16cam_mulaw(int16_t sample)
{
	static const int16_t ends[8] = { 0xff, 0x1ff, 0x3ff, 0x7ff, 0xfff, 0x1fff, 0x3fff, 0x7fff };
	int value = sample;
	int mask;
	int segment;
	uint8_t encoded;

	if (value < 0) {
		value = -value;
		mask = 0x7f;
	} else {
		mask = 0xff;
	}
	if (value > 32635)
		value = 32635;
	value += 132;
	for (segment = 0; segment < 8 && value > ends[segment]; ++segment)
		;
	encoded = (uint8_t)((segment << 4) | ((value >> (segment + 3)) & 0x0f));
	return encoded ^ mask;
}

static void xf16cam_audio_task(void *arg)
{
	struct pcm_config config;
	int16_t pcm[XF16CAM_AUDIO_SAMPLES_PER_PACKET];
	(void)arg;

	memset(&config, 0, sizeof(config));
	config.channels = 1;
	config.format = PCM_FORMAT_S16_LE;
	config.period_count = 2;
	config.period_size = XF16CAM_AUDIO_SAMPLES_PER_PACKET;
	config.rate = XF16CAM_AUDIO_RATE;
	audio_manager_handler(AUDIO_SND_CARD_DEFAULT, AUDIO_MANAGER_SET_VOLUME_LEVEL,
	                      AUDIO_IN_DEV_AMIC, XF16CAM_AUDIO_MIC_LEVEL);
	if (snd_pcm_open(AUDIO_SND_CARD_DEFAULT, PCM_IN, &config) != 0) {
		printf("xf16cam audio: AMIC open failed\n");
		OS_ThreadDelete(&g_audio_thread);
		return;
	}
	g_audio_info.active = 1;
	printf("xf16cam audio: AMIC 8000 Hz mono S16, PCMU packets=20 ms gain_level=%u\n",
	       (unsigned int)XF16CAM_AUDIO_MIC_LEVEL);
	while (1) {
		uint32_t packet = g_audio_packets;
		XF16CamAudioPacket *slot = &g_audio_ring[packet % XF16CAM_AUDIO_RING_PACKETS];
		uint32_t total = 0;
		uint16_t peak = 0;
		int i;

		if (snd_pcm_read(AUDIO_SND_CARD_DEFAULT, pcm, sizeof(pcm)) != sizeof(pcm)) {
			g_audio_info.read_errors++;
			continue;
		}
		for (i = 0; i < XF16CAM_AUDIO_SAMPLES_PER_PACKET; ++i) {
			uint16_t magnitude = pcm[i] == INT16_MIN ? 32768U :
			                     (uint16_t)(pcm[i] < 0 ? -pcm[i] : pcm[i]);
			if (magnitude > peak)
				peak = magnitude;
			total += magnitude;
			slot->pcmu[i] = xf16cam_mulaw(pcm[i]);
		}
		slot->timestamp = packet * XF16CAM_AUDIO_SAMPLES_PER_PACKET;
		slot->generation = packet + 1;
		g_audio_info.peak = peak;
		g_audio_info.mean = (uint16_t)(total / XF16CAM_AUDIO_SAMPLES_PER_PACKET);
		g_audio_info.packets = packet + 1;
		g_audio_packets = packet + 1;
	}
}

int xf16cam_audio_start(void)
{
	memset(&g_audio_info, 0, sizeof(g_audio_info));
	memset(g_audio_ring, 0, sizeof(g_audio_ring));
	g_audio_packets = 0;
	return OS_ThreadCreate(&g_audio_thread, "xf16cam-audio", xf16cam_audio_task, NULL,
	                       OS_THREAD_PRIO_APP, XF16CAM_AUDIO_STACK_SIZE) == OS_OK ? 0 : -1;
}

static void xf16cam_audio_http_task(void *arg)
{
	static const char header[] =
		"HTTP/1.1 200 OK\r\nContent-Type: audio/basic\r\n"
		"Cache-Control: no-store\r\nConnection: close\r\n\r\n";
	uint8_t pcmu[XF16CAM_AUDIO_SAMPLES_PER_PACKET];
	uint32_t cursor = xf16cam_audio_cursor();
	uint32_t timestamp;
	int fd = (int)(intptr_t)arg;

	if (xf16cam_audio_send_all(fd, header, sizeof(header) - 1) == 0) {
		printf("xf16cam WEB audio: PCMU/8000 client connected\n");
		while (1) {
			int ready = xf16cam_audio_read(&cursor, pcmu, &timestamp);
			(void)timestamp;
			if (ready > 0) {
				if (xf16cam_audio_send_all(fd, pcmu, sizeof(pcmu)) != 0)
					break;
			} else {
				OS_MSleep(5);
			}
		}
	}
	closesocket(fd);
	g_audio_http_active = 0;
	printf("xf16cam WEB audio client stopped\n");
	OS_ThreadDelete(&g_audio_http_thread);
}

int xf16cam_audio_http_start(int fd)
{
	if (!g_audio_info.active || g_audio_http_active)
		return -1;
	g_audio_http_active = 1;
	if (OS_ThreadCreate(&g_audio_http_thread, "xf16cam-web-audio", xf16cam_audio_http_task,
	                    (void *)(intptr_t)fd, OS_THREAD_PRIO_APP,
	                    XF16CAM_AUDIO_HTTP_STACK) != OS_OK) {
		g_audio_http_active = 0;
		return -1;
	}
	return 0;
}

const XF16CamAudioInfo *xf16cam_audio_info(void)
{
	return &g_audio_info;
}

uint32_t xf16cam_audio_cursor(void)
{
	return g_audio_packets;
}

int xf16cam_audio_read(uint32_t *cursor, uint8_t *pcmu, uint32_t *timestamp)
{
	uint32_t available = g_audio_packets;
	XF16CamAudioPacket *slot;
	uint32_t generation;

	if (*cursor == available)
		return 0;
	if (available - *cursor > XF16CAM_AUDIO_RING_PACKETS)
		*cursor = available - XF16CAM_AUDIO_RING_PACKETS;
	slot = &g_audio_ring[*cursor % XF16CAM_AUDIO_RING_PACKETS];
	generation = *cursor + 1;
	if (slot->generation != generation)
		return 0;
	memcpy(pcmu, slot->pcmu, XF16CAM_AUDIO_SAMPLES_PER_PACKET);
	*timestamp = slot->timestamp;
	if (slot->generation != generation)
		return 0;
	(*cursor)++;
	return 1;
}
