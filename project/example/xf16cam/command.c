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

#include "common/cmd/cmd_util.h"
#include "common/cmd/cmd.h"
#include "driver/chip/hal_prcm.h"
#include "driver/chip/hal_wdg.h"

#include "xf16cam_config.h"

static enum cmd_status cmd_xf16cam_wifi_exec(char *cmd)
{
	char *argv[3];
	int argc = cmd_parse_argv(cmd, argv, cmd_nitems(argv));
	int ret;

	if (argc == 1 && strcmp(argv[0], "ap") == 0) {
		ret = xf16cam_config_save_ap();
	} else if (argc == 3 && strcmp(argv[0], "sta") == 0) {
		ret = xf16cam_config_save_sta(argv[1], argv[2]);
	} else {
		printf("usage: wifi ap | wifi sta <ssid> <password>\n");
		return CMD_STATUS_INVALID_ARG;
	}

	if (ret != 0) {
		printf("wifi config rejected\n");
		return CMD_STATUS_FAIL;
	}

	printf("wifi config saved; rebooting\n");
	OS_MSleep(100);
	HAL_PRCM_SetCPUABootFlag(PRCM_CPUA_BOOT_FROM_COLD_RESET);
	HAL_WDG_Reboot();
	return CMD_STATUS_OK;
}

/*
 * main commands
 */
static const struct cmd_data g_main_cmds[] = {
	{ "upgrade", cmd_upgrade_exec },
	{ "mem",     cmd_mem_exec },
	{ "camera",  cmd_camera_exec },
	{ "wifi",    cmd_xf16cam_wifi_exec },
};

void main_cmd_exec(char *cmd)
{
	cmd_main_exec(cmd, g_main_cmds, cmd_nitems(g_main_cmds));
}
