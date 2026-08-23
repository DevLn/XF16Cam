#include <stdio.h>

#include "kernel/os/os.h"
#include "driver/chip/hal_gpio.h"

#ifndef NO_PTZ

#define MOTOR_GPIO_PORT                GPIO_PORT_B

//Horizontal/pan motor
#define HORIZONTAL_MOTOR_BIT_3 GPIO_PIN_3
#define HORIZONTAL_MOTOR_BIT_2 GPIO_PIN_5
#define HORIZONTAL_MOTOR_BIT_1 GPIO_PIN_2
#define HORIZONTAL_MOTOR_BIT_0 GPIO_PIN_6

#define HORIZONTAL_STEPS 50
#define HORIZONTAL_HOME_STEPS 4096


//Vertical/tilt motor
#define VERTICAL_MOTOR_BIT_3   GPIO_PIN_14
#define VERTICAL_MOTOR_BIT_2   GPIO_PIN_15
#define VERTICAL_MOTOR_BIT_1   GPIO_PIN_4
#define VERTICAL_MOTOR_BIT_0   GPIO_PIN_7

#define VERTICAL_STEPS 35
#define VERTICAL_HOME_STEPS 1350
#define PTZ_STEP_DELAY_MS 2

volatile int g_ptz_ready;
volatile int g_last_ptz_time;

static const GPIO_Pin g_horizontal_motor_pins[4] = {
	HORIZONTAL_MOTOR_BIT_3, HORIZONTAL_MOTOR_BIT_2,
	HORIZONTAL_MOTOR_BIT_1, HORIZONTAL_MOTOR_BIT_0,
};
static const GPIO_Pin g_vertical_motor_pins[4] = {
	VERTICAL_MOTOR_BIT_3, VERTICAL_MOTOR_BIT_2,
	VERTICAL_MOTOR_BIT_1, VERTICAL_MOTOR_BIT_0,
};
static const uint8_t g_ptz_halfstep[8] = {
	0x09, 0x08, 0x0C, 0x04, 0x06, 0x02, 0x03, 0x01,
};
static unsigned int g_horizontal_step;
static unsigned int g_vertical_step;

void xf16cam_ptz_init(void)
{
	unsigned int pin_index;

		GPIO_InitParam output = {
		.mode = GPIOx_Pn_F1_OUTPUT,
		.driving = GPIO_DRIVING_LEVEL_1,
		.pull = GPIO_PULL_NONE,
	};

	for (pin_index = 0; pin_index < 4; ++pin_index) {
		HAL_GPIO_Init(MOTOR_GPIO_PORT, g_horizontal_motor_pins[pin_index], &output);
		HAL_GPIO_Init(MOTOR_GPIO_PORT, g_vertical_motor_pins[pin_index], &output);
		HAL_GPIO_WritePin(MOTOR_GPIO_PORT, g_horizontal_motor_pins[pin_index], GPIO_PIN_LOW);
		HAL_GPIO_WritePin(MOTOR_GPIO_PORT, g_vertical_motor_pins[pin_index], GPIO_PIN_LOW);
	}
	g_horizontal_step = 0;
	g_vertical_step = 0;
	g_ptz_ready = 1;
	g_last_ptz_time = 0;
    printf("xf16cam PTZ: initialized\n");
}

//The stepper motors need to be powered down when the camera is going to sleep,
//otherwise they will draw current, will be hot and drain the battery.
void xf16cam_ptz_power_down(void)
{
	unsigned int pin_index;

	for (pin_index = 0; pin_index < 4; ++pin_index) {
		HAL_GPIO_WritePin(MOTOR_GPIO_PORT, g_horizontal_motor_pins[pin_index], GPIO_PIN_LOW);
		HAL_GPIO_WritePin(MOTOR_GPIO_PORT, g_vertical_motor_pins[pin_index], GPIO_PIN_LOW);
        HAL_GPIO_DeInit(MOTOR_GPIO_PORT, g_horizontal_motor_pins[pin_index]);
        HAL_GPIO_DeInit(MOTOR_GPIO_PORT, g_vertical_motor_pins[pin_index]);
	}
	g_ptz_ready = 0;
	g_last_ptz_time = 0;
    printf("xf16cam PTZ: powered down\n");
}

static void ptz_write_phase(const GPIO_Pin *pins, unsigned int phase)
{
	unsigned int bit;

	for (bit = 0; bit < 4; ++bit)
		HAL_GPIO_WritePin(MOTOR_GPIO_PORT, pins[bit],
		                  (g_ptz_halfstep[phase] & (1U << (3U - bit))) ?
		                  GPIO_PIN_HIGH : GPIO_PIN_LOW);
}

static void ptz_move(const GPIO_Pin *pins, unsigned int *step,
			    unsigned int steps, int direction)
{
    if (!g_ptz_ready) {
        xf16cam_ptz_init();
    }
    g_last_ptz_time = OS_TicksToMSecs(OS_GetTicks());
	printf("xf16cam PTZ: moving %u steps %s\n", steps, direction > 0 ? "forward" : "backward");
	unsigned int step_count;
	for (step_count = 0; step_count < steps; ++step_count) {
		if (direction > 0)
			*step = (*step + 1U) % 8U;
		else
			*step = (*step + 7U) % 8U;
		ptz_write_phase(pins, *step);
		OS_MSleep(PTZ_STEP_DELAY_MS);
	}
	printf("xf16cam PTZ: move complete\n");
}

void ptz_move_left(void)
{
	ptz_move(g_horizontal_motor_pins, &g_horizontal_step, HORIZONTAL_STEPS, -1);
}

void ptz_move_right(void)
{
	ptz_move(g_horizontal_motor_pins, &g_horizontal_step, HORIZONTAL_STEPS, 1);
}

void ptz_move_up(void)
{
	ptz_move(g_vertical_motor_pins, &g_vertical_step, VERTICAL_STEPS, 1);
}

void ptz_move_down(void)
{
	ptz_move(g_vertical_motor_pins, &g_vertical_step, VERTICAL_STEPS, -1);
}

void ptz_move_home(void)
{
	if (!g_ptz_ready) {
		xf16cam_ptz_init();
	}
	g_last_ptz_time = OS_TicksToMSecs(OS_GetTicks());
	printf("xf16cam PTZ: homing to mechanical stops\n");
	ptz_move(g_horizontal_motor_pins, &g_horizontal_step,
	         HORIZONTAL_HOME_STEPS, -1);
	ptz_move(g_vertical_motor_pins, &g_vertical_step,
	         VERTICAL_HOME_STEPS, -1);

	/* Release both end stops by one normal movement increment. */
	ptz_move(g_horizontal_motor_pins, &g_horizontal_step,
	         HORIZONTAL_STEPS, 1);
	ptz_move(g_vertical_motor_pins, &g_vertical_step,
	         VERTICAL_STEPS, 1);
	g_horizontal_step = 0;
	g_vertical_step = 0;
	printf("xf16cam PTZ: homing complete\n");
}

#endif