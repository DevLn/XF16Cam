/*
 * Copyright (C) 2017 XRADIO TECHNOLOGY CO., LTD.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kernel/os/os.h"
#include "fs/fatfs/ff.h"
#include "common/framework/platform_init.h"
#include "common/framework/fs_ctrl.h"
#include "driver/chip/hal_csi_jpeg.h"
#include "driver/chip/hal_gpio.h"
#include "driver/chip/hal_i2c.h"
#include "driver/chip/hal_prcm.h"
#include "driver/component/csi_camera/camera.h"
#include "driver/component/csi_camera/gc0328c/drv_gc0328c.h"

#define JPEG_ONLINE_EN           (1)
#define JPEG_SRAM_SIZE           (220 * 1024)
#define JPEG_MPART_EN            (0)
#define JPEG_BUFF_SIZE           (50 * 1024)
#define JPEG_IMAGE_WIDTH         (320)
#define JPEG_IMAGE_HEIGHT        (240)
#define JPEG_OUTPUT_FILE         "test.jpg"
#define XF16_DEMO_REVISION       "v64 tidy-pass2"

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
static uint8_t g_sd_mounted;
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
static int fs_init(void)
{
	if (fs_ctrl_mount(FS_MNT_DEV_TYPE_SDCARD, 0) != 0) {
		printf("mount fail, continuing without SD write\n");
		return -1;
	}

	g_sd_mounted = 1;
	printf("mount success\n");
	return 0;
}

static int fs_deinit(void)
{
	if (!g_sd_mounted)
		return 0;

	g_sd_mounted = 0;
	if (fs_ctrl_unmount(FS_MNT_DEV_TYPE_SDCARD, 0) != 0) {
		printf("unmount fail\n");
		return -1;
	}

	printf("unmount success\n");
	return 0;
}

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

static void camera_power_prepare(void)
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

static int camera_mem_create(CAMERA_JpegCfg *jpeg_cfg, CAMERA_Mgmt *mgmt)
{
	uint8_t *addr;
	uint8_t *end_addr;

	addr = (uint8_t *)malloc(JPEG_SRAM_SIZE);
	if (!addr) {
		printf("malloc fail\n");
		return -1;
	}
	memset(addr, 0, JPEG_SRAM_SIZE);
	end_addr = addr + JPEG_SRAM_SIZE;
	printf("malloc addr: %p -> %p\n", addr, end_addr);

	mgmt->yuv_buf.addr = (uint8_t *)ALIGN_16B((uint32_t)addr);
	mgmt->yuv_buf.size = jpeg_cfg->width * jpeg_cfg->height * 3 / 2;
	mgmt->jpeg_buf[0].addr =
		(uint8_t *)ALIGN_1K((uint32_t)mgmt->yuv_buf.addr +
		                    mgmt->yuv_buf.size + CAMERA_JPEG_HEADER_LEN);
	mgmt->jpeg_buf[0].size = JPEG_BUFF_SIZE;

	if ((mgmt->yuv_buf.addr + mgmt->yuv_buf.size) > end_addr ||
	    (mgmt->jpeg_buf[0].addr + mgmt->jpeg_buf[0].size) > end_addr) {
		printf("addr exceeded\n");
		free(addr);
		return -1;
	}

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

static int xf16_find_last_jpeg_eoi(const uint8_t *addr, uint32_t size)
{
	int32_t i;

	if (size < 2)
		return -1;

	for (i = (int32_t)size - 2; i >= 0; i--) {
		if (addr[i] == 0xff && addr[i + 1] == 0xd9)
			return i;
	}
	return -1;
}

static int xf16_ensure_jpeg_eoi(uint8_t *addr, uint32_t *size, uint32_t capacity)
{
	int eoi = xf16_find_last_jpeg_eoi(addr, *size);

	if (eoi >= 0)
		return eoi;
	if (*size + 2 > capacity) {
		printf("xf16 jpeg eoi append failed: size=%lu capacity=%lu\n",
		       (unsigned long)*size,
		       (unsigned long)capacity);
		return -1;
	}

	addr[*size] = 0xff;
	addr[*size + 1] = 0xd9;
	eoi = (int)*size;
	*size += 2;
	printf("xf16 jpeg eoi appended: off=%ld total=%lu\n",
	       (long)eoi,
	       (unsigned long)*size);
	return eoi;
}

static int camera_write_jpeg_file(const uint8_t *addr, uint32_t size)
{
	FIL fp;
	uint32_t bw;
	int res;

	if (!g_sd_mounted && fs_init() != 0) {
		printf("SD not mounted, JPEG kept in RAM only\n");
		return 0;
	}

	f_unlink(JPEG_OUTPUT_FILE);
	res = f_open(&fp, JPEG_OUTPUT_FILE, FA_WRITE | FA_CREATE_NEW);
	if (res != FR_OK) {
		printf("open %s error %d\n", JPEG_OUTPUT_FILE, res);
		return -1;
	}

	res = f_write(&fp, addr, size, &bw);
	f_close(&fp);
	if (res != FR_OK || bw < size) {
		printf("write %s fail(%d), bw=%lu size=%lu\n",
		       JPEG_OUTPUT_FILE,
		       res,
		       (unsigned long)bw,
		       (unsigned long)size);
		return -1;
	}

	printf("write jpeg image ok: %s size=%lu\n", JPEG_OUTPUT_FILE, (unsigned long)size);
	return 0;
}

static int camera_get_image(void)
{
	CAMERA_JpegBuffInfo jpeg_info;
	uint8_t *addr;
	uint32_t size;

	printf("start capture\n");
	if (HAL_CAMERA_CaptureImage(CAMERA_OUT_JPEG, &jpeg_info, 1) < 0) {
		printf("capture fail\n");
		return -1;
	}

	addr = mem_mgmt.jpeg_buf[jpeg_info.buff_index].addr - CAMERA_JPEG_HEADER_LEN;
	size = jpeg_info.size + CAMERA_JPEG_HEADER_LEN;
	{
		uint32_t capacity = CAMERA_JPEG_HEADER_LEN + mem_mgmt.jpeg_buf[jpeg_info.buff_index].size;
		int eoi = xf16_ensure_jpeg_eoi(addr, &size, capacity);

		if (eoi < 0)
			return -1;
		printf("capture done: index=%u size=%lu addr=%p\n",
		       (unsigned int)jpeg_info.buff_index,
		       (unsigned long)size,
		       addr);
		printf("xf16 jpeg proof: soi=%u eoi=%u eoi_off=%ld eoi_tail_gap=%ld total=%lu payload=%lu\n",
		       (unsigned int)(size >= 2 && addr[0] == 0xff && addr[1] == 0xd8),
		       (unsigned int)(eoi >= 0),
		       (long)eoi,
		       (long)(size - (uint32_t)eoi - 2),
		       (unsigned long)size,
		       (unsigned long)jpeg_info.size);
		if (camera_write_jpeg_file(addr, size) != 0)
			return -1;
	}
	return 0;
}

int main(void)
{
	int ret = -1;
	uint8_t camera_started = 0;

	platform_init();
	printf("jpeg demo started (xf16 factory-wrapper " XF16_DEMO_REVISION ")\n");

	camera_power_prepare();
	if (camera_init() != 0) {
		camera_mem_destroy();
		goto exit_fs;
	}
	camera_started = 1;

	if (camera_get_image() != 0)
		goto exit_camera;

	ret = 0;

exit_camera:
	if (camera_started)
		camera_deinit();
exit_fs:
	fs_deinit();
	return ret;
}
