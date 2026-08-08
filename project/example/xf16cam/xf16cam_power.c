#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "compiler.h"
#include "driver/chip/hal_adc.h"
#include "driver/chip/hal_gpio.h"
#include "driver/chip/hal_prcm.h"
#include "driver/chip/hal_wdg.h"
#include "driver/chip/hal_wakeup.h"
#include "pm/pm.h"

#include "xf16cam_board.h"
#include "xf16cam_power.h"

#define XF16CAM_BATTERY_CHANNEL       ADC_CHANNEL_6
#define XF16CAM_BATTERY_SAMPLES       (10U)
#define XF16CAM_BATTERY_FULL_SCALE_MV (4250U)
#define XF16CAM_WAKE_IO_PA20          (6U)

static XF16CamPowerInfo g_power;

int xf16cam_power_measure(void)
{
	ADC_InitParam param;
	uint32_t sum = 0;
	uint32_t minimum = 0xfff;
	uint32_t maximum = 0;
	uint32_t sample;
	uint32_t average;
	unsigned int i;

	memset(&param, 0, sizeof(param));
	param.delay = 10;
	param.freq = 500000;
	param.vref_mode = 1; /* 2.5 V reference used by the factory A9 profile. */
	param.mode = ADC_CONTI_CONV;
	if (HAL_ADC_Init(&param) != HAL_OK)
		goto fail;
	for (i = 0; i < XF16CAM_BATTERY_SAMPLES; ++i) {
		if (HAL_ADC_Conv_Polling(XF16CAM_BATTERY_CHANNEL, &sample, 100) != HAL_OK) {
			HAL_ADC_DeInit();
			goto fail;
		}
		sample &= 0xfff;
		if (sample < minimum)
			minimum = sample;
		if (sample > maximum)
			maximum = sample;
		sum += sample;
	}
	HAL_ADC_DeInit();
	average = (sum - minimum - maximum) / (XF16CAM_BATTERY_SAMPLES - 2U);
	g_power.raw = (uint16_t)average;
	/* Factory divider is approximately 1.7:1: raw * 2500/4096 * 17/10. */
	g_power.millivolts = (uint16_t)((average * XF16CAM_BATTERY_FULL_SCALE_MV + 2048U) / 4096U);
	g_power.valid = 1;
	printf("xf16cam battery: raw=%u approximate=%u mV (uncalibrated)\n",
	       g_power.raw, g_power.millivolts);
	return 0;

fail:
	g_power.valid = 0;
	printf("xf16cam battery: ADC read failed\n");
	return -1;
}

const XF16CamPowerInfo *xf16cam_power_info(void)
{
	return &g_power;
}

void xf16cam_power_hibernate(void)
{
	GPIO_InitParam input = {
		.mode = GPIOx_Pn_F0_INPUT,
		.driving = GPIO_DRIVING_LEVEL_1,
		.pull = GPIO_PULL_UP,
	};

	xf16cam_board_prepare_sleep();
	HAL_GPIO_Init(GPIO_PORT_A, GPIO_PIN_20, &input);
	HAL_PRCM_SetWakeupDebClk0(0);
	HAL_PRCM_SetWakeupIOxDebSrc(XF16CAM_WAKE_IO_PA20, 0);
	HAL_PRCM_SetWakeupIOxDebounce(XF16CAM_WAKE_IO_PA20, 1);
	HAL_Wakeup_SetIO(XF16CAM_WAKE_IO_PA20, WKUPIO_WK_MODE_FALLING_EDGE,
	                 GPIO_PULL_UP);
	printf("xf16cam power: entering hibernation; press PA20 to wake\n");
	if (pm_enter_mode(PM_MODE_HIBERNATION) != 0)
		printf("xf16cam power: hibernation failed; rebooting\n");
	else
		printf("xf16cam power: hibernation returned unexpectedly; rebooting\n");
	/* A successful XR872 hibernation never returns and wakes through a cold
	 * boot. Recover the same way if platform PM rejects or exits the request;
	 * media and board services have already been quiesced by this point. */
	HAL_PRCM_SetCPUABootFlag(PRCM_CPUA_BOOT_FROM_COLD_RESET);
	HAL_WDG_Reboot();
}
