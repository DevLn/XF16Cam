#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "compiler.h"
#include "board_config.h"
#include "common/board/board.h"
#include "kernel/os/os.h"
#include "lwip/sockets.h"

#include "xf16cam_log.h"
#include "xf16cam_net.h"

#ifdef XF16CAM_NETLOG

/* Ring size must be a power of two. 2 KiB holds roughly the last 30 lines,
 * which covers a boot banner until Wi-Fi is up and the broadcast starts. */
#define XF16CAM_LOG_RING     (2048U)
/* Bytes per datagram; also the stack buffer in the board task. */
#define XF16CAM_LOG_CHUNK    (256U)

static char g_log_ring[XF16CAM_LOG_RING];
/* Single writer (any task printing, serialised by the stdout mutex), one
 * broadcast reader (the board task): monotonic byte counters, no lock.
 * /api/log readers only snapshot, so a torn read costs a garbled byte. */
static volatile uint32_t g_log_head;
static uint32_t g_log_tail;
static int g_log_socket = -1;
static struct sockaddr_in g_log_target;
static XF16CamLogInfo g_log_info;

/* Stays in SRAM on purpose: printf can run while flash is disabled for an
 * OTA piece or a settings write, and this is called from every printf. */
static int xf16cam_log_write(const char *buf, int len)
{
	uint32_t head;
	uint32_t i;

	if (len <= 0)
		return 0;
	board_uart_write(BOARD_MAIN_UART_ID, buf, len);
	head = g_log_head;
	for (i = 0; i < (uint32_t)len; ++i)
		g_log_ring[(head + i) & (XF16CAM_LOG_RING - 1U)] = buf[i];
	__sync_synchronize();
	g_log_head = head + (uint32_t)len;
	return len;
}

void xf16cam_log_init(void)
{
	g_log_head = 0;
	g_log_tail = 0;
	stdio_set_write(xf16cam_log_write);
}

__xip_text
static int xf16cam_log_socket_open(void)
{
	const char *ip = xf16cam_net_ip();
	int broadcast = 1;
	int fd;

	if (ip == NULL || ip[0] == '\0' || strcmp(ip, "0.0.0.0") == 0)
		return -1;
	fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (fd < 0)
		return -1;
	setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast));
	memset(&g_log_target, 0, sizeof(g_log_target));
	g_log_target.sin_family = AF_INET;
	g_log_target.sin_port = htons(XF16CAM_LOG_PORT);
	g_log_target.sin_addr.s_addr = INADDR_BROADCAST;
	g_log_socket = fd;
	return 0;
}

/* Copies ring bytes from *cursor, resynchronising a cursor the writer has
 * overtaken. Returns bytes copied, 0 when caught up. */
__xip_text
int xf16cam_log_read(uint32_t *cursor, char *out, uint32_t size)
{
	uint32_t head = g_log_head;
	uint32_t pending = head - *cursor;
	uint32_t i;

	if (pending > XF16CAM_LOG_RING) {
		*cursor = head - XF16CAM_LOG_RING;
		pending = XF16CAM_LOG_RING;
	}
	if (pending > size)
		pending = size;
	for (i = 0; i < pending; ++i)
		out[i] = g_log_ring[(*cursor + i) & (XF16CAM_LOG_RING - 1U)];
	*cursor += pending;
	return (int)pending;
}

/* Called from the board task every poll. Sends at most one datagram so the
 * task keeps its cadence; the ring absorbs bursts between polls. */
__xip_text
void xf16cam_log_poll(void)
{
	char chunk[XF16CAM_LOG_CHUNK];
	uint32_t tail = g_log_tail;
	int count;
	int sent;

	if (g_log_head == tail)
		return;
	if (g_log_socket < 0 && xf16cam_log_socket_open() != 0)
		return;
	if (g_log_head - tail > XF16CAM_LOG_RING)
		g_log_info.overruns++;
	count = xf16cam_log_read(&tail, chunk, sizeof(chunk));
	if (count <= 0)
		return;
	sent = sendto(g_log_socket, chunk, (size_t)count, 0,
	              (struct sockaddr *)&g_log_target, sizeof(g_log_target));
	if (sent != count) {
		/* Network not up yet, or transient: keep the bytes for next time. */
		g_log_info.send_errors++;
		return;
	}
	g_log_tail = tail;
	g_log_info.bytes += (uint32_t)count;
	g_log_info.datagrams++;
}

__xip_text
const XF16CamLogInfo *xf16cam_log_info(void)
{
	return &g_log_info;
}

#endif /* XF16CAM_NETLOG */
