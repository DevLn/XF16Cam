#include <stdio.h>

#include "kernel/os/os.h"
#include "driver/chip/hal_gpio.h"
#include "driver/chip/hal_prcm.h"
#include "driver/chip/hal_wdg.h"

#include "xf16cam_board.h"
#include "xf16cam_config.h"
#include "xf16cam_storage.h"
#include "xf16cam_ptz.h"

#ifdef NO_PTZ
#define XF16CAM_LED_PIN              GPIO_PIN_21
#define XF16CAM_MODE_BUTTON_PIN      GPIO_PIN_15
#define XF16CAM_RESET_BUTTON_PIN     GPIO_PIN_20
#define XF16CAM_GPIO_PORT            GPIO_PORT_A
#else
#define XF16CAM_LED_PIN              GPIO_PIN_20
#define XF16CAM_IR_LED_PIN           GPIO_PIN_22
#define XF16CAM_MODE_BUTTON_PIN      #error "PTZ version does not have a mode button"
#define XF16CAM_RESET_BUTTON_PIN     GPIO_PIN_19
#define XF16CAM_GPIO_PORT            GPIO_PORT_B

#endif

#define XF16CAM_BUTTON_POLL_MS       (50)
#define XF16CAM_BUTTON_DEBOUNCE_MS   (100)
#define XF16CAM_RESET_HOLD_MS        (3000)
#define XF16CAM_BOARD_STACK_SIZE     (1024)

static OS_Thread_t g_board_thread;
static volatile int g_board_ready;
static volatile int g_board_sleeping;

static int xf16cam_button_pressed(GPIO_Pin pin)
{
	return HAL_GPIO_ReadPin(XF16CAM_GPIO_PORT, pin) == GPIO_PIN_LOW;
}

#ifdef NO_PTZ
int xf16cam_board_mode_button_pressed(void)
{
	return xf16cam_button_pressed(XF16CAM_MODE_BUTTON_PIN);
}
#endif

int xf16cam_board_reset_button_pressed(void)
{
	return xf16cam_button_pressed(XF16CAM_RESET_BUTTON_PIN);
}

static void xf16cam_board_reboot(void)
{
	if (xf16cam_update_begin() != 0) {
		printf("xf16cam reboot deferred: firmware update is active\n");
		return;
	}
	if (xf16cam_storage_unmount() != 0)
		printf("xf16cam board: SD eject failed before reboot\n");
	OS_MSleep(250);
	HAL_PRCM_SetCPUABootFlag(PRCM_CPUA_BOOT_FROM_COLD_RESET);
	HAL_WDG_Reboot();
}

static void xf16cam_board_task(void *arg)
{
	unsigned int mode_held_ms = 0;
	unsigned int reset_held_ms = 0;
	unsigned int blink_ms = 0;
	int mode_handled = 0;
	int reset_handled = 0;
	int led = 0;

	(void)arg;
	while (1) {
		#ifdef NO_PTZ
		int mode_pressed = xf16cam_board_mode_button_pressed();
		#else
		if(g_ptz_ready && g_last_ptz_time != 0 &&
		   OS_TicksToMSecs(OS_GetTicks()) - g_last_ptz_time > PTZ_IDLE_TIMEOUT_MS) {
			g_last_ptz_time = 0;
			xf16cam_ptz_power_down();
		}
		int mode_pressed = 0;
		#endif
		int reset_pressed = xf16cam_board_reset_button_pressed();

		if (g_board_sleeping) {
			if (led) {
				led = 0;
				HAL_GPIO_WritePin(XF16CAM_GPIO_PORT, XF16CAM_LED_PIN, GPIO_PIN_LOW);
			}
			OS_MSleep(XF16CAM_BUTTON_POLL_MS);
			continue;
		}
		if (!g_board_ready) {
			blink_ms += XF16CAM_BUTTON_POLL_MS;
			if (blink_ms >= 250) {
				blink_ms = 0;
				led = !led;
				HAL_GPIO_WritePin(XF16CAM_GPIO_PORT, XF16CAM_LED_PIN,
				                  led ? GPIO_PIN_HIGH : GPIO_PIN_LOW);
			}
		} else if (!led) {
			led = 1;
			#ifdef NO_PTZ
			// non-PTZ version: LED is status LED, so turn it on when ready
			HAL_GPIO_WritePin(XF16CAM_GPIO_PORT, XF16CAM_LED_PIN, GPIO_PIN_HIGH);
			#else
			// PTZ version: LED is luming LED, so turn it off when ready
			HAL_GPIO_WritePin(XF16CAM_GPIO_PORT, XF16CAM_LED_PIN, GPIO_PIN_LOW);
			#endif
		}
		if (!g_board_ready) {
			mode_held_ms = 0;
			reset_held_ms = 0;
			OS_MSleep(XF16CAM_BUTTON_POLL_MS);
			continue;
		}

		if (mode_pressed) {
			mode_held_ms += XF16CAM_BUTTON_POLL_MS;
		} else {
			if (!mode_handled && mode_held_ms >= XF16CAM_BUTTON_DEBOUNCE_MS) {
				XF16CamMediaMode next = xf16cam_config_get()->media_mode == XF16CAM_MEDIA_WEB ?
				                          XF16CAM_MEDIA_RTSP : XF16CAM_MEDIA_WEB;
				printf("xf16cam PA15: switching media mode to %s\n",
				       next == XF16CAM_MEDIA_WEB ? "WEB" : "RTSP");
				if (xf16cam_config_save_media(next) == 0)
					xf16cam_board_reboot();
			}
			mode_held_ms = 0;
			mode_handled = 0;
		}

		if (reset_pressed) {
			reset_held_ms += XF16CAM_BUTTON_POLL_MS;
			if (!reset_handled && reset_held_ms >= XF16CAM_RESET_HOLD_MS) {
				reset_handled = 1;
				printf("xf16cam PA20: restoring setup AP\n");
				if (xf16cam_config_save_ap() == 0)
					xf16cam_board_reboot();
			}
		} else {
			reset_held_ms = 0;
			reset_handled = 0;
		}

		OS_MSleep(XF16CAM_BUTTON_POLL_MS);
	}
}

int xf16cam_board_init(void)
{
	GPIO_InitParam input = {
		.mode = GPIOx_Pn_F0_INPUT,
		.driving = GPIO_DRIVING_LEVEL_1,
		.pull = GPIO_PULL_UP,
	};
	GPIO_InitParam output = {
		.mode = GPIOx_Pn_F1_OUTPUT,
		.driving = GPIO_DRIVING_LEVEL_1,
		.pull = GPIO_PULL_NONE,
	};

	#ifdef NO_PTZ
	HAL_GPIO_Init(XF16CAM_GPIO_PORT, XF16CAM_MODE_BUTTON_PIN, &input);
	#else
	xf16cam_ptz_init();
	#endif
	HAL_GPIO_Init(XF16CAM_GPIO_PORT, XF16CAM_RESET_BUTTON_PIN, &input);
	HAL_GPIO_Init(XF16CAM_GPIO_PORT, XF16CAM_LED_PIN, &output);
	HAL_GPIO_WritePin(XF16CAM_GPIO_PORT, XF16CAM_LED_PIN, GPIO_PIN_LOW);
	printf("xf16cam board: PA15 mode=%s PA20 reset=%s PA21 status LED\n",
		#ifdef NO_PTZ
	       xf16cam_board_mode_button_pressed() ? "pressed" : "released",
		#else
	       "N/A",
		#endif
	       xf16cam_board_reset_button_pressed() ? "pressed" : "released");

	if (OS_ThreadCreate(&g_board_thread, "xf16cam-board", xf16cam_board_task,
	                    NULL, OS_THREAD_PRIO_APP, XF16CAM_BOARD_STACK_SIZE) != OS_OK) {
		printf("xf16cam board thread create failed\n");
		return -1;
	}
	return 0;
}

void xf16cam_board_set_ready(void)
{
	g_board_ready = 1;
}

void xf16cam_board_prepare_sleep(void)
{
	g_board_sleeping = 1;
	g_board_ready = 0;
	HAL_GPIO_WritePin(XF16CAM_GPIO_PORT, XF16CAM_LED_PIN, GPIO_PIN_LOW);
	#ifndef NO_PTZ
	if (g_ptz_ready) {
		xf16cam_ptz_power_down();
	}
	#endif
}

void xf16cam_board_set_led(int on)
{
	HAL_GPIO_WritePin(XF16CAM_GPIO_PORT, XF16CAM_LED_PIN,
	                  on ? GPIO_PIN_HIGH : GPIO_PIN_LOW);
}

int xf16cam_board_get_led_on(void)
{
	return HAL_GPIO_ReadPin(XF16CAM_GPIO_PORT, XF16CAM_LED_PIN) == GPIO_PIN_HIGH;
}

#ifndef NO_PTZ
#endif

//IR LED control functions for PTZ version
void xf16cam_board_set_ir_led(int on)
{
	#ifdef XF16CAM_IR_LED_PIN
	HAL_GPIO_WritePin(GPIO_PORT_A, XF16CAM_IR_LED_PIN,
	                  on ? GPIO_PIN_HIGH : GPIO_PIN_LOW);
	#endif
}

int xf16cam_board_get_ir_led_on(void)
{
	#ifdef XF16CAM_IR_LED_PIN
	return HAL_GPIO_ReadPin(GPIO_PORT_A, XF16CAM_IR_LED_PIN) == GPIO_PIN_HIGH;
	#else
	return 0;
	#endif
}

__xip_text
uint32_t xf16cam_board_stack_min_free(void)
{
	return OS_ThreadGetStackMinFreeSize(&g_board_thread);
}
