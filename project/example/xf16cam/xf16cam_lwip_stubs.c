/*
 * Link-time stubs that keep lwIP's DNS client and IGMP out of the image.
 *
 * The SDK's lwipopts.h compiles both features in, and the camera needs
 * neither: it only serves connections, never resolves a hostname, and never
 * joins a multicast group. Rather than editing the SDK's lwIP configuration,
 * this file defines every symbol the rest of the image imports from dns.o and
 * igmp.o. The app's objects are linked before the libraries, so the linker
 * resolves those imports here and never extracts the two archive members --
 * their code, strings and static buffers all drop out (about 2.7 KB of XIP
 * and 1.4 KB of RAM). The SDK tree stays untouched and can be updated freely.
 *
 * To bring a feature back, set XF16CAM_LWIP_DNS_EN or XF16CAM_LWIP_IGMP_EN to
 * 1 in prj_config.h. That is the whole change: lwIP is already compiled with
 * both on, so no library rebuild or SDK configuration is involved. DNS is
 * needed to reach a broker or NTP server by hostname; IGMP is needed to be
 * found by multicast discovery (ONVIF WS-Discovery, mDNS) or to receive a
 * multicast stream.
 *
 * Failure modes are loud. If a future lwIP imports a symbol that is not
 * stubbed here, the linker extracts the member and reports duplicate
 * definitions of the ones that are. If the symbols are renamed so that the
 * stubs match nothing, the member would link silently instead -- which is
 * why CI asserts that dns_table and igmp_group_list are absent from the
 * image. A changed prototype is a compile error against the lwIP headers.
 *
 * Behaviour matches lwIP built with the feature off: multicast is dropped
 * before protocol dispatch, group joins fail, and resolution fails -- except
 * that dotted-decimal literals still resolve, because MQTT-style clients
 * commonly pass every address through gethostbyname().
 */

#include "prj_config.h"

/* lwIP 1.4.1's dns.h and igmp.h rely on the includer for these types. */
#include "lwip/opt.h"
#include "lwip/err.h"
#include "lwip/ip_addr.h"
#include "lwip/netif.h"
#include "lwip/pbuf.h"
#include "lwip/dns.h"
#include "lwip/igmp.h"

#include "compiler.h"

#if !XF16CAM_LWIP_DNS_EN && LWIP_DNS

__xip_text
void dns_init(void)
{
}

__xip_text
void dns_tmr(void)
{
}

__xip_text
void dns_setserver(u8_t numdns, ip_addr_t *dnsserver)
{
	(void)numdns;
	(void)dnsserver;
}

__xip_text
err_t dns_gethostbyname(const char *hostname, ip_addr_t *addr,
                        dns_found_callback found, void *callback_arg)
{
	(void)found;
	(void)callback_arg;
	if (hostname == NULL || addr == NULL)
		return ERR_ARG;
	return ipaddr_aton(hostname, addr) ? ERR_OK : ERR_ARG;
}

#endif /* !XF16CAM_LWIP_DNS_EN && LWIP_DNS */

#if !XF16CAM_LWIP_IGMP_EN && LWIP_IGMP

__xip_text
void igmp_init(void)
{
}

__xip_text
err_t igmp_start(struct netif *netif)
{
	(void)netif;
	return ERR_OK;
}

__xip_text
err_t igmp_stop(struct netif *netif)
{
	(void)netif;
	return ERR_OK;
}

__xip_text
void igmp_report_groups(struct netif *netif)
{
	(void)netif;
}

__xip_text
struct igmp_group *igmp_lookfor_group(struct netif *ifp, ip_addr_t *addr)
{
	(void)ifp;
	(void)addr;
	return NULL;
}

/* ip_input() hands the packet over; the real handler frees it, so must this. */
__xip_text
void igmp_input(struct pbuf *p, struct netif *inp, ip_addr_t *dest)
{
	(void)inp;
	(void)dest;
	pbuf_free(p);
}

__xip_text
err_t igmp_joingroup(ip_addr_t *ifaddr, ip_addr_t *groupaddr)
{
	(void)ifaddr;
	(void)groupaddr;
	return ERR_VAL;
}

__xip_text
err_t igmp_leavegroup(ip_addr_t *ifaddr, ip_addr_t *groupaddr)
{
	(void)ifaddr;
	(void)groupaddr;
	return ERR_VAL;
}

__xip_text
void igmp_tmr(void)
{
}

#endif /* !XF16CAM_LWIP_IGMP_EN && LWIP_IGMP */
