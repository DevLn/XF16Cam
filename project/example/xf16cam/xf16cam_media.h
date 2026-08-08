#ifndef XF16CAM_MEDIA_H
#define XF16CAM_MEDIA_H

#include <stdint.h>

int xf16cam_mjpeg_start(int fd);
int xf16cam_media_client_connected(int fd);
int xf16cam_media_quiesce_for_update(uint32_t timeout_ms);

#endif
