/*
 * Copyright (C) 2023 XRADIO TECHNOLOGY CO., LTD. All rights reserved.
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
#ifdef __CONFIG_PSRAM
#include "sys/sys_heap.h"
#else
#include <stdlib.h>
#endif
#include "sonic/sonic_glue.h"
#ifdef __CONFIG_SONIC
sonicStream g_stream = NULL;
#endif

#ifdef __CONFIG_PSRAM
void *sonicMalloc(int size)
{
    return psram_malloc(size);
}

void *sonicCalloc(int num, int size)
{
    return psram_calloc(num, size);
}

void *sonicRealloc(void *p, int oldNum, int newNum, int size)
{
    return psram_realloc(p, newNum * size);
}

void sonicFree(void *p)
{
    psram_free(p);
}
#else
void *sonicMalloc(int size)
{
    return malloc(size);
}

void *sonicCalloc(int num, int size)
{
    return calloc(num, size);
}

void *sonicRealloc(void *p, int oldNum, int newNum, int size)
{
    return realloc(p, newNum * size);
}

void sonicFree(void *p)
{
    free(p);
}
#endif

void xrSonicSetSpeed(float speed)
{
#if defined(__CONFIG_SONIC_LITE)
    sonicSetSpeed(speed);
#elif defined(__CONFIG_SONIC)
    if (g_stream == NULL) {
        printf("please init Sonic at first\n");
        return ;
    }
    sonicSetSpeed(g_stream, speed);
#endif
}

void xrSonicFlushStream(void)
{
#if defined(__CONFIG_SONIC_LITE)
    sonicFlushStream();
#elif defined(__CONFIG_SONIC)
    if (g_stream == NULL) {
        printf("please init Sonic at first\n");
        return ;
    }
    sonicFlushStream(g_stream);
#endif
}

void xrSonicWriteShortToStream(short *samples, int numSamples)
{
#if defined(__CONFIG_SONIC_LITE)
    sonicWriteShortToStream(samples, numSamples);
#elif defined(__CONFIG_SONIC)
    if (g_stream == NULL) {
        printf("please init Sonic at first\n");
        return ;
    }
    sonicWriteShortToStream(g_stream, samples, numSamples);
#endif
}

int xrSonicReadShortFromStream(short *samples, int maxSamples)
{
#if defined(__CONFIG_SONIC_LITE)
    return sonicReadShortFromStream(samples, maxSamples);
#elif defined(__CONFIG_SONIC)
    if (g_stream == NULL) {
        printf("please init Sonic at first\n");
        return 0;
    }
    return sonicReadShortFromStream(g_stream, samples, maxSamples);
#endif
    return 0;
}

int xrSonicInit(int rate, int channle, int sample)
{
    int ret = -1;
#if defined(__CONFIG_SONIC_LITE)
    ret = sonicInit(rate, sample);
#elif defined(__CONFIG_SONIC)
    g_stream = sonicCreateStream(rate, channle, sample);
    ret = (g_stream == NULL) ? -1 : 0;
#endif
    return ret;
}

void xrSonicDeinit(void)
{
#if defined(__CONFIG_SONIC_LITE)
    sonicDeInit();
#elif defined(__CONFIG_SONIC)
    if (g_stream == NULL) {
        printf("Sonic is not init\n");
        return ;
    }
    sonicDestroyStream(g_stream);
    g_stream = NULL;
#endif
}
