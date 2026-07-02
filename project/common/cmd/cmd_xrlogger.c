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

#include "cmd_util.h"
#include "util/xr_logger.h"
#include "sys/xr_debug.h"
#include "driver/chip/hal_uart.h"
#include "common/board/board.h"

#ifdef __CONFIG_XRADIO_LOGGER
#define TEST_UART_PORT UART1_ID

/**
 * @brief test example:
 * 1. remove xrlog_write called in stdout_write @ retarget_stdout.c
 */
static void *_xl;

static enum cmd_status cmd_xrlog_init_exec(char *cmd)
{
	xrlog_initparam param;
	int32_t cnt;
	uint32_t loop, size;

	cnt = cmd_sscanf(cmd, "%d %d", &loop, &size);
	if (cnt != 2 || loop > 1) {
		CMD_ERR("err cmd:%s, expect: <0/1, 8192>\n", cmd);
		return CMD_STATUS_INVALID_ARG;
	}

	param.loop = loop;
	param.size = size;
	_xl = xrlog_init(&param);
	CMD_DBG("xrlog init @ %p\n", _xl);
	board_uart_init(TEST_UART_PORT);
	return CMD_STATUS_OK;
}

static enum cmd_status cmd_xrlog_deinit_exec(char *cmd)
{
	board_uart_deinit(TEST_UART_PORT);
	xrlog_deinit();
	return CMD_STATUS_OK;
}

static enum cmd_status cmd_xrlog_get_exec(char *cmd)
{
	int len;
	char *buf;
	int32_t cnt;
	uint32_t size = 0;

	cnt = cmd_sscanf(cmd, "%d", &size);
	if (cnt != 1 || size < 1)
		size = 10240;

	buf = malloc(size);
	if (!buf)
		return CMD_STATUS_FAIL;

	print_hex_dump_words(_xl, 40);
	do {
		len = xrlog_read(buf, size);
		HAL_UART_Transmit_Poll(TEST_UART_PORT, (const uint8_t *)buf, len);
		CMD_DBG("xrlog data len:%d\n", len);
		print_hex_dump_words(_xl, 40);
	} while (len);
	free(buf);

	return CMD_STATUS_OK;
}

static enum cmd_status cmd_xrlog_start_exec(char *cmd)
{
	xrlog_start();

	return CMD_STATUS_OK;
}

static enum cmd_status cmd_xrlog_stop_exec(char *cmd)
{
	xrlog_stop();

	return CMD_STATUS_OK;
}

static enum cmd_status cmd_xrlog_loop_exec(char *cmd)
{
	int32_t cnt;
	uint32_t loop;

	cnt = cmd_sscanf(cmd, "%d", &loop);
	if (cnt != 1 || loop > 1) {
		CMD_ERR("err cmd:%s, expect: <0/1>\n", cmd);
		return CMD_STATUS_INVALID_ARG;
	}

	xrlog_set_loop(loop);

	return CMD_STATUS_OK;
}

static enum cmd_status cmd_xrlog_flush_exec(char *cmd)
{
	xrlog_flush();

	return CMD_STATUS_OK;
}

static enum cmd_status cmd_xrlog_print_exec(char *cmd)
{
	int32_t cnt;
	uint32_t num;

	cnt = cmd_sscanf(cmd, "%d", &num);
	if (cnt != 1) {
		CMD_ERR("err cmd:%s, expect: <100>\n", cmd);
		return CMD_STATUS_INVALID_ARG;
	}

	for (cnt = 0; cnt < num / 20; cnt++)
		xrlog_write("0123456789ABCDEFGHI\n", 20);

	return CMD_STATUS_OK;
}
#endif

static const struct cmd_data g_xrlog_cmds[] = {
#ifdef __CONFIG_XRADIO_LOGGER
	{ "init",        cmd_xrlog_init_exec },
	{ "deinit",      cmd_xrlog_deinit_exec },
	{ "get",         cmd_xrlog_get_exec },
	{ "start",       cmd_xrlog_start_exec },
	{ "stop",        cmd_xrlog_stop_exec },
	{ "loop",        cmd_xrlog_loop_exec },
	{ "flush",       cmd_xrlog_flush_exec },
	{ "print",       cmd_xrlog_print_exec },
#endif
};

enum cmd_status cmd_xrlog_exec(char *cmd)
{
	return cmd_exec(cmd, g_xrlog_cmds, cmd_nitems(g_xrlog_cmds));
}
