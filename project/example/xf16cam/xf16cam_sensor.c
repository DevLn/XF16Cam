#include <stdio.h>

#include "compiler.h"
#include "driver/chip/hal_gpio.h"
#include "driver/chip/hal_i2c.h"
#include "driver/component/csi_camera/camera.h"
#include "driver/component/csi_camera/gc0328c/drv_gc0328c.h"
#include "kernel/os/os.h"

#include "xf16cam_sensor.h"
#include "xf16cam_sensor_tables.h"

#define XF16CAM_SENSOR_SETTLE_MS (100)

typedef struct {
	const char *name;
	const SENSOR_Func *driver;
	uint8_t address;
	uint8_t bank_register;
	uint8_t bank_value;
	uint8_t id_register;
	uint8_t id_value;
	const uint8_t *table;
	uint16_t table_size;
	uint16_t input_width;
	uint16_t input_height;
	uint16_t output_width;
	uint16_t output_height;
} XF16CamSensor;

__xip_rodata static const SENSOR_Func g_gc0328_driver = {
	.init = HAL_GC0328C_Init,
	.deinit = HAL_GC0328C_DeInit,
	.suspend = HAL_GC0328C_Suspend,
	.resume = HAL_GC0328C_Resume,
	.ioctl = HAL_GC0328C_IoCtl,
};

/* Native SDK drivers and compact register-table backends share one probe list. */
__xip_rodata static const XF16CamSensor g_sensors[] = {
	{
		.name = "GC0328",
		.driver = &g_gc0328_driver,
		.address = 0x21,
		.bank_register = 0xfe,
		.bank_value = 0x00,
		.id_register = 0xf0,
		.id_value = 0x9d,
		.input_width = 320,
		.input_height = 240,
		.output_width = 320,
		.output_height = 240,
	},
	{
		.name = "GC0308",
		.address = 0x21,
		.bank_register = 0xfe,
		.bank_value = 0x00,
		.id_register = 0x00,
		.id_value = 0x9b,
		.table = xf16cam_gc0308_table,
		.table_size = XF16CAM_GC0308_TABLE_SIZE,
		.input_width = 640,
		.input_height = 480,
		.output_width = 320,
		.output_height = 240,
	},
	{
		.name = "HI704",
		.address = 0x30,
		.bank_register = 0x03,
		.bank_value = 0x00,
		.id_register = 0x04,
		.id_value = 0x96,
		.table = xf16cam_hi704_table,
		.table_size = XF16CAM_HI704_TABLE_SIZE,
		.input_width = 640,
		.input_height = 480,
		.output_width = 320,
		.output_height = 240,
	},
	{
		.name = "SP0A20",
		.address = 0x21,
		.bank_register = 0xfd,
		.bank_value = 0x00,
		.id_register = 0x02,
		.id_value = 0x2b,
		.table = xf16cam_sp0a20_table,
		.table_size = XF16CAM_SP0A20_TABLE_SIZE,
		.input_width = 640,
		.input_height = 480,
		.output_width = 320,
		.output_height = 240,
	},
	{
		.name = "SP0828",
		.address = 0x18,
		.bank_register = 0xfd,
		.bank_value = 0x00,
		.id_register = 0x02,
		.id_value = 0x0c,
		.table = xf16cam_sp0828_table,
		.table_size = XF16CAM_SP0828_TABLE_SIZE,
		.input_width = 240,
		.input_height = 320,
		.output_width = 240,
		.output_height = 320,
	},
};

static const XF16CamSensor *g_selected;

static void xf16cam_sensor_control(const SENSOR_ConfigParam *cfg, GPIO_PinState state)
{
	GPIO_InitParam param;

	param.driving = GPIO_DRIVING_LEVEL_1;
	param.mode = GPIOx_Pn_F1_OUTPUT;
	param.pull = GPIO_PULL_NONE;
	HAL_GPIO_Init(cfg->pwcfg.Pwdn_Port, cfg->pwcfg.Pwdn_Pin, &param);
	HAL_GPIO_WritePin(cfg->pwcfg.Pwdn_Port, cfg->pwcfg.Pwdn_Pin, state);
	OS_MSleep(XF16CAM_SENSOR_SETTLE_MS);
}

static HAL_Status xf16cam_sccb_init(I2C_ID bus)
{
	I2C_InitParam param;
	HAL_Status status;

	param.addrMode = I2C_ADDR_MODE_7BIT;
	param.clockFreq = 100000;
	status = HAL_I2C_Init(bus, &param);
	if (status != HAL_OK)
		printf("xf16cam camera: SCCB init failed (%ld)\n", (long)status);
	return status;
}

static int xf16cam_sensor_probe(I2C_ID bus, const XF16CamSensor *sensor,
				uint8_t *chip_id)
{
	uint8_t value = sensor->bank_value;

	if (HAL_I2C_SCCB_Master_Transmit_IT(bus, sensor->address,
	                                  sensor->bank_register, &value) != 1)
		return 0;
	value = 0;
	if (HAL_I2C_SCCB_Master_Receive_IT(bus, sensor->address,
	                                 sensor->id_register, &value) != 1)
		return 0;
	*chip_id = value;
	return value == sensor->id_value;
}

static HAL_Status xf16cam_sensor_load_table(SENSOR_ConfigParam *cfg)
{
	uint16_t size;
	uint16_t offset;
	I2C_ID bus = (I2C_ID)cfg->i2c_id;

	if (!g_selected || !g_selected->table || !g_selected->table_size)
		return HAL_ERROR;
	size = g_selected->table_size;
	if (xf16cam_sccb_init(bus) != HAL_OK)
		return HAL_ERROR;

	for (offset = 0; offset + 1 < size; offset += 2) {
		uint8_t reg = g_selected->table[offset];
		uint8_t value = g_selected->table[offset + 1];
		int attempt;

		if (reg == 0xff && value == 0xff)
			return HAL_OK;
		if (reg == 0xff && (value == 0xfe || value == 0xfd)) {
			OS_MSleep(value == 0xfe ? 100 : 1000);
			continue;
		}
		for (attempt = 0; attempt < 3; ++attempt) {
			uint8_t data = value;
			if (HAL_I2C_SCCB_Master_Transmit_IT(bus, g_selected->address,
			                                  reg, &data) == 1)
				break;
			OS_MSleep(2);
		}
		if (attempt == 3) {
			printf("xf16cam camera: %s table write failed at %u\n",
			       g_selected->name, (unsigned int)(offset / 2));
			HAL_I2C_DeInit(bus);
			return HAL_ERROR;
		}
		if (offset == 0)
			OS_MSleep(1);
	}

	printf("xf16cam camera: %s table has no terminator\n", g_selected->name);
	HAL_I2C_DeInit(bus);
	return HAL_ERROR;
}

HAL_Status xf16cam_sensor_init(SENSOR_ConfigParam *cfg)
{
	static const GPIO_PinState control_states[] = { GPIO_PIN_HIGH, GPIO_PIN_LOW };
	I2C_ID bus;
	uint8_t chip_id = 0;
	unsigned int phase;
	unsigned int index;

	if (!cfg)
		return HAL_ERROR;
	bus = (I2C_ID)cfg->i2c_id;
	g_selected = NULL;

	for (phase = 0; phase < sizeof(control_states) / sizeof(control_states[0]); ++phase) {
		xf16cam_sensor_control(cfg, control_states[phase]);
		if (xf16cam_sccb_init(bus) != HAL_OK)
			return HAL_ERROR;
		for (index = 0; index < sizeof(g_sensors) / sizeof(g_sensors[0]); ++index) {
			if (xf16cam_sensor_probe(bus, &g_sensors[index], &chip_id)) {
				g_selected = &g_sensors[index];
				HAL_I2C_DeInit(bus);
				printf("xf16cam camera: %s detected (id=0x%02x)\n",
				       g_selected->name, chip_id);
				if (g_selected->driver && g_selected->driver->init)
					return g_selected->driver->init(cfg);
				return xf16cam_sensor_load_table(cfg);
			}
		}
		HAL_I2C_DeInit(bus);
	}

	printf("xf16cam camera: no supported sensor (last id=0x%02x)\n", chip_id);
	return HAL_ERROR;
}

void xf16cam_sensor_deinit(SENSOR_ConfigParam *cfg)
{
	if (g_selected && g_selected->driver && g_selected->driver->deinit)
		g_selected->driver->deinit(cfg);
	else if (cfg)
		HAL_I2C_DeInit((I2C_ID)cfg->i2c_id);
	g_selected = NULL;
}

HAL_Status xf16cam_sensor_ioctl(SENSOR_IoctrlCmd attr, uint32_t arg)
{
	if (g_selected && g_selected->driver && g_selected->driver->ioctl)
		return g_selected->driver->ioctl(attr, arg);
	return HAL_ERROR;
}

int xf16cam_sensor_configure_camera(uint16_t configured_width,
				    uint16_t configured_height)
{
	SENSOR_PixelSize input;
	int scale;

	if (!g_selected)
		return -1;
	if (g_selected->input_width == configured_width &&
	    g_selected->input_height == configured_height)
		return 0;

	input.width = g_selected->input_width;
	input.height = g_selected->input_height;
	if (HAL_CAMERA_IoCtl(CAMERA_SET_PIXEL_SIZE, (uint32_t)&input) != 0 ||
	    HAL_CAMERA_IoCtl(CAMERA_SET_JPEG_MODE, JPEG_MOD_ONLINE) != 0)
		return -1;

	scale = g_selected->input_width != g_selected->output_width ||
	        g_selected->input_height != g_selected->output_height;
	if (!scale)
		return 0;
	if (g_selected->input_width != g_selected->output_width * 2 ||
	    g_selected->input_height != g_selected->output_height * 2)
		return -1;
	return HAL_CAMERA_IoCtl(CAMERA_SET_JPEG_SCALE, 1);
}

const char *xf16cam_sensor_name(void)
{
	return g_selected ? g_selected->name : "Unknown";
}

uint16_t xf16cam_sensor_width(void)
{
	return g_selected ? g_selected->output_width : 0;
}

uint16_t xf16cam_sensor_height(void)
{
	return g_selected ? g_selected->output_height : 0;
}
