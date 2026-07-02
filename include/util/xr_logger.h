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

#ifndef _XR_LOGGER_H_
#define _XR_LOGGER_H_

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define __XRLOG_STR0 'X'
#define __XRLOG_STR1 'R'
#define __XRLOG_STR2 'L'
#define __XRLOG_STR3 'G'
#ifdef __CONFIG_XRADIO_LOGGER
#define XRLOG_STR   "XRLG"
#else
#define XRLOG_STR
#endif

/**
 * @brief xrlog initialization parameters
 */
typedef struct {
	bool loop;          /* loop when buffer full */
	uint32_t size;      /* buffer size, in bytes */
} xrlog_initparam;

#ifdef __CONFIG_XRADIO_LOGGER
void xrlog_start(void);
void xrlog_stop(void);
void xrlog_set_loop(bool loop);

/**
 * drop all buffered logs
 */
void xrlog_flush(void);

uint32_t xrlog_write(const char *log, uint32_t len);
uint32_t xrlog_read(char *buf, uint32_t len);

void *xrlog_init(xrlog_initparam *param);
void xrlog_deinit(void);
#else
static inline void xrlog_start(void) { ; }
static inline void xrlog_stop(void) { ; }
static inline void xrlog_set_loop(bool loop) { ; }
static inline void xrlog_flush(void) { ; }
static inline uint32_t xrlog_write(const char *log, uint32_t len) { return 0; }
static inline uint32_t xrlog_read(char *buf, uint32_t len) { return 0; }
static inline void *xrlog_init(xrlog_initparam *param) { return NULL; }
static inline void xrlog_deinit(void) { ; }
#endif

#endif /* _XR_LOGGER_H_ */
