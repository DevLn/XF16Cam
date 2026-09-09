#include <stdio.h>
#include <string.h>

#include "compiler.h"
#include "kernel/os/os.h"
#include "common/framework/net_ctrl.h"
#include "common/framework/sysinfo.h"
#include "lwip/inet.h"
#include "lwip/netifapi.h"
#include "net/ethernetif/ethernetif.h"
#include "net/udhcp/usr_dhcpd.h"
#include "net/wlan/wlan.h"

#include "xf16cam_media.h"
#include "xf16cam_net.h"

#define XF16CAM_STA_TIMEOUT_MS  (20000U)

static XF16CamWifiMode g_active_mode = XF16CAM_WIFI_AP;
/* "XF16CAM-" plus six hex digits and the terminator. ethernetif copies this
 * into its own 32-byte buffer, so it can never be truncated on the way. */
static char g_hostname[16];

/*
 * Derive the DHCP hostname from the Wi-Fi MAC. That address is factory
 * programmed in eFuse (PRJCONF_MAC_ADDR_SOURCE), so the name is unique per
 * board, identical across reboots, and untouched by OTA or a full reflash --
 * one firmware image still gives every camera its own name, with no
 * provisioning step. lwIP sends it as DHCP option 12 while requesting the STA
 * lease; routers that publish DHCP names will then resolve it. In AP mode the
 * camera is the DHCP server, so the name is unused there.
 */
__xip_text
static void xf16cam_net_init_hostname(void)
{
	__xip_rodata static const char fmt[] = "XF16CAM-%02X%02X%02X";
	const struct sysinfo *info = sysinfo_get();

	if (info == NULL)
		return;
	snprintf(g_hostname, sizeof(g_hostname), fmt,
	         info->mac_addr[3], info->mac_addr[4], info->mac_addr[5]);
	ethernetif_set_hostname(g_hostname);
}

__xip_text
static int xf16cam_net_wait(uint32_t timeout_ms)
{
	uint32_t elapsed = 0;

	while (!(g_wlan_netif && NETIF_IS_AVAILABLE(g_wlan_netif))) {
		if (elapsed >= timeout_ms)
			return -1;
		OS_MSleep(100);
		elapsed += 100;
	}
	return 0;
}

__xip_text
static int xf16cam_net_start_ap(void)
{
	ip_addr_t ip;
	ip_addr_t netmask;
	ip_addr_t gateway;
	struct dhcp_server_info dhcp = { 0 };

	printf("xf16cam Wi-Fi: starting open AP ssid=%s ip=%s\n",
	       XF16CAM_AP_SSID, XF16CAM_AP_IP);
	if (net_switch_mode(WLAN_MODE_HOSTAP) != 0) {
		printf("xf16cam Wi-Fi: AP mode switch failed\n");
		return -1;
	}
	/* A freshly created interface can already be disabled. Configuration and
	 * enable failures are authoritative; disable is intentionally best-effort. */
	(void)wlan_ap_disable();
	if (wlan_ap_set((uint8_t *)XF16CAM_AP_SSID, strlen(XF16CAM_AP_SSID),
	                NULL) != 0 ||
	    wlan_ap_enable() != 0) {
		printf("xf16cam Wi-Fi: AP configuration failed\n");
		return -1;
	}
	if (xf16cam_net_wait(5000) != 0)
		return -1;

	inet_aton(XF16CAM_AP_IP, &ip);
	inet_aton("255.255.255.0", &netmask);
	inet_aton(XF16CAM_AP_IP, &gateway);
	dhcp_server_stop();
	if (netifapi_netif_set_addr(g_wlan_netif, &ip, &netmask, &gateway) != ERR_OK) {
		printf("xf16cam Wi-Fi: AP address setup failed\n");
		return -1;
	}

	dhcp.addr_start = inet_addr(XF16CAM_DHCP_IP);
	dhcp.addr_end = inet_addr(XF16CAM_DHCP_IP);
	dhcp.lease_time = 60 * 60;
	dhcp.max_leases = 1;
	dhcp_server_start(&dhcp);
	g_active_mode = XF16CAM_WIFI_AP;
	printf("xf16cam Wi-Fi ready: mode=AP ip=%s lease=%s\n",
	       ipaddr_ntoa(&g_wlan_netif->ip_addr), XF16CAM_DHCP_IP);
	return 0;
}

__xip_text
static int xf16cam_net_start_sta(const XF16CamConfig *config)
{
	printf("xf16cam Wi-Fi: starting STA ssid=%s\n", config->ssid);
	if (net_switch_mode(WLAN_MODE_STA) != 0) {
		printf("xf16cam Wi-Fi: STA mode switch failed\n");
		return -1;
	}
	(void)wlan_sta_disable();
	if (wlan_sta_set((uint8_t *)config->ssid, strlen(config->ssid),
	                 (uint8_t *)config->psk) != 0 ||
	    wlan_sta_enable() != 0) {
		printf("xf16cam Wi-Fi: STA configuration failed\n");
		return -1;
	}
	if (xf16cam_net_wait(XF16CAM_STA_TIMEOUT_MS) != 0) {
		printf("xf16cam Wi-Fi: STA timeout, falling back to setup AP\n");
		return -1;
	}
	g_active_mode = XF16CAM_WIFI_STA;
	printf("xf16cam Wi-Fi ready: mode=STA ip=%s\n",
	       ipaddr_ntoa(&g_wlan_netif->ip_addr));
	return 0;
}

__xip_text
int xf16cam_net_start(const XF16CamConfig *config)
{
	/* Must precede net_switch_mode(): DHCP is started asynchronously by
	 * net_config() on the link-up event, and option 12 is only emitted for a
	 * non-empty hostname. */
	xf16cam_net_init_hostname();
	if (config->wifi_mode == XF16CAM_WIFI_STA && config->ssid[0] != '\0' &&
	    xf16cam_net_start_sta(config) == 0) {
		return 0;
	}
	return xf16cam_net_start_ap();
}

XF16CamWifiMode xf16cam_net_mode(void)
{
	return g_active_mode;
}

const char *xf16cam_net_ip(void)
{
	return g_wlan_netif ? ipaddr_ntoa(&g_wlan_netif->ip_addr) : "0.0.0.0";
}

const char *xf16cam_net_hostname(void)
{
	return g_hostname;
}
