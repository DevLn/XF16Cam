#ifndef XF16CAM_PTZ_H
#define XF16CAM_PTZ_H
void xf16cam_ptz_init(void);
void xf16cam_ptz_power_down(void);
void ptz_move_left(void);
void ptz_move_right(void);
void ptz_move_up(void);
void ptz_move_down(void);
void ptz_move_home(void);

extern volatile int g_last_ptz_time;
extern volatile int g_ptz_ready;

#define PTZ_IDLE_TIMEOUT_MS 10000

#endif