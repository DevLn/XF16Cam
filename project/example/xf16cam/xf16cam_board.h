#ifndef XF16CAM_BOARD_H
#define XF16CAM_BOARD_H

#include <stdint.h>

int xf16cam_board_init(void);
void xf16cam_board_set_ready(void);
void xf16cam_board_prepare_sleep(void);
#ifdef NO_PTZ
int xf16cam_board_mode_button_pressed(void);
#endif
/* Compiled on both variants; no-op and always 0 without an IR LED pin. */
void xf16cam_board_set_ir_led(int on);
int xf16cam_board_get_ir_led_on(void);
void xf16cam_board_set_led(int on);
int xf16cam_board_get_led_on(void);

int xf16cam_board_reset_button_pressed(void);
uint32_t xf16cam_board_stack_min_free(void);
#endif
