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
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#ifdef __CONFIG_XRADIO_LOGGER
#include "util/xr_logger.h"
#include "_xr_logger.h"
#include "kernel/os/os_time.h"
#include "driver/chip/hal_rtc.h"

static struct xr_logger *_xl_t;

void xrlog_start(void)
{
	struct xr_logger *xl = _xl_t;

	if (!xl)
		return;

	xl->locked = false;
}

void xrlog_stop(void)
{
	struct xr_logger *xl = _xl_t;

	if (!xl)
		return;

	xl->locked = true;
}

void xrlog_set_loop(bool loop)
{
	struct xr_logger *xl = _xl_t;

	if (!xl)
		return;

	xl->loop = loop;
}

uint32_t xrlog_write(const char *log, uint32_t len)
{
	uint32_t ret;
	struct xr_logger *xl = _xl_t;
	char str[32];
	uint32_t sz = 0;
	char enter;
	OS_Time_t tick = OS_GetTicks();

	if (!xl || !log || !xl->inited || xl->locked || len < 1)
		return 0;

	xrlog_lock(xl);
	if (xl->enter)
		sz += snprintf(str + sz, 32, "%d ", tick);
	enter = log[len];
	if (enter == 0)
		enter = log[len - 1];
	if (enter == '\n' || enter == '\r')
		xl->enter = 1;
	else
		xl->enter = 0;

	if (xl->loop) {
		ret = xrlog_buf_put_force(xl, str, sz);
		ret += xrlog_buf_put_force(xl, log, len);
	} else {
		ret = xrlog_buf_put(xl, str, sz);
		ret += xrlog_buf_put(xl, log, len);
	}
	xrlog_unlock(xl);
	return ret;
}

uint32_t xrlog_read(char *buf, uint32_t len)
{
	uint32_t ret;
	struct xr_logger *xl = _xl_t;

	if (!xl || !buf || len < 1)
		return 0;

	xrlog_lock(xl);
	ret = xrlog_buf_get(xl, buf, len);
	xrlog_unlock(xl);
	return ret;
}

void xrlog_flush(void)
{
	struct xr_logger *xl = _xl_t;

	if (!xl)
		return;

	xrlog_lock(xl);
	xrlog_buf_reset(xl);
	xrlog_unlock(xl);
}

void *xrlog_init(xrlog_initparam *param)
{
	struct xr_logger *xl = _xl_t;

	if (xl || !param)
		return NULL;

	xl = malloc(sizeof(struct xr_logger) + param->size);
	if (xl) {
		memset(xl, 0, sizeof(struct xr_logger) + param->size);
		xl->loop = param->loop;
		xl->size = param->size;
		xrlog_lock_init(xl);
		xl->enter = 1;
		xl->inited = 1;
	}

	_xl_t = xl;
	return xl;
}

void xrlog_deinit(void)
{
	struct xr_logger *xl = _xl_t;

	if (!xl)
		return;

	_xl_t = NULL;
	xrlog_lock(xl);
	xrlog_unlock(xl);

	xrlog_lock_deinit(xl);
	free(xl);
}

#endif /* __CONFIG_XRADIO_LOGGER */

