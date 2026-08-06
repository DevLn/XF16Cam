#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "image/fdcm.h"

#include "xf16cam_config.h"

#define XF16CAM_CONFIG_MAGIC   (0x58464346UL)
#define XF16CAM_CONFIG_SCHEMA  (1U)

static fdcm_handle_t *g_config_store;
static XF16CamConfig g_config;

static uint32_t xf16cam_config_checksum(const XF16CamConfig *config)
{
	const uint8_t *data = (const uint8_t *)config;
	uint32_t hash = 2166136261UL;
	size_t i;

	for (i = 0; i < offsetof(XF16CamConfig, checksum); ++i) {
		hash ^= data[i];
		hash *= 16777619UL;
	}
	return hash;
}

static void xf16cam_config_defaults(XF16CamConfig *config)
{
	memset(config, 0, sizeof(*config));
	config->magic = XF16CAM_CONFIG_MAGIC;
	config->schema = XF16CAM_CONFIG_SCHEMA;
	config->length = sizeof(*config);
	config->wifi_mode = XF16CAM_WIFI_AP;
	config->media_mode = XF16CAM_MEDIA_RTSP;
	config->checksum = xf16cam_config_checksum(config);
}

static int xf16cam_config_valid(const XF16CamConfig *config)
{
	if (config->magic != XF16CAM_CONFIG_MAGIC ||
	    config->schema != XF16CAM_CONFIG_SCHEMA ||
	    config->length != sizeof(*config) ||
	    config->wifi_mode < XF16CAM_WIFI_AP ||
	    config->wifi_mode > XF16CAM_WIFI_STA ||
	    config->media_mode < XF16CAM_MEDIA_RTSP ||
	    config->media_mode > XF16CAM_MEDIA_WEB ||
	    config->ssid[XF16CAM_SSID_MAX_LEN] != '\0' ||
	    config->psk[XF16CAM_PSK_MAX_LEN] != '\0') {
		return 0;
	}
	return config->checksum == xf16cam_config_checksum(config);
}

int xf16cam_config_init(void)
{
	g_config_store = fdcm_open(XF16CAM_CONFIG_FLASH,
	                           XF16CAM_CONFIG_ADDR,
	                           XF16CAM_CONFIG_SIZE);
	if (g_config_store == NULL) {
		printf("xf16cam config: FDCM open failed at 0x%08lx\n",
		       (unsigned long)XF16CAM_CONFIG_ADDR);
		xf16cam_config_defaults(&g_config);
		return -1;
	}

	if (fdcm_read(g_config_store, &g_config, sizeof(g_config)) != sizeof(g_config) ||
	    !xf16cam_config_valid(&g_config)) {
		xf16cam_config_defaults(&g_config);
		printf("xf16cam config: using defaults (AP mode)\n");
		return 0;
	}

	printf("xf16cam config: loaded schema=%u wifi=%s media=%s ssid=%s\n",
	       g_config.schema,
	       g_config.wifi_mode == XF16CAM_WIFI_STA ? "STA" : "AP",
	       g_config.media_mode == XF16CAM_MEDIA_WEB ? "WEB" : "RTSP",
	       g_config.ssid);
	return 0;
}

const XF16CamConfig *xf16cam_config_get(void)
{
	return &g_config;
}

static int xf16cam_config_write(void)
{
	if (g_config_store == NULL)
		return -1;
	g_config.checksum = xf16cam_config_checksum(&g_config);
	return fdcm_write(g_config_store, &g_config, sizeof(g_config)) == sizeof(g_config) ? 0 : -1;
}

int xf16cam_config_save_sta(const char *ssid, const char *psk)
{
	size_t ssid_len = strlen(ssid);
	size_t psk_len = strlen(psk);

	if (ssid_len == 0 || ssid_len > XF16CAM_SSID_MAX_LEN ||
	    psk_len > XF16CAM_PSK_MAX_LEN || (psk_len > 0 && psk_len < 8)) {
		return -1;
	}

	memset(g_config.ssid, 0, sizeof(g_config.ssid));
	memset(g_config.psk, 0, sizeof(g_config.psk));
	memcpy(g_config.ssid, ssid, ssid_len);
	memcpy(g_config.psk, psk, psk_len);
	g_config.wifi_mode = XF16CAM_WIFI_STA;
	return xf16cam_config_write();
}

int xf16cam_config_save_ap(void)
{
	g_config.wifi_mode = XF16CAM_WIFI_AP;
	memset(g_config.ssid, 0, sizeof(g_config.ssid));
	memset(g_config.psk, 0, sizeof(g_config.psk));
	return xf16cam_config_write();
}

int xf16cam_config_save_media(XF16CamMediaMode mode)
{
	if (mode != XF16CAM_MEDIA_RTSP && mode != XF16CAM_MEDIA_WEB)
		return -1;
	g_config.media_mode = mode;
	return xf16cam_config_write();
}
