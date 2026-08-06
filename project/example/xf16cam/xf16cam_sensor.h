#ifndef XF16CAM_SENSOR_H
#define XF16CAM_SENSOR_H

#include <stdint.h>

#include "driver/component/csi_camera/camera_sensor.h"

HAL_Status xf16cam_sensor_init(SENSOR_ConfigParam *cfg);
void xf16cam_sensor_deinit(SENSOR_ConfigParam *cfg);
HAL_Status xf16cam_sensor_ioctl(SENSOR_IoctrlCmd attr, uint32_t arg);
int xf16cam_sensor_configure_camera(uint16_t configured_width,
				    uint16_t configured_height);
const char *xf16cam_sensor_name(void);
uint16_t xf16cam_sensor_width(void);
uint16_t xf16cam_sensor_height(void);

#endif
