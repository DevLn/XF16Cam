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

#ifdef __CONFIG_XRADIO_LOGGER
#include "util/xr_logger.h"
#include "_xr_logger.h"
#include "driver/chip/hal_cmsis.h"
#include "kernel/os/os_thread.h"

/* case of critical context
 *    - IRQ disabled
 *    - FIQ disabled
 *    - Execute in ISR context
 *    - Scheduler is not running
 */
static int _is_critical_context(void)
{
	return (__get_PRIMASK() || __get_FAULTMASK() || __get_IPSR() || !OS_ThreadIsSchedulerRunning());
}

void xrlog_lock(struct xr_logger *xl)
{
	if (_is_critical_context()) {
		return;
	}

	if (OS_MutexIsValid(&xl->mutex))
		OS_RecursiveMutexLock(&xl->mutex, OS_WAIT_FOREVER);
}

void xrlog_unlock(struct xr_logger *xl)
{
	if (_is_critical_context()) {
		return;
	}

	if (OS_MutexIsValid(&xl->mutex))
		OS_RecursiveMutexUnlock(&xl->mutex);
}

void xrlog_lock_init(struct xr_logger *xl)
{
	OS_RecursiveMutexCreate(&xl->mutex);
}

void xrlog_lock_deinit(struct xr_logger *xl)
{
	OS_RecursiveMutexDelete(&xl->mutex);
}

#endif /* __CONFIG_XRADIO_LOGGER */
