#ifndef XF16CAM_POWER_H
#define XF16CAM_POWER_H

#include <stdint.h>

typedef struct {
	uint16_t raw;
	uint16_t millivolts;
	uint8_t valid;
} XF16CamPowerInfo;

int xf16cam_power_measure(void);
const XF16CamPowerInfo *xf16cam_power_info(void);
#ifdef NO_PTZ
void xf16cam_power_hibernate(void);
#endif

#endif
