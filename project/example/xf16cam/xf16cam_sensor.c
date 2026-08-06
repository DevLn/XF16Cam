#include <stdio.h>

#include "driver/chip/hal_gpio.h"
#include "driver/chip/hal_i2c.h"
#include "driver/component/csi_camera/gc0328c/drv_gc0328c.h"
#include "kernel/os/os.h"

#include "xf16cam_sensor.h"

#define XF16CAM_SENSOR_SETTLE_MS (100)

typedef struct {
	const char *name;
	SENSOR_Func driver;
	uint8_t address;
	uint8_t bank_register;
	uint8_t bank_value;
	uint8_t id_register;
	uint8_t id_value;
	uint16_t width;
	uint16_t height;
} XF16CamSensor;

/* Add new QVGA-capable sensors here; only referenced drivers enter the image. */
static const XF16CamSensor g_sensors[] = {
	{
		.name = "GC0328",
		.driver = {
			.init = HAL_GC0328C_Init,
			.deinit = HAL_GC0328C_DeInit,
			.suspend = HAL_GC0328C_Suspend,
			.resume = HAL_GC0328C_Resume,
			.ioctl = HAL_GC0328C_IoCtl,
		},
		.address = 0x21,
		.bank_register = 0xfe,
		.bank_value = 0x00,
		.id_register = 0xf0,
		.id_value = 0x9d,
		.width = 320,
		.height = 240,
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
				return g_selected->driver.init(cfg);
			}
		}
		HAL_I2C_DeInit(bus);
	}

	printf("xf16cam camera: no supported sensor (last id=0x%02x)\n", chip_id);
	return HAL_ERROR;
}

void xf16cam_sensor_deinit(SENSOR_ConfigParam *cfg)
{
	if (g_selected && g_selected->driver.deinit)
		g_selected->driver.deinit(cfg);
	g_selected = NULL;
}

HAL_Status xf16cam_sensor_ioctl(SENSOR_IoctrlCmd attr, uint32_t arg)
{
	if (g_selected && g_selected->driver.ioctl)
		return g_selected->driver.ioctl(attr, arg);
	return HAL_ERROR;
}

const char *xf16cam_sensor_name(void)
{
	return g_selected ? g_selected->name : "Unknown";
}

uint16_t xf16cam_sensor_width(void)
{
	return g_selected ? g_selected->width : 0;
}

uint16_t xf16cam_sensor_height(void)
{
	return g_selected ? g_selected->height : 0;
}
