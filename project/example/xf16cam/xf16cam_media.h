#ifndef XF16CAM_MEDIA_H
#define XF16CAM_MEDIA_H

#include <stdint.h>

typedef enum {
	XF16CAM_MEDIA_CLIENT_NONE,
	XF16CAM_MEDIA_CLIENT_WEB,
	XF16CAM_MEDIA_CLIENT_RTSP,
} XF16CamMediaClient;

typedef struct {
	uint32_t frames_captured;
	uint32_t frames_delivered;
	uint32_t frame_errors;
	XF16CamMediaClient active_client;
} XF16CamMediaStats;

int xf16cam_mjpeg_start(int fd);
int xf16cam_media_quiesce_for_update(uint32_t timeout_ms);
void xf16cam_media_stats(XF16CamMediaStats *stats);

#endif
