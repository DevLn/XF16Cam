#ifndef XF16CAM_BOARD_H
#define XF16CAM_BOARD_H

int xf16cam_board_init(void);
void xf16cam_board_set_ready(void);
int xf16cam_board_mode_button_pressed(void);
int xf16cam_board_reset_button_pressed(void);

#endif
