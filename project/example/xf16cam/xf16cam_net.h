#ifndef XF16CAM_NET_H
#define XF16CAM_NET_H

#include "xf16cam_config.h"

#define XF16CAM_AP_SSID  "XF16CAM"
#define XF16CAM_AP_PSK   "xf16camera"
#define XF16CAM_AP_IP    "192.168.4.1"
#define XF16CAM_DHCP_IP  "192.168.4.100"

int xf16cam_net_start(const XF16CamConfig *config);
XF16CamWifiMode xf16cam_net_mode(void);
const char *xf16cam_net_ip(void);

#endif
