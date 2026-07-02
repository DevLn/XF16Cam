/*
 * Copyright (C) 2017 XRADIO TECHNOLOGY CO., LTD. All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *    1. Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *    2. Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the
 *       distribution.
 *    3. Neither the name of XRADIO TECHNOLOGY CO., LTD. nor the names of
 *       its contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *  A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *  OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *  LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *  THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifdef __CONFIG_WLAN

#include "net/wlan/wlan.h"
#include "net/wlan/wlan_ext_req.h"
#include "lwip/sockets.h"
#include "sys/xr_util.h"

#define WLAN_USER_DBG_ON     0
#define WLAN_USER_WRN_ON     1
#define WLAN_USER_ERR_ON     1
#define WLAN_USER_ABORT_ON   0

#define WLAN_USER_SYSLOG     printf
#define WLAN_USER_ABORT()    sys_abort()

#define WLAN_USER_LOG(flags, fmt, arg...)    \
	do {                                     \
		if (flags)                           \
			WLAN_USER_SYSLOG(fmt, ##arg);    \
	} while (0)

#define WLAN_USER_DBG(fmt, arg...) WLAN_USER_LOG(WLAN_USER_DBG_ON, "[wlan user D] "fmt, ##arg)
#define WLAN_USER_WRN(fmt, arg...) WLAN_USER_LOG(WLAN_USER_WRN_ON, "[wlan user W] "fmt, ##arg)
#define WLAN_USER_ERR(fmt, arg...)                               \
	do {                                                    \
		WLAN_USER_LOG(WLAN_USER_ERR_ON, "[wlan user E] %s():%d, "fmt, \
		         __func__, __LINE__, ##arg);                          \
		if (WLAN_USER_ABORT_ON)                                       \
			WLAN_USER_ABORT();                                        \
	} while (0)

/* enable for beacon lost compensate in STA mode,
 * we recomendate use it with macro WLAN_USER_DTIM_TAKE_EFFECT_IMMEDIATELY enable */
#define WLAN_USER_ENABLE_BEACON_LOST_COMP       0
#define WLAN_USER_DTIM_TAKE_EFFECT_IMMEDIATELY  0

#define WLAN_USER_ENABLE_PM_RX_BCN_11B_ONLY     0 /* enable for support recv beacon use 11b only after app standby */

#define WLAN_USER_SUPPORT_HIDE_AP_SSID          1 /* enable for support hide ssid config to wlan fw */

int wlan_ext_low_power_param_set_default(uint32_t dtim)
{
	int ret;
	int bcn_win;
	uint32_t dtim_period;
	uint32_t listen_interval;
	int null_period;
	wlan_ext_ps_cfg_t ps_cfg;
	wlan_ext_bcn_win_param_set_t bcn_win_param;
	wlan_ext_pre_rx_bcn_t pre_rx_bcn;

	bcn_win = 2300;
	ret = wlan_ext_request(wlan_netif_get(WLAN_MODE_NONE),
	                       WLAN_EXT_CMD_SET_BCN_WIN_US, bcn_win);
	if (ret) {
		WLAN_USER_ERR("fail to set bcn_win!\n");
		return ret;
	}

#if WLAN_USER_DTIM_TAKE_EFFECT_IMMEDIATELY
	dtim_period = dtim;
	listen_interval = 0;
#else
	dtim_period = 1;
	listen_interval = dtim;
#endif

	ret = wlan_ext_request(wlan_netif_get(WLAN_MODE_NONE),
	                       WLAN_EXT_CMD_SET_PM_DTIM, dtim_period);
	if (ret) {
		WLAN_USER_ERR("fail to set dtim_period!\n");
		return ret;
	}
	ret = wlan_ext_request(wlan_netif_get(WLAN_MODE_NONE),
	                       WLAN_EXT_CMD_SET_LISTEN_INTERVAL, listen_interval);
	if (ret) {
		WLAN_USER_ERR("fail to set listen_interval!\n");
		return ret;
	}

#if 0 /* TODO: support it in FW */
	int bcn_freq_offs_time;
	bcn_freq_offs_time = 40 * listen_interval;
	ret = wlan_ext_request(wlan_netif_get(WLAN_MODE_NONE),
	                       WLAN_EXT_CMD_SET_BCN_FREQ_OFFS_TIME, bcn_freq_offs_time);
	if (ret) {
		WLAN_USER_ERR("fail to set bcn_freq_offs_time!\n");
		return ret;
	}
#endif

	null_period = 48;
	ret = wlan_ext_request(wlan_netif_get(WLAN_MODE_NONE),
	                       WLAN_EXT_CMD_SET_PM_TX_NULL_PERIOD, null_period);
	if (ret) {
		WLAN_USER_ERR("fail to set null_period!\n");
		return ret;
	}

	ps_cfg.ps_mode = 1;
	ps_cfg.ps_idle_period = 10;
	ps_cfg.ps_change_period = 10;
	ret = wlan_ext_request(wlan_netif_get(WLAN_MODE_NONE),
	                       WLAN_EXT_CMD_SET_PS_CFG, (uint32_t)&ps_cfg);
	if (ret) {
		WLAN_USER_ERR("fail to set ps_cfg!\n");
		return ret;
	}

	/* As rx beacon use 11b function will affect the normal TX in active state,
	 * so this function cfg only effect after app enter pm standby,
	 * and reverted in app resume.
	 */
#if WLAN_USER_ENABLE_PM_RX_BCN_11B_ONLY
	uint32_t bcn_rx_11b_only_en = 1;
	ret = wlan_ext_request(wlan_netif_get(WLAN_MODE_NONE),
	                       WLAN_EXT_CMD_SET_BCN_RX_11B_ONLY, (uint32_t)bcn_rx_11b_only_en);
	if (ret) {
		WLAN_USER_ERR("fail to set bcn_rx_11b_only_en!\n");
		return ret;
	}
#endif

	bcn_win_param.BeaconWindowAdjStartNum = 1;
	bcn_win_param.BeaconWindowAdjStopNum = 5;
	bcn_win_param.BeaconWindowAdjAmpUs = 1000;
	bcn_win_param.BeaconWindowMaxStartNum = 5;
	ret = wlan_ext_request(wlan_netif_get(WLAN_MODE_NONE),
	                       WLAN_EXT_CMD_SET_BCN_WIN_CFG, (uint32_t)(&bcn_win_param));
	if (ret) {
		WLAN_USER_ERR("fail to set bcn_win_param!\n");
		return ret;
	}

	pre_rx_bcn.enable = 1;
	pre_rx_bcn.flags = 0;
	pre_rx_bcn.stop_num = 0;
	ret = wlan_ext_request(wlan_netif_get(WLAN_MODE_NONE),
	                       WLAN_EXT_CMD_SET_PRE_RX_BCN, (uint32_t)&pre_rx_bcn);
	if (ret) {
		WLAN_USER_ERR("fail to set pre_rx_bcn!\n");
		return ret;
	}

#if WLAN_USER_ENABLE_BEACON_LOST_COMP
	wlan_ext_bcn_lost_comp_set_t bcn_lost_comp;
	bcn_lost_comp.Enable = 1;
	bcn_lost_comp.DtimLostNum = 2;
	bcn_lost_comp.CompInterval = 1;
	bcn_lost_comp.CompCnt = 2;
	ret = wlan_ext_request(wlan_netif_get(WLAN_MODE_NONE),
	                       WLAN_EXT_CMD_SET_BCN_LOST_COMP, (uint32_t)(&bcn_lost_comp));
	if (ret) {
		WLAN_USER_ERR("fail to set bcn lost comp!\n");
		return ret;
	}
#endif

	return ret;
}

int wlan_ext_p2p_keepalive_default(struct netif *nif, wlan_ext_p2p_keepalive_param_t *p2p_param)
{
	int i, ret;
	uint8_t ser_num;
	int wk_enable_cnt = 0;
	int kpalive_enable_cnt = 0;
	struct sockaddr_in local_addr;
	socklen_t socklen = sizeof(struct sockaddr);
	wlan_ext_p2p_svr_set_t svr;
	wlan_ext_p2p_kpalive_param_set_t kpalive;
	wlan_ext_p2p_wkp_param_set_t wkp_param;

	memset(&svr, 0, sizeof(wlan_ext_p2p_svr_set_t));
	memset(&kpalive, 0, sizeof(wlan_ext_p2p_kpalive_param_set_t));
	memset(&wkp_param, 0, sizeof(wlan_ext_p2p_wkp_param_set_t));

	for (ser_num = 0; ser_num < IPC_P2P_SERVER_MAX; ser_num++) {
		if (p2p_param->Socket[ser_num].Enable) {
			if (p2p_param->Socket[ser_num].KeepAliveCfg.PayloadLen > IPC_P2P_KPALIVE_PAYLOAD_LEN_MAX) {
				WLAN_USER_ERR("invalid server[%d] KeepAlive payload len %d, should be 1 to %d\n",
				    ser_num, p2p_param->Socket[ser_num].KeepAliveCfg.PayloadLen, IPC_P2P_KPALIVE_PAYLOAD_LEN_MAX);
				return -1;
			}
			if (p2p_param->Socket[ser_num].WakeupPkt.PayloadLen > IPC_P2P_WUP_PAYLOAD_LEN_MAX) {
				WLAN_USER_ERR("invalid server[%d] WakeupPkt payload len %d, should be 1 to %d\n",
				    ser_num, p2p_param->Socket[ser_num].WakeupPkt.PayloadLen, IPC_P2P_WUP_PAYLOAD_LEN_MAX);
				return -1;
			}

#if LWIP_IPV4 && LWIP_IPV6
			memcpy(svr.SrcIPv4Addr, &(nif->ip_addr.u_addr), 4);
#else
			memcpy(svr.SrcIPv4Addr, &(nif->ip_addr), 4);
#endif
			getsockname(p2p_param->Socket[ser_num].fd, (struct sockaddr *)&local_addr, &socklen);
			svr.P2PServerCfgs[ser_num].DstPort = p2p_param->Socket[ser_num].Port;
			svr.P2PServerCfgs[ser_num].SrcPort = ntohs(local_addr.sin_port);
			for (i = 0; i < 4; i++) {
				svr.P2PServerCfgs[ser_num].DstIPv4Addr[i] = p2p_param->Socket[ser_num].DstIPv4Addr[i];
			}

			for (i = 0; i < 6; i++) {
				svr.P2PServerCfgs[ser_num].DstMacAddr[i] = p2p_param->Socket[ser_num].DstMacAddr[i];
			}

			svr.P2PServerCfgs[ser_num].TcpSeqInit = getsockack(p2p_param->Socket[ser_num].fd);
			svr.P2PServerCfgs[ser_num].TcpAckInit = getsockseq(p2p_param->Socket[ser_num].fd);
			svr.P2PServerCfgs[ser_num].IPIdInit = 1;
			svr.P2PServerCfgs[ser_num].Enable = p2p_param->Socket[ser_num].Enable;
			svr.P2PServerCfgs[ser_num].TcpOrUdp = p2p_param->Socket[ser_num].udp_flag ? 0x02 : 0x01;
		}
	}
	ret = wlan_ext_request(wlan_netif_get(WLAN_MODE_NONE), WLAN_EXT_CMD_SET_P2P_SVR, (int)(&svr));
	if (ret) {
		WLAN_USER_ERR("fail to set P2P Server!\n");
		return ret;
	}

	for (i = 0; i < IPC_P2P_SERVER_MAX; i++) {
		if (p2p_param->Socket[i].KeepAliveCfg.Enable) {
			kpalive_enable_cnt++;
			kpalive.P2PKeepAliveParamCfgs[i].Enable = 1;
			kpalive.P2PKeepAliveParamCfgs[i].PayloadLen = p2p_param->Socket[i].KeepAliveCfg.PayloadLen;
			memcpy(kpalive.P2PKeepAliveParamCfgs[i].Payload,
			    p2p_param->Socket[i].KeepAliveCfg.Payload, kpalive.P2PKeepAliveParamCfgs[i].PayloadLen);
		}
	}
	if (kpalive_enable_cnt) {
		kpalive.KeepAlivePeriod_s = p2p_param->KeepAlive.Period;
		kpalive.TxTimeOut_s = p2p_param->KeepAlive.TxTimeOut;
		kpalive.TxRetryLimit = p2p_param->KeepAlive.TxRetryLimit;
	}
	ret = wlan_ext_request(wlan_netif_get(WLAN_MODE_NONE), WLAN_EXT_CMD_SET_P2P_KPALIVE_CFG, (int)(&kpalive));
	if (ret) {
		WLAN_USER_ERR("fail to set P2P keep alive!\n");
		return ret;
	}

	for (i = 0; i < IPC_P2P_SERVER_MAX; i++) {
		if (p2p_param->Socket[i].WakeupPkt.Enable || p2p_param->Socket[i].WakeupIp.Enable)
			wk_enable_cnt++;
		wkp_param.P2PWkpParamCfgs[i].Enable = p2p_param->Socket[i].WakeupPkt.Enable;

		if (p2p_param->Socket[i].WakeupPkt.Enable) {
			wkp_param.P2PWkpParamCfgs[i].PayloadLen = p2p_param->Socket[i].WakeupPkt.PayloadLen;
			memcpy(wkp_param.P2PWkpParamCfgs[i].Payload,
			    p2p_param->Socket[i].WakeupPkt.Payload, p2p_param->Socket[i].WakeupPkt.PayloadLen);
		}
		if (p2p_param->Socket[i].WakeupIp.Enable) {
			memcpy(wkp_param.P2PIpv4FilterCfgs[i].Ipv4Filter, p2p_param->Socket[i].WakeupIp.Ipv4Filter, 4);
		}
	}

	wkp_param.Enable = ((wk_enable_cnt > 0) ? 1 : 0);
	ret = wlan_ext_request(wlan_netif_get(WLAN_MODE_NONE), WLAN_EXT_CMD_SET_P2P_WKP_CFG, (int)(&wkp_param));
	if (ret) {
		WLAN_USER_ERR("fail to set P2P wakeup!\n");
		return ret;
	}

	return ret;
}


int wlan_ext_ap_hiden_ssid(uint32_t enable)
{
#if WLAN_USER_SUPPORT_HIDE_AP_SSID
#define IE_SSID_MAX_LEN (WLAN_SSID_MAX_LEN + 2)
	int ret;
	uint8_t ie[IE_SSID_MAX_LEN];
	wlan_ap_config_t config;

	memset(ie, 0, IE_SSID_MAX_LEN);
	if (enable) {
		/*
		 * Beacon SSID IE
		 *     Element ID: 0
		 *     Length = 0
		 *     SSID = NULL
		 */

		WLAN_USER_DBG("set hidden ssid\n");
	} else {
		config.field = WLAN_AP_FIELD_SSID;
		if (wlan_ap_get_config(&config) != 0) {
			WLAN_USER_ERR("get ap config failed\n");
			return -1;
		}

		ie[1] = config.u.ssid.ssid_len;
		memcpy(ie + 2, config.u.ssid.ssid, config.u.ssid.ssid_len);
		WLAN_USER_DBG("set normal ssid\n");
	}
	ret = wlan_ext_request(wlan_netif_get(WLAN_MODE_NONE), WLAN_EXT_CMD_SET_UPDATE_TEMP_IE, (unsigned int)(ie));
	if (ret)
		WLAN_USER_ERR("update beacon ssid ie failed=%d\n", ret);
	return ret;
#undef IE_SSID_MAX_LEN
#else
	return -1;
#endif
}

#endif /* __CONFIG_WLAN */
