#ifndef XF16CAM_STORAGE_H
#define XF16CAM_STORAGE_H

#include <stdint.h>

typedef struct {
	int mounted;
	uint32_t total_mb;
	uint32_t free_mb;
} XF16CamStorageInfo;

int xf16cam_storage_init(void);
int xf16cam_storage_refresh(void);
int xf16cam_storage_format(void);
const XF16CamStorageInfo *xf16cam_storage_info(void);

#endif
