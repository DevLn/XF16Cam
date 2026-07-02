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

#ifndef __XRADIO_PM_IDLE_SUSPEND_H
#define __XRADIO_PM_IDLE_SUSPEND_H

#if (defined(__CONFIG_PM_IDLE_SUSPEND))
#define CONFIG_PM_IDLE_SUSPEND
#endif

/**
 * @brief The pm idle suspend policy structure.
 */
typedef enum {
	PIS_POLICY_NO_SUSPEND = 0,     /* only enter tickless, sleep time base freertos */
	PIS_POLICY_SUSPEND_BY_SYS,    /* enter tickless or standby, sleep time base freertos */
	PIS_POLICY_SUSPEND_BY_USER,   /* enter tickless or standby, sleep time base user */
	PIS_POLICY_MAX,
} pis_policy_t;

/**
 * @brief The pm idle suspend param structure.
 */
struct pis_param {
	uint32_t suspend_threshold;      /* suspend time threshold,decide to enter tickless or standby*/
	uint32_t compensate_threshold;   /* compensate time threshold,decide to compensate or not*/
};

#ifdef CONFIG_PM_IDLE_SUSPEND
/**
 * @brief set pm idle suspend param.
   @param param->pis_param struct
   @retval 0 for success, other for fail
 */
extern int pm_idle_suspend_set_param(struct pis_param *param);
/**
 * @brief set pm idle suspend policy.
   @param policy->pis_policy_t enum
   @retval 0 for success, other for fail
 */
extern int pm_idle_suspend_set_policy(pis_policy_t policy);
/**
 * @brief set pm idle suspend period.
   @param period->period in ms
   @retval 0 for success, other for fail
 */
extern int pm_idle_suspend_set_period(uint32_t period);
/**
 * @brief pm idle suspend enter handle,.
   @param period:period in ms
   @retval 0 for success, other for fail
 */
extern int pm_idle_suspend_enter_handle(void);
/**
 * @brief pm idle init
 */
void pm_idle_suspend_init(void);
/**
 * @brief freertos configPRE_SUPPRESS_TICKS_AND_SLEEP_PROCESSING()
   @param xExpectedIdleTime:expected time by freertos
   @retval expected time for tickless
 */
extern uint32_t pm_idle_suspend_suppress_tick_and_sleep(uint32_t xExpectedIdleTime);
#else
static inline int pm_idle_suspend_set_param(struct pis_param *param) { return 0; }
static inline int pm_idle_suspend_set_policy(pis_policy_t policy) { return 0; }
static inline int pm_idle_suspend_set_period(uint32_t period) { return 0; }
static inline int pm_idle_suspend_enter_handle(void) { return 0; }

#endif /* CONFIG_PM_IDLE_SUSPEND */

#endif

