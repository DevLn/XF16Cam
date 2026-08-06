/*
 * Copyright (C) 2017 XRADIO TECHNOLOGY CO., LTD.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kernel/os/os.h"
#include "common/framework/platform_init.h"
#include "common/framework/net_ctrl.h"
#include "driver/chip/hal_csi_jpeg.h"
#include "driver/chip/hal_gpio.h"
#include "driver/chip/hal_i2c.h"
#include "driver/chip/hal_prcm.h"
#include "driver/component/csi_camera/camera.h"
#include "driver/component/csi_camera/gc0328c/drv_gc0328c.h"
#include "net/wlan/wlan.h"
#include "net/wlan/wlan_defs.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"

#include "xf16cam_config.h"
#include "xf16cam_http.h"
#include "xf16cam_net.h"
#include "xf16cam_version.h"

#define JPEG_ONLINE_EN           (1)
#define JPEG_BUFFER_COUNT        (2)
#define JPEG_SRAM_SIZE           (106 * 1024)
#define JPEG_MPART_EN            (0)
#define JPEG_BUFF_SIZE           (50 * 1024)
#define JPEG_IMAGE_WIDTH         (320)
#define JPEG_IMAGE_HEIGHT        (240)
#define XF16CAM_RTSP_PORT        (8554)
#define XF16CAM_RTP_MTU          (1300)
#define XF16CAM_RTP_SSRC         (0x58463136UL)

#define XF16_SENSOR_I2C_ID       I2C0_ID
#define XF16_CTRL_PORT           GPIO_PORT_A
#define XF16_CTRL_PIN            GPIO_PIN_14
#define XF16_FACTORY_PA23_PORT    GPIO_PORT_A
#define XF16_FACTORY_PA23_PIN     GPIO_PIN_23
#define XF16_FACTORY_PA23_PULSE_MS (100)
#define XF16_SETTLE_MS           (100)
#define XF16_GC0328_ADDR         (0x21)
#define XF16_GC0328_CHIP_ID      (0x9d)
#define XF16_SAME_PIN_PWR(_port, _pin) \
	{ \
		.Reset_Port = (_port), \
		.Reset_Pin = (_pin), \
		.Pwdn_Port = (_port), \
		.Pwdn_Pin = (_pin), \
	}

typedef struct {
	const char *name;
	const SENSOR_Func *hooks;
	uint8_t addr;
	uint8_t id_reg;
	uint8_t id_value;
	uint32_t width;
	uint32_t height;
} XF16_SensorBackend;

static HAL_Status xf16_sensor_detect_wrapper(SENSOR_ConfigParam *cfg);
static void xf16_sensor_dispatch_deinit(SENSOR_ConfigParam *cfg);
static HAL_Status xf16_sensor_dispatch_ioctl(SENSOR_IoctrlCmd attr, uint32_t arg);
static void xf16_release_camera_wakeup_hold(void);
static void xf16_camera_ctrl_prehold_low(void);
static void xf16_factory_pa23_prepare(void);

static uint8_t *gmemaddr;
static CAMERA_Mgmt mem_mgmt;
static const XF16_SensorBackend *g_selected_backend;

static const SENSOR_Func g_gc0328_hooks = {
	.init = HAL_GC0328C_Init,
	.deinit = HAL_GC0328C_DeInit,
	.suspend = HAL_GC0328C_Suspend,
	.resume = HAL_GC0328C_Resume,
	.ioctl = HAL_GC0328C_IoCtl,
};

static const XF16_SensorBackend g_gc0328_backend = {
	.name = "gc0328",
	.hooks = &g_gc0328_hooks,
	.addr = XF16_GC0328_ADDR,
	.id_reg = 0xf0,
	.id_value = XF16_GC0328_CHIP_ID,
	.width = JPEG_IMAGE_WIDTH,
	.height = JPEG_IMAGE_HEIGHT,
};
static CAMERA_Cfg camera_cfg = {
	.jpeg_cfg.jpeg_en = 1,
	.jpeg_cfg.quality = 60,
	.jpeg_cfg.jpeg_clk = 0,
	.jpeg_cfg.memPartEn = JPEG_MPART_EN,
	.jpeg_cfg.memPartNum = 0,
	.jpeg_cfg.jpeg_mode = JPEG_ONLINE_EN ? JPEG_MOD_ONLINE : JPEG_MOD_OFFLINE,
	.jpeg_cfg.width = JPEG_IMAGE_WIDTH,
	.jpeg_cfg.height = JPEG_IMAGE_HEIGHT,

	.csi_cfg.csi_clk = 24000000,
	.csi_cfg.hor_start = 0,
	.csi_cfg.ver_start = 0,

	.sensor_cfg.i2c_id = XF16_SENSOR_I2C_ID,
	.sensor_cfg.pwcfg = XF16_SAME_PIN_PWR(XF16_CTRL_PORT, XF16_CTRL_PIN),

	.sensor_func.init = xf16_sensor_detect_wrapper,
	.sensor_func.deinit = xf16_sensor_dispatch_deinit,
	.sensor_func.ioctl = xf16_sensor_dispatch_ioctl,
};

/* XF16 board-specific camera rail and control-pin preparation. */
static void xf16_release_camera_wakeup_hold(void)
{
	uint32_t mask = HAL_BIT(4) | HAL_BIT(5);
	uint32_t before = PRCM->CPUA_WAKE_IO_HOLD;

	HAL_PRCM_WakeupIODisableCfgHold(mask);
	printf("xf16 wake hold release: mask=0x%08lx before=0x%08lx after=0x%08lx wake_en=0x%08lx\n",
	       (unsigned long)mask,
	       (unsigned long)before,
	       (unsigned long)PRCM->CPUA_WAKE_IO_HOLD,
	       (unsigned long)PRCM->CPUA_WAKE_IO_EN);
}

static void xf16_camera_ctrl_prehold_low(void)
{
	GPIO_InitParam param;

	param.driving = GPIO_DRIVING_LEVEL_1;
	param.mode = GPIOx_Pn_F1_OUTPUT;
	param.pull = GPIO_PULL_NONE;
	HAL_GPIO_Init(XF16_CTRL_PORT, XF16_CTRL_PIN, &param);
	HAL_GPIO_WritePin(XF16_CTRL_PORT, XF16_CTRL_PIN, GPIO_PIN_LOW);
	OS_MSleep(20);
}

static void xf16_factory_pa23_prepare(void)
{
	GPIO_InitParam param;

	param.driving = GPIO_DRIVING_LEVEL_1;
	param.mode = GPIOx_Pn_F1_OUTPUT;
	param.pull = GPIO_PULL_NONE;
	HAL_GPIO_Init(XF16_FACTORY_PA23_PORT, XF16_FACTORY_PA23_PIN, &param);
	HAL_GPIO_WritePin(XF16_FACTORY_PA23_PORT, XF16_FACTORY_PA23_PIN, GPIO_PIN_LOW);
	printf("xf16 factory PA23 pulse: low %u ms then high\n",
	       (unsigned int)XF16_FACTORY_PA23_PULSE_MS);
	OS_MSleep(XF16_FACTORY_PA23_PULSE_MS);
	HAL_GPIO_WritePin(XF16_FACTORY_PA23_PORT, XF16_FACTORY_PA23_PIN, GPIO_PIN_HIGH);
}

static void xf16_board_camera_power_prepare(void)
{
	HAL_PRCM_SelectEXTLDOVolt(PRCM_EXT_LDO_3V3);
	HAL_PRCM_SetEXTLDOMode(PRCM_EXTLDO_ALWAYS_ON);
	HAL_PRCM_SetTOPLDOVoltage(PRCM_TOPLDO_VOLT_2V8);
	OS_MSleep(20);
	xf16_release_camera_wakeup_hold();
	xf16_factory_pa23_prepare();
	xf16_camera_ctrl_prehold_low();
	printf("camera rail prep done: top_ldo=0x%08lx ldo1=0x%08lx sys_pwr=0x%08lx ldo_sw=0x%08lx wake_hold=0x%08lx wake_en=0x%08lx\n",
	       (unsigned long)HAL_PRCM_GetTOPLDOVoltage(),
	       (unsigned long)HAL_PRCM_GetLDO1WorkVolt(),
	       (unsigned long)HAL_PRCM_GetSysPowerEnableFlags(),
	       (unsigned long)PRCM->SYS_LDO_SW_CTRL,
	       (unsigned long)PRCM->CPUA_WAKE_IO_HOLD,
	       (unsigned long)PRCM->CPUA_WAKE_IO_EN);
}

/* Factory-style GC0328 detect wrapper used by HAL_CAMERA_Init(). */
static void xf16_drive_ctrl(GPIO_PinState state, const char *phase)
{
	GPIO_InitParam param;

	param.driving = GPIO_DRIVING_LEVEL_1;
	param.mode = GPIOx_Pn_F1_OUTPUT;
	param.pull = GPIO_PULL_NONE;
	HAL_GPIO_Init(XF16_CTRL_PORT, XF16_CTRL_PIN, &param);
	HAL_GPIO_WritePin(XF16_CTRL_PORT, XF16_CTRL_PIN, state);
	printf("xf16 wrapper phase=%s ctrl=PA14 state=%u settle_ms=%u\n",
	       phase,
	       (unsigned int)state,
	       (unsigned int)XF16_SETTLE_MS);
	OS_MSleep(XF16_SETTLE_MS);
}

static int xf16_sccb_write_reg(I2C_ID bus, uint8_t dev_addr, uint8_t reg, uint8_t value, const char *tag)
{
	int32_t ret;
	uint8_t tmp = value;

	ret = HAL_I2C_SCCB_Master_Transmit_IT(bus, dev_addr, reg, &tmp);
	printf("xf16 gc0328 probe %s write dev=0x%02x reg=0x%02x val=0x%02x ret=%ld\n",
	       tag,
	       dev_addr,
	       reg,
	       value,
	       (long)ret);
	return ret == 1;
}

static int xf16_sccb_read_reg(I2C_ID bus, uint8_t dev_addr, uint8_t reg, uint8_t *value, const char *tag)
{
	int32_t ret;

	*value = 0;
	ret = HAL_I2C_SCCB_Master_Receive_IT(bus, dev_addr, reg, value);
	printf("xf16 gc0328 probe %s read dev=0x%02x reg=0x%02x ret=%ld val=0x%02x expect=0x%02x\n",
	       tag,
	       dev_addr,
	       reg,
	       (long)ret,
	       *value,
	       XF16_GC0328_CHIP_ID);
	return ret == 1;
}

static int xf16_probe_gc0328(I2C_ID bus, const char *phase, uint8_t *chip_id)
{
	uint8_t value;

	printf("xf16 gc0328 probe begin phase=%s dev=0x%02x id_reg=0x%02x expect=0x%02x\n",
	       phase,
	       g_gc0328_backend.addr,
	       g_gc0328_backend.id_reg,
	       g_gc0328_backend.id_value);
	if (!xf16_sccb_write_reg(bus, g_gc0328_backend.addr, 0xfe, 0x00, phase))
		return 0;
	if (!xf16_sccb_read_reg(bus, g_gc0328_backend.addr, g_gc0328_backend.id_reg, &value, phase))
		return 0;
	*chip_id = value;
	if (value == g_gc0328_backend.id_value) {
		printf("---->detect %s\n", g_gc0328_backend.name);
		return 1;
	}
	printf("xf16 gc0328 probe mismatch phase=%s got=0x%02x expect=0x%02x\n",
	       phase,
	       value,
	       g_gc0328_backend.id_value);
	return 0;
}

static HAL_Status xf16_sccb_init_bus(I2C_ID bus, const char *tag)
{
	I2C_InitParam initParam;
	HAL_Status status;

	initParam.addrMode = I2C_ADDR_MODE_7BIT;
	initParam.clockFreq = 100000;
	status = HAL_I2C_Init(bus, &initParam);
	printf("xf16 sccb init %s: bus=%u addr_mode=%u clock=%lu status=%ld\n",
	       tag,
	       (unsigned int)bus,
	       (unsigned int)initParam.addrMode,
	       (unsigned long)initParam.clockFreq,
	       (long)status);
	return status;
}

static void xf16_sccb_deinit_bus(I2C_ID bus)
{
	HAL_I2C_DeInit(bus);
}

static HAL_Status xf16_call_selected_backend(SENSOR_ConfigParam *cfg)
{
	HAL_Status status;

	if (!g_selected_backend || !g_selected_backend->hooks || !g_selected_backend->hooks->init)
		return HAL_ERROR;

	HAL_I2C_DeInit((I2C_ID)cfg->i2c_id);
	status = g_selected_backend->hooks->init(cfg);
	if (status == HAL_OK) {
		camera_cfg.jpeg_cfg.width = g_selected_backend->width;
		camera_cfg.jpeg_cfg.height = g_selected_backend->height;
	}
	return status;
}

static HAL_Status xf16_sensor_detect_wrapper(SENSOR_ConfigParam *cfg)
{
	HAL_Status status;
	uint8_t chip_id = 0;
	I2C_ID bus;

	if (!cfg)
		return HAL_ERROR;

	bus = (I2C_ID)cfg->i2c_id;
	g_selected_backend = NULL;
	printf("xf16 detect ctx: gc0328-only bus=%u ctrl=PA14 settle_ms=%u\n",
	       (unsigned int)bus,
	       (unsigned int)XF16_SETTLE_MS);

	xf16_drive_ctrl(GPIO_PIN_HIGH, "primary_high");
	status = xf16_sccb_init_bus(bus, "after_primary_init");
	if (status != HAL_OK)
		return HAL_ERROR;

	if (xf16_probe_gc0328(bus, "primary_high", &chip_id)) {
		g_selected_backend = &g_gc0328_backend;
		return xf16_call_selected_backend(cfg);
	}

	xf16_sccb_deinit_bus(bus);
	xf16_drive_ctrl(GPIO_PIN_LOW, "fallback_low");
	status = xf16_sccb_init_bus(bus, "after_fallback_init");
	if (status != HAL_OK)
		return HAL_ERROR;

	if (xf16_probe_gc0328(bus, "fallback_low", &chip_id)) {
		g_selected_backend = &g_gc0328_backend;
		return xf16_call_selected_backend(cfg);
	}

	xf16_sccb_deinit_bus(bus);
	printf("xf16 gc0328 detect failed: last_chip=0x%02x\n", chip_id);
	return HAL_ERROR;
}

static void xf16_sensor_dispatch_deinit(SENSOR_ConfigParam *cfg)
{
	if (g_selected_backend && g_selected_backend->hooks && g_selected_backend->hooks->deinit)
		g_selected_backend->hooks->deinit(cfg);
	g_selected_backend = NULL;
}

static HAL_Status xf16_sensor_dispatch_ioctl(SENSOR_IoctrlCmd attr, uint32_t arg)
{
	if (g_selected_backend && g_selected_backend->hooks && g_selected_backend->hooks->ioctl)
		return g_selected_backend->hooks->ioctl(attr, arg);
	return HAL_ERROR;
}

/* JPEG demo buffer, capture, and SD-file handling. */
static int camera_mem_create(CAMERA_JpegCfg *jpeg_cfg, CAMERA_Mgmt *mgmt)
{
	uint8_t *addr;
	uint8_t *end_addr;
	uint8_t *cursor;
	uint32_t i;
	(void)jpeg_cfg;

	addr = (uint8_t *)malloc(JPEG_SRAM_SIZE);
	if (!addr) {
		printf("malloc fail\n");
		return -1;
	}
	memset(addr, 0, JPEG_SRAM_SIZE);
	end_addr = addr + JPEG_SRAM_SIZE;
	printf("malloc addr: %p -> %p\n", addr, end_addr);

	/* Online JPEG mode does not consume a YUV framebuffer. Keep two encoded
	 * frames so the network task has one full frame of overwrite tolerance. */
	mgmt->yuv_buf.addr = NULL;
	mgmt->yuv_buf.size = 0;
	cursor = addr;
	for (i = 0; i < JPEG_BUFFER_COUNT; i++) {
		mgmt->jpeg_buf[i].addr =
			(uint8_t *)ALIGN_1K((uint32_t)cursor + CAMERA_JPEG_HEADER_LEN);
		mgmt->jpeg_buf[i].size = JPEG_BUFF_SIZE;
		cursor = mgmt->jpeg_buf[i].addr + JPEG_BUFF_SIZE;
		if (cursor > end_addr) {
			printf("jpeg buffer %lu exceeds capture arena\n", (unsigned long)i);
			free(addr);
			return -1;
		}
	}
	printf("xf16cam buffers: count=%u bytes_each=%u arena=%u\n",
	       JPEG_BUFFER_COUNT, JPEG_BUFF_SIZE, JPEG_SRAM_SIZE);

	gmemaddr = addr;
	return 0;
}

static void camera_mem_destroy(void)
{
	if (gmemaddr) {
		free(gmemaddr);
		gmemaddr = NULL;
	}
}

static void camera_deinit(void)
{
	HAL_CAMERA_DeInit();
	camera_mem_destroy();
}

static int camera_init(void)
{
	memset(&mem_mgmt, 0, sizeof(mem_mgmt));
	if (camera_mem_create(&camera_cfg.jpeg_cfg, &mem_mgmt) != 0)
		return -1;

	camera_cfg.mgmt = &mem_mgmt;
	if (HAL_CAMERA_Init(&camera_cfg) != HAL_OK) {
		printf("HAL_CAMERA_Init failed\n");
		return -1;
	}
	return 0;
}

typedef struct {
	const uint8_t *scan;
	uint32_t scan_len;
	uint16_t width;
	uint16_t height;
	uint8_t type;
	uint8_t qtables[128];
	uint8_t qtable_mask;
} XF16CamJpeg;

static uint16_t xf16_be16(const uint8_t *p)
{
	return ((uint16_t)p[0] << 8) | p[1];
}

/* Extract the entropy scan and RFC 2435 metadata from a complete JPEG. */
static int xf16cam_parse_jpeg(const uint8_t *jpeg, uint32_t len, XF16CamJpeg *out)
{
	uint32_t pos = 2;
	int32_t eoi;

	memset(out, 0, sizeof(*out));
	if (len < 4 || jpeg[0] != 0xff || jpeg[1] != 0xd8)
		return -1;

	while (pos + 4 <= len) {
		uint8_t marker;
		uint16_t seg_len;
		const uint8_t *data;
		uint32_t data_len;

		while (pos < len && jpeg[pos] == 0xff)
			pos++;
		if (pos >= len)
			break;
		marker = jpeg[pos++];
		if (marker == 0xd9)
			break;
		if (marker == 0x01 || (marker >= 0xd0 && marker <= 0xd7))
			continue;
		if (pos + 2 > len)
			return -1;
		seg_len = xf16_be16(jpeg + pos);
		if (seg_len < 2 || pos + seg_len > len)
			return -1;
		data = jpeg + pos + 2;
		data_len = seg_len - 2;

		if (marker == 0xdb) {
			uint32_t qpos = 0;
			while (qpos < data_len) {
				uint8_t pq_tq = data[qpos++];
				uint32_t qlen = (pq_tq >> 4) ? 128 : 64;
				uint8_t id = pq_tq & 0x0f;
				if (qpos + qlen > data_len)
					return -1;
				if ((pq_tq >> 4) == 0 && id < 2) {
					memcpy(out->qtables + id * 64, data + qpos, 64);
					out->qtable_mask |= (uint8_t)(1U << id);
				}
				qpos += qlen;
			}
		} else if (marker == 0xc0 && data_len >= 8) {
			out->height = xf16_be16(data + 1);
			out->width = xf16_be16(data + 3);
			/* RFC 2435 type 1 is 4:2:0, type 0 is 4:2:2. */
			out->type = (data[7] == 0x22) ? 1 : 0;
		} else if (marker == 0xda) {
			out->scan = jpeg + pos + seg_len;
			break;
		}
		pos += seg_len;
	}

	if (!out->scan || !out->width || !out->height || out->qtable_mask != 3)
		return -1;
	for (eoi = (int32_t)len - 2; eoi >= (int32_t)(out->scan - jpeg); eoi--) {
		if (jpeg[eoi] == 0xff && jpeg[eoi + 1] == 0xd9) {
			out->scan_len = (uint32_t)((jpeg + eoi) - out->scan);
			return out->scan_len ? 0 : -1;
		}
	}
	/* XR872's encoder length can omit the terminal EOI marker. RTP/JPEG
	 * transports entropy data only, so the complete remaining buffer is valid. */
	out->scan_len = len - (uint32_t)(out->scan - jpeg);
	return out->scan_len ? 0 : -1;
}

static int xf16cam_send_all(int fd, const void *data, uint32_t len)
{
	const uint8_t *p = data;
	while (len) {
		int n = send(fd, p, len, 0);
		if (n <= 0)
			return -1;
		p += n;
		len -= (uint32_t)n;
	}
	return 0;
}

static int xf16cam_send_rtp_jpeg(int fd, const XF16CamJpeg *jpg,
				 uint16_t *sequence, uint32_t timestamp)
{
	uint32_t offset = 0;
	while (offset < jpg->scan_len) {
		uint8_t header[24];
		uint8_t qheader[132];
		uint32_t payload = jpg->scan_len - offset;
		uint32_t extra = offset == 0 ? sizeof(qheader) : 0;
		uint16_t interleaved_len;
		int last;

		if (payload > XF16CAM_RTP_MTU)
			payload = XF16CAM_RTP_MTU;
		last = (offset + payload == jpg->scan_len);
		interleaved_len = (uint16_t)(12 + 8 + extra + payload);
		header[0] = '$'; header[1] = 0;
		header[2] = (uint8_t)(interleaved_len >> 8);
		header[3] = (uint8_t)interleaved_len;
		header[4] = 0x80; header[5] = (uint8_t)(26 | (last ? 0x80 : 0));
		header[6] = (uint8_t)(*sequence >> 8); header[7] = (uint8_t)*sequence;
		header[8] = (uint8_t)(timestamp >> 24); header[9] = (uint8_t)(timestamp >> 16);
		header[10] = (uint8_t)(timestamp >> 8); header[11] = (uint8_t)timestamp;
		header[12] = (uint8_t)(XF16CAM_RTP_SSRC >> 24);
		header[13] = (uint8_t)(XF16CAM_RTP_SSRC >> 16);
		header[14] = (uint8_t)(XF16CAM_RTP_SSRC >> 8);
		header[15] = (uint8_t)XF16CAM_RTP_SSRC;
		header[16] = 0;
		header[17] = (uint8_t)(offset >> 16);
		header[18] = (uint8_t)(offset >> 8);
		header[19] = (uint8_t)offset;
		header[20] = jpg->type; header[21] = 255;
		header[22] = (uint8_t)((jpg->width + 7) / 8);
		header[23] = (uint8_t)((jpg->height + 7) / 8);
		(*sequence)++;

		if (xf16cam_send_all(fd, header, sizeof(header)) != 0)
			return -1;
		if (offset == 0) {
			qheader[0] = 0; qheader[1] = 0;
			qheader[2] = 0; qheader[3] = 128;
			memcpy(qheader + 4, jpg->qtables, 128);
			if (xf16cam_send_all(fd, qheader, sizeof(qheader)) != 0)
				return -1;
		}
		if (xf16cam_send_all(fd, jpg->scan + offset, payload) != 0)
			return -1;
		offset += payload;
	}
	return 0;
}

static int xf16cam_cseq(const char *request)
{
	const char *p = strstr(request, "CSeq:");
	return p ? atoi(p + 5) : 0;
}

/* Returns 1 for PLAY, 2 for TEARDOWN, and 0 for other requests. */
static int xf16cam_rtsp_reply(int fd, const char *request, const char *ip,
			      uint16_t sequence, uint32_t timestamp)
{
	char response[768];
	char sdp[256];
	int cseq = xf16cam_cseq(request);
	int n;

	if (!strncmp(request, "OPTIONS ", 8)) {
		n = snprintf(response, sizeof(response),
			     "RTSP/1.0 200 OK\r\nCSeq: %d\r\nPublic: OPTIONS, DESCRIBE, SETUP, PLAY, TEARDOWN, GET_PARAMETER\r\n\r\n",
			     cseq);
	} else if (!strncmp(request, "DESCRIBE ", 9)) {
		int sdp_len = snprintf(sdp, sizeof(sdp),
			"v=0\r\no=- 0 0 IN IP4 %s\r\ns=XF16 GC0328\r\nc=IN IP4 %s\r\nt=0 0\r\n"
			"m=video 0 RTP/AVP 26\r\na=rtpmap:26 JPEG/90000\r\na=control:track1\r\n",
			ip, ip);
		n = snprintf(response, sizeof(response),
			     "RTSP/1.0 200 OK\r\nCSeq: %d\r\nContent-Base: rtsp://%s:%u/stream/\r\n"
			     "Content-Type: application/sdp\r\nContent-Length: %d\r\n\r\n%s",
			     cseq, ip, XF16CAM_RTSP_PORT, sdp_len, sdp);
	} else if (!strncmp(request, "SETUP ", 6)) {
		n = snprintf(response, sizeof(response),
			     "RTSP/1.0 200 OK\r\nCSeq: %d\r\nSession: 58463136\r\n"
			     "Transport: RTP/AVP/TCP;unicast;interleaved=0-1\r\n\r\n", cseq);
	} else if (!strncmp(request, "PLAY ", 5)) {
		n = snprintf(response, sizeof(response),
			     "RTSP/1.0 200 OK\r\nCSeq: %d\r\nSession: 58463136\r\nRange: npt=0.000-\r\n"
			     "RTP-Info: url=rtsp://%s:%u/stream/track1;seq=%u;rtptime=%lu\r\n\r\n",
			     cseq, ip, XF16CAM_RTSP_PORT, sequence, (unsigned long)timestamp);
	} else if (!strncmp(request, "TEARDOWN ", 9)) {
		n = snprintf(response, sizeof(response),
			     "RTSP/1.0 200 OK\r\nCSeq: %d\r\nSession: 58463136\r\n\r\n", cseq);
		if (n > 0)
			xf16cam_send_all(fd, response, (uint32_t)n);
		return 2;
	} else {
		n = snprintf(response, sizeof(response),
			     "RTSP/1.0 200 OK\r\nCSeq: %d\r\nSession: 58463136\r\n\r\n", cseq);
	}
	if (n <= 0 || n >= (int)sizeof(response) ||
	    xf16cam_send_all(fd, response, (uint32_t)n) != 0)
		return -1;
	return !strncmp(request, "PLAY ", 5) ? 1 : 0;
}

static int xf16cam_stream_client(int fd, const char *ip)
{
	char request[1024];
	uint16_t sequence = 1;
	uint32_t timestamp = OS_TicksToMSecs(OS_GetTicks()) * 90U;
	int timeout_ms = 50;
	int playing = 0;

	while (!playing) {
		int n = recv(fd, request, sizeof(request) - 1, 0);
		int action;
		if (n <= 0)
			return -1;
		request[n] = '\0';
		action = xf16cam_rtsp_reply(fd, request, ip, sequence, timestamp);
		if (action < 0 || action == 2)
			return -1;
		playing = action == 1;
	}

	setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout_ms, sizeof(timeout_ms));
	if (HAL_CAMERA_CaptureVideoStart() != 0)
		return -1;
	printf("xf16cam PLAY: RTP/JPEG over RTSP TCP\n");
	while (1) {
		CAMERA_JpegBuffInfo info;
		XF16CamJpeg jpg;
		uint8_t *jpeg;
		uint32_t jpeg_len;
		int n;

		if (HAL_CAMERA_CaptureVideoData(&info) != 0)
			break;
		if (info.buff_index >= JPEG_BUFFER_COUNT)
			continue;
		jpeg = mem_mgmt.jpeg_buf[info.buff_index].addr - CAMERA_JPEG_HEADER_LEN;
		jpeg_len = info.size + CAMERA_JPEG_HEADER_LEN;
		if (xf16cam_parse_jpeg(jpeg, jpeg_len, &jpg) != 0) {
			printf("xf16cam invalid jpeg: index=%u len=%lu\n",
			       info.buff_index, (unsigned long)jpeg_len);
			continue;
		}
		if (xf16cam_send_rtp_jpeg(fd, &jpg, &sequence, timestamp) != 0)
			break;
		timestamp = OS_TicksToMSecs(OS_GetTicks()) * 90U;

		n = recv(fd, request, sizeof(request) - 1, 0);
		if (n == 0)
			break;
		if (n > 0 && request[0] != '$') {
			int action;
			request[n] = '\0';
			action = xf16cam_rtsp_reply(fd, request, ip, sequence, timestamp);
			if (action < 0 || action == 2)
				break;
		}
	}
	HAL_CAMERA_CaptureVideoStop();
	printf("xf16cam client stopped\n");
	return 0;
}

static void xf16cam_rtsp_server(void)
{
	int server;
	struct sockaddr_in addr;
	const char *ip = xf16cam_net_ip();

	server = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (server < 0) {
		printf("xf16cam socket failed\n");
		return;
	}
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(XF16CAM_RTSP_PORT);
	addr.sin_addr.s_addr = INADDR_ANY;
	if (bind(server, (struct sockaddr *)&addr, sizeof(addr)) != 0 ||
	    listen(server, 1) != 0) {
		printf("xf16cam bind/listen failed\n");
		closesocket(server);
		return;
	}
	printf("xf16cam ready: rtsp://%s:%u/stream\n", ip, XF16CAM_RTSP_PORT);
	while (1) {
		int client = accept(server, NULL, NULL);
		if (client < 0)
			continue;
		printf("xf16cam RTSP client connected\n");
		xf16cam_stream_client(client, ip);
		closesocket(client);
	}
}

int main(void)
{
	platform_init();
	printf("xf16cam version %s\n", XF16CAM_VERSION);

	/* Reserve the one large contiguous block before AP/DHCP activity can
	 * fragment the heap. No capture starts until an RTSP client sends PLAY. */
	xf16_board_camera_power_prepare();
	if (camera_init() != 0) {
		camera_mem_destroy();
		return -1;
	}
	xf16cam_config_init();
	if (xf16cam_net_start(xf16cam_config_get()) != 0)
		return -1;
	xf16cam_http_start();
	xf16cam_rtsp_server();
	camera_deinit();
	return -1;
}
