#ifndef XF16CAM_CONFIG_H
#define XF16CAM_CONFIG_H

#include <stdint.h>

#define XF16CAM_SSID_MAX_LEN  (32)
#define XF16CAM_PSK_MAX_LEN   (63)
/* MQTT broker settings (schema 3). The host is an IP literal today because
 * lwIP DNS is linked out (prj_config.h); the field is wide enough for a short
 * hostname should DNS ever be switched back on. */
#define XF16CAM_MQTT_HOST_MAX_LEN  (31)
#define XF16CAM_MQTT_USER_MAX_LEN  (31)
#define XF16CAM_MQTT_PASS_MAX_LEN  (31)
#define XF16CAM_MQTT_DEFAULT_PORT  (1883U)
#define XF16CAM_MQTT_DEFAULT_INTERVAL_S (10U)

typedef enum {
	XF16CAM_WIFI_AP = 1,
	XF16CAM_WIFI_STA = 2,
} XF16CamWifiMode;

typedef enum {
	XF16CAM_MEDIA_RTSP = 1,
	XF16CAM_MEDIA_WEB = 2,
} XF16CamMediaMode;

typedef enum {
	XF16CAM_RESOLUTION_QVGA = 0,
	XF16CAM_RESOLUTION_VGA = 1,
} XF16CamResolution;

typedef struct {
	uint32_t magic;
	uint16_t schema;
	uint16_t length;
	uint8_t wifi_mode;
	uint8_t media_mode;
	uint8_t resolution;
	uint8_t reserved;
	char ssid[XF16CAM_SSID_MAX_LEN + 1];
	char psk[XF16CAM_PSK_MAX_LEN + 1];
	/* Schema 3 additions. Every field before this line is byte-identical to
	 * the 116-byte schema 1/2 record, which is what lets the loader migrate
	 * an old record in place. Keep new fields before checksum so the FNV-1a
	 * span covers them. The fields exist on every build variant so an MQTT
	 * and a non-MQTT image share one settings record. */
	uint8_t mqtt_interval_s;	/* image publish period; 0 = default */
	uint16_t mqtt_port;		/* 0 = default 1883 */
	char mqtt_host[XF16CAM_MQTT_HOST_MAX_LEN + 1];	/* empty = MQTT off */
	char mqtt_user[XF16CAM_MQTT_USER_MAX_LEN + 1];
	char mqtt_pass[XF16CAM_MQTT_PASS_MAX_LEN + 1];
	uint8_t mqtt_reserved[8];
	uint32_t checksum;
} XF16CamConfig;

int xf16cam_config_init(void);
const XF16CamConfig *xf16cam_config_get(void);
int xf16cam_config_save_sta(const char *ssid, const char *psk);
int xf16cam_config_save_ap(void);
int xf16cam_config_save_media(XF16CamMediaMode mode);
int xf16cam_config_save_resolution(XF16CamResolution resolution);
/* Any string may be empty; an empty host disables MQTT. port 0 and
 * interval_s 0 store the defaults. */
int xf16cam_config_save_mqtt(const char *host, uint16_t port, const char *user,
                             const char *pass, uint8_t interval_s);

static inline uint16_t xf16cam_config_mqtt_port(const XF16CamConfig *config)
{
	return config->mqtt_port != 0 ? config->mqtt_port : XF16CAM_MQTT_DEFAULT_PORT;
}

static inline uint8_t xf16cam_config_mqtt_interval(const XF16CamConfig *config)
{
	return config->mqtt_interval_s != 0 ? config->mqtt_interval_s :
	                                      XF16CAM_MQTT_DEFAULT_INTERVAL_S;
}
int xf16cam_update_begin(void);
void xf16cam_update_end(void);
int xf16cam_update_active(void);

#endif
