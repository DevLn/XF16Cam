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

#ifdef __CONFIG_XRADIO_LOGGER
#include "util/xr_logger.h"
#include "_xr_logger.h"

/**
 * get the size of data in xl
 */
static uint32_t xrlog_buf_data_len(struct xr_logger *xl)
{
	int32_t len = xl->size * (xl->w_cnt - xl->r_cnt);

	len += (int32_t)(xl->w_idx - xl->r_idx);
	if ((len > xl->size) || (len < 0))
		len = xl->size;

	return (uint32_t)len;
}

static inline uint32_t xrlog_buf_free_len(struct xr_logger *xl)
{
	return (xl->size - xrlog_buf_data_len(xl));
}

/**
 * put a block of data into ring buffer
 */
uint32_t xrlog_buf_put(struct xr_logger *xl, const char *ptr, uint32_t len)
{
	uint32_t sz;

	//if (!xl || !ptr || len == 0)
		//return 0;

	sz = xrlog_buf_free_len(xl);
	if (sz == 0)
		return 0;

	if (sz < len)
		len = sz;

	if (xl->size - xl->w_idx > len) {
		memcpy(&xl->buffer[xl->w_idx], ptr, len);
		xl->w_idx += len;
	} else {
		memcpy(&xl->buffer[xl->w_idx], &ptr[0], xl->size - xl->w_idx);
		memcpy(&xl->buffer[0], &ptr[xl->size - xl->w_idx], len - (xl->size - xl->w_idx));
		xl->w_cnt++;
		xl->w_idx = len - (xl->size - xl->w_idx);
	}

	return len;
}

/**
 * put a block of data into ring buffer
 *
 * When the buffer is full, it will discard the old data.
 */
uint32_t xrlog_buf_put_force(struct xr_logger *xl, const char *ptr, uint32_t len)
{
	uint32_t sz;

	//if (!xl || !ptr || len == 0)
		//return 0;

	sz = xrlog_buf_free_len(xl);

	if (len > xl->size) {
		ptr = &ptr[len - xl->size];
		len = xl->size;
	}

	if (xl->size - xl->w_idx > len) {
		memcpy(&xl->buffer[xl->w_idx], ptr, len);
		xl->w_idx += len;
		if (len > sz)
			xl->r_idx = xl->w_idx;
	} else {
		memcpy(&xl->buffer[xl->w_idx], &ptr[0], xl->size - xl->w_idx);
		memcpy(&xl->buffer[0], &ptr[xl->size - xl->w_idx], len - (xl->size - xl->w_idx));
		xl->w_cnt++;
		xl->w_idx = len - (xl->size - xl->w_idx);
		if (len > sz)
			xl->r_idx = xl->w_idx;
	}

	return len;
}

/**
 * get data from ring buffer
 */
uint32_t xrlog_buf_get(struct xr_logger *xl, char *ptr, uint32_t len)
{
	uint32_t sz;

	//if (!xl || !ptr || len == 0)
		//return 0;

	if ((xl->w_cnt - xl->r_cnt > 1) || (xl->w_cnt < xl->r_cnt))
		xl->r_cnt = xl->w_cnt - 1;

	sz = xrlog_buf_data_len(xl);
	if (sz == 0)
		return 0;

	if (sz < len)
		len = sz;

	if (xl->size - xl->r_idx > len) {
		memcpy(ptr, &xl->buffer[xl->r_idx], len);
		xl->r_idx += len;
		return len;
	}

	memcpy(&ptr[0], &xl->buffer[xl->r_idx], xl->size - xl->r_idx);
	memcpy(&ptr[xl->size - xl->r_idx], &xl->buffer[0], len - (xl->size - xl->r_idx));

	xl->r_cnt++;
	xl->r_idx = len - (xl->size - xl->r_idx);

	return len;
}

/**
 * empty the ring buffer
 */
void xrlog_buf_reset(struct xr_logger *xl)
{
	//if (!xl)
		//return;

	xl->r_cnt = 0;
	xl->r_idx = 0;
	xl->w_cnt = 0;
	xl->w_idx = 0;
}

#endif /* __CONFIG_XRADIO_LOGGER */
