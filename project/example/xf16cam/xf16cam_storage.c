#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "compiler.h"
#include "common/framework/fs_ctrl.h"
#include "driver/chip/sdmmc/sdmmc.h"
#include "fs/fatfs/ff.h"
#include "sys/xr_debug.h"

#include "xf16cam_storage.h"

#define XF16CAM_MKFS_WORK_SIZE (10 * 1024)

static XF16CamStorageInfo g_storage;

__xip_text
static int xf16cam_storage_read_capacity(void)
{
	DWORD free_clusters;
	FATFS *fs;
	uint32_t sector_size;
	uint64_t total_bytes;
	uint64_t free_bytes;

	if (f_getfree("0:/", &free_clusters, &fs) != FR_OK)
		return -1;
#if (_MAX_SS == _MIN_SS)
	sector_size = _MAX_SS;
#else
	sector_size = fs->ssize;
#endif
	total_bytes = (uint64_t)(fs->n_fatent - 2) * fs->csize * sector_size;
	free_bytes = (uint64_t)free_clusters * fs->csize * sector_size;
	g_storage.total_mb = (uint32_t)(total_bytes / (1024 * 1024));
	g_storage.free_mb = (uint32_t)(free_bytes / (1024 * 1024));
	return 0;
}

__xip_text
int xf16cam_storage_refresh(void)
{
	if (!g_storage.mounted) {
		if (fs_ctrl_mount(FS_MNT_DEV_TYPE_SDCARD, 0) != 0) {
			g_storage.total_mb = 0;
			g_storage.free_mb = 0;
			return -1;
		}
		g_storage.mounted = 1;
	}
	if (xf16cam_storage_read_capacity() != 0) {
		fs_ctrl_unmount(FS_MNT_DEV_TYPE_SDCARD, 0);
		g_storage.mounted = 0;
		g_storage.total_mb = 0;
		g_storage.free_mb = 0;
		return -1;
	}
	printf("xf16cam SD: mounted total=%lu MiB free=%lu MiB\n",
	       (unsigned long)g_storage.total_mb, (unsigned long)g_storage.free_mb);
	return 0;
}

__xip_text
int xf16cam_storage_init(void)
{
	memset(&g_storage, 0, sizeof(g_storage));
	printf("xf16cam SD: idle; use Check card to probe\n");
	return 0;
}

__xip_text
int xf16cam_storage_format(void)
{
	uint8_t *work;
	FRESULT result;
	int temporary_card = 0;

	if (!g_storage.mounted) {
		SDCard_InitTypeDef card_param = { 0 };
		struct mmc_card *card;

		card_param.debug_mask = ROM_WRN_MASK | ROM_ERR_MASK | ROM_ANY_MASK;
		card_param.type = MMC_TYPE_SD;
		if (mmc_card_create(0, &card_param) != 0)
			return -1;
		card = mmc_card_open(0);
		if (card == NULL || (!mmc_card_present(card) && mmc_rescan(card, 0) != 0)) {
			if (card != NULL)
				mmc_card_close(0);
			mmc_card_delete(0, 0);
			return -1;
		}
		mmc_card_close(0);
		temporary_card = 1;
	}
	work = malloc(XF16CAM_MKFS_WORK_SIZE);
	if (work == NULL) {
		if (temporary_card)
			mmc_card_delete(0, 0);
		return -1;
	}
	result = f_mkfs("0:/", FM_FAT32, 4 * 1024, work, XF16CAM_MKFS_WORK_SIZE);
	free(work);
	if (temporary_card)
		mmc_card_delete(0, 0);
	if (result != FR_OK) {
		printf("xf16cam SD: format failed (%d)\n", result);
		return -1;
	}
	printf("xf16cam SD: FAT32 format complete\n");
	return xf16cam_storage_refresh();
}

const XF16CamStorageInfo *xf16cam_storage_info(void)
{
	return &g_storage;
}
