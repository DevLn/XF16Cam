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

#include <stdio.h>
#include <string.h>

#include "console/console.h"
#include "driver/chip/hal_prcm.h"
#include "driver/chip/hal_wdg.h"
#include "kernel/os/os.h"

#include "xf16cam_config.h"
#include "xf16cam_storage.h"

static char *xf16cam_cmd_arg(char **cursor)
{
	char *arg;
	char *p = *cursor;

	while (*p == ' ' || *p == '\t')
		++p;
	if (*p == '\0') {
		*cursor = p;
		return NULL;
	}
	arg = p;
	while (*p != '\0' && *p != ' ' && *p != '\t')
		++p;
	if (*p != '\0')
		*p++ = '\0';
	*cursor = p;
	return arg;
}

static void xf16cam_cmd_response(int status, int prompt)
{
	static const char *const responses[] = {
		"<ACK> 0 OK\n",
		"<ACK> 1 Unknown command\n",
		"<ACK> 2 Invalid argument\n",
		"<ACK> 3 Fail\n",
	};

	if (status < 0 || status >= (int)(sizeof(responses) / sizeof(responses[0])))
		status = 3;
	console_write((uint8_t *)responses[status], strlen(responses[status]));
	if (prompt)
		console_write((uint8_t *)"$ ", 2);
}

static void __attribute__((noreturn)) xf16cam_cmd_upgrade(void)
{
	/* Keep the serial recovery path independent of media, storage, and
	 * configuration state. A full UART update can repair any of them. */
	xf16cam_cmd_response(0, 0);
	OS_MSleep(10);
	HAL_PRCM_SetCPUABootFlag(PRCM_CPUA_BOOT_FROM_SYS_UPDATE);
	__DSB();
	__ISB();
	HAL_WDG_Reboot();
	for (;;) {
	}
}

static void xf16cam_cmd_wifi(char *args)
{
	char *mode = xf16cam_cmd_arg(&args);
	char *ssid;
	char *password;
	int ret = -1;

	if (mode && strcmp(mode, "ap") == 0 && xf16cam_cmd_arg(&args) == NULL) {
		ret = xf16cam_config_save_ap();
	} else if (mode && strcmp(mode, "sta") == 0) {
		ssid = xf16cam_cmd_arg(&args);
		password = xf16cam_cmd_arg(&args);
		if (ssid && password && xf16cam_cmd_arg(&args) == NULL)
			ret = xf16cam_config_save_sta(ssid, password);
	} else {
		printf("usage: wifi ap | wifi sta <ssid> <password>\n");
	}

	if (ret != 0) {
		xf16cam_cmd_response(2, 1);
		return;
	}
	if (xf16cam_update_begin() != 0) {
		printf("wifi config saved; reboot deferred because an update is active\n");
		xf16cam_cmd_response(3, 1);
		return;
	}

	printf("wifi config saved; rebooting\n");
	if (xf16cam_storage_unmount() != 0)
		printf("xf16cam console: SD eject failed before reboot\n");
	xf16cam_cmd_response(0, 0);
	OS_MSleep(100);
	HAL_PRCM_SetCPUABootFlag(PRCM_CPUA_BOOT_FROM_COLD_RESET);
	HAL_WDG_Reboot();
}

void main_cmd_exec(char *cmd)
{
	char *cursor = cmd;
	char *name = xf16cam_cmd_arg(&cursor);

	if (name == NULL) {
		console_write((uint8_t *)"$ ", 2);
	} else if (strcmp(name, "upgrade") == 0 && xf16cam_cmd_arg(&cursor) == NULL) {
		xf16cam_cmd_upgrade();
	} else if (strcmp(name, "wifi") == 0) {
		xf16cam_cmd_wifi(cursor);
	} else {
		xf16cam_cmd_response(1, 1);
	}
}
