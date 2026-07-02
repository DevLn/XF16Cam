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

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "kernel/os/os_semaphore.h"
#include "kernel/os/os_thread.h"
#include "kernel/os/os_time.h"
#include "sys/interrupt.h"
#include "driver/chip/hal_rtc.h"
#include "driver/chip/hal_wakeup.h"
#include "_pm_define.h"
#include "pm/pm.h"
#include "pm/pm_idle_suspend.h"

#ifdef CONFIG_PM_IDLE_SUSPEND
#define PIS_SUSPEND_THRESHOLD          (80U)
#define PIS_COMPENSATE_THRESHOLD       (0xFFFFFFFFU)
#define PIS_THREAD_STACK_SIZE          (1024 * 2)
#define PIS_MAX_PERIOD_MS              (0x3E7FFFF)   /* wuptimer max cnt is 31bit, 0x3E7FFFF = (0x7FFFFFFFU * 1000U / 32768U)*/
#define PIS_MAX_FRUN_MS                (0x7CFFFFF)   /* freerun timer max cnt is 32bit, 0x7CFFFFF = (0xFFFFFFFFU * 1000U / 32768U) */
#define TIME_MS_TO_32K(t)              ((uint64_t)(t) * 32768U / 1000U)
#define TIME_32K_TO_MS(t)              ((uint64_t)(t) * 1000U / 32768U)
#define TIME_US_TO_MS(t)               (t / 1000U)
#define GET_FRUN_CNT()                 ((uint32_t)HAL_RTC_GetFreeRunCnt())
#define GET_SUSPEND_RESUME_LATENCY()   (TIME_US_TO_MS(pm_get_suspend_resume_latency()))
#define GET_RESUME_LATENCY()           (TIME_US_TO_MS(pm_get_resume_latency()))
#define PIS_IRQ_SAVE()          arch_irq_save()
#define PIS_IRQ_RESTORE(flag)   arch_irq_restore(flag)

static uint8_t pis_protect;       /* pis standby protect, 1 is in standby, 0 is out standby */
static uint32_t pis_period_cnt;   /* pis sleep period cnt */
static uint32_t pis_end_cnt;     /* pis sleep end cnt */
static uint32_t pis_compensate_cnt;   /* pis compensate, use to mark compensate time */
static uint32_t pis_compensate_tick;
static struct pis_param pis_param =  {
	.suspend_threshold = PIS_SUSPEND_THRESHOLD,
	.compensate_threshold = PIS_COMPENSATE_THRESHOLD,
};   /* pis param */
static pis_policy_t pis_policy = PIS_POLICY_NO_SUSPEND;
static OS_Thread_t pis_task_thread;
static OS_Semaphore_t pis_task_sem;

/**
 * @brief get pm idle suspend period.
   retval remain period in ms
 */
static uint32_t pm_idle_suspend_get_period(void)
{
	unsigned long irqflag;
	uint32_t cur_cnt;
	uint32_t start_cnt;
	uint32_t period = 0;

	irqflag = PIS_IRQ_SAVE();
	cur_cnt = GET_FRUN_CNT();
	start_cnt = pis_end_cnt - pis_period_cnt;
	if ((start_cnt < pis_end_cnt && cur_cnt < pis_end_cnt) ||
	    (start_cnt > pis_end_cnt && (cur_cnt >= start_cnt || cur_cnt < pis_end_cnt))) {
		period = TIME_32K_TO_MS(pis_end_cnt - cur_cnt);
	}
	PIS_IRQ_RESTORE(irqflag);
	return period;
}

/**
 * @brief is pm idle suspend allow standby
   @param lantency:lantency time in ms
 */
static uint8_t pm_idle_suspend_is_allow_standby(uint32_t lantency)
{
	unsigned long irqflag;
	uint8_t ret = 0;

	irqflag = PIS_IRQ_SAVE();
	if (pis_policy != PIS_POLICY_NO_SUSPEND && pm_idle_suspend_get_period() > lantency) {
		ret = 1;
	}
	PIS_IRQ_RESTORE(irqflag);
	return ret;
}

/**
 * @brief pm idle start compensation
   @note set wakeup timer to wakeup from standby, mark the rtc count
   @retval 0 for success, other for fail
 */
static int pm_idle_suspend_start_compensate(void)
{
	unsigned long irqflag;
	uint8_t ret = -1;

	irqflag = PIS_IRQ_SAVE();
	/*
	 * if policy is PIS_POLICY_NO_SUSPEND, pm need not compensate,
	 * user can use "pm standby" cmd in PIS_POLICY_NO_SUSPEND
	*/
	if (pis_policy == PIS_POLICY_NO_SUSPEND) {
		ret = 0;
	} else if (pm_idle_suspend_is_allow_standby(GET_SUSPEND_RESUME_LATENCY())) {
		pis_compensate_tick = OS_GetTicks();
		pis_compensate_cnt = GET_FRUN_CNT();
		ret = 0;
	}
	PIS_IRQ_RESTORE(irqflag);
	return ret;
}

/**
 * @brief pm idle finish compensation
   @note close wakeup timer, calculate compensation time by rtc counter, compensation for rtos tick
   @retval 0 for success, other for fail
 */
static int pm_idle_suspend_finish_compensate(void)
{
	unsigned long irqflag;
	uint32_t tick;
	uint32_t compensate;

	irqflag = PIS_IRQ_SAVE();
	if (pis_policy != PIS_POLICY_NO_SUSPEND) {
		tick = OS_GetTicks() - pis_compensate_tick;
		compensate = TIME_32K_TO_MS(GET_FRUN_CNT() - pis_compensate_cnt);
		if (compensate >= tick) {
			compensate -= tick;
		} else {
			compensate = 0;
		}
		if (pis_param.compensate_threshold == PIS_COMPENSATE_THRESHOLD ||
		    compensate <= pis_param.compensate_threshold) {
			OS_AddTicks(compensate);
		}
		PM_LOGD("real_comp: %d\n", compensate);
	}
	PIS_IRQ_RESTORE(irqflag);
	return 0;
}

/**
 * @brief set pm idle suspend param.
   @param param->pis_param struct
   @retval 0 for success, other for fail
 */
int pm_idle_suspend_set_param(struct pis_param *param)
{
	unsigned long irqflag;

	if (param == NULL) {
		return -EINVAL;
	}
	if (param->suspend_threshold > PIS_MAX_PERIOD_MS ||
	    param->suspend_threshold <= GET_SUSPEND_RESUME_LATENCY()) {
		return -EINVAL;
	}
	if (param->compensate_threshold != PIS_COMPENSATE_THRESHOLD &&
	    param->compensate_threshold > PIS_MAX_FRUN_MS) {
		return -EINVAL;
	}
	irqflag = PIS_IRQ_SAVE();
	memcpy(&pis_param, param, sizeof(*param));
	PIS_IRQ_RESTORE(irqflag);
	return 0;
}

/**
 * @brief set pm idle suspend policy.
   @param policy->pis_policy_t enum
   @retval 0 for success, other for fail
 */
int pm_idle_suspend_set_policy(pis_policy_t policy)
{
	unsigned long irqflag;
	uint8_t ret = -1;

	irqflag = PIS_IRQ_SAVE();
	if (policy < PIS_POLICY_MAX && pis_policy != policy) {
		pis_policy = policy;
		pis_period_cnt = 0;
		ret = 0;
	}
	PIS_IRQ_RESTORE(irqflag);
	if (ret) {
		PM_LOGD("pis_set_policy change: %d\n", pis_policy);
	}
	return ret;
}

/**
 * @brief set pm idle suspend period.
   @param period:period in ms
   @retval 0 for success, other for fail
 */
int pm_idle_suspend_set_period(uint32_t period)
{
	unsigned long irqflag;
	uint8_t ret = -1;

	irqflag = PIS_IRQ_SAVE();
	if (pis_policy == PIS_POLICY_SUSPEND_BY_USER  && period <= PIS_MAX_PERIOD_MS) {
		pis_period_cnt = TIME_MS_TO_32K(period);
		pis_end_cnt = GET_FRUN_CNT() + pis_period_cnt;
		ret = 0;
	}
	PIS_IRQ_RESTORE(irqflag);
	if (ret) {
		PM_LOGD("%s period: %d\n", __func__, period);
	}
	return ret;
}

/**
 * @brief pm idle suspend enter handle,.
   @param period:period in ms
   @retval 0 for success, other for fail
 */
int pm_idle_suspend_enter_handle(void)
{
	uint32_t period;
	uint32_t latency;

	period = pm_idle_suspend_get_period();
	latency = GET_RESUME_LATENCY();
	if (period <= latency) {
		return -1;
	}
	HAL_Wakeup_SetTimer_mS(period - latency);
	return 0;
}

/**
 * @brief freertos configPRE_SUPPRESS_TICKS_AND_SLEEP_PROCESSING()
   @param xExpectedIdleTime:expected time by freertos
   @retval expected time for tickless
 */
uint32_t pm_idle_suspend_suppress_tick_and_sleep(uint32_t xExpectedIdleTime)
{
	unsigned long irqflag;
	uint32_t ret_time = xExpectedIdleTime;

	irqflag = PIS_IRQ_SAVE();
	if (pis_protect) {
		goto exit;
	}

	if (pis_policy == PIS_POLICY_NO_SUSPEND) {
		goto exit;
	} else if (pis_policy == PIS_POLICY_SUSPEND_BY_SYS) {
		if (xExpectedIdleTime > PIS_MAX_PERIOD_MS) {
			pis_period_cnt = TIME_MS_TO_32K(PIS_MAX_PERIOD_MS);
		} else {
			pis_period_cnt = TIME_MS_TO_32K(xExpectedIdleTime);

		}
		pis_end_cnt = GET_FRUN_CNT() + pis_period_cnt;
	}
	if (!pm_idle_suspend_get_period()) {
		pis_period_cnt = 0;
		goto exit;
	}
	if (pis_param.suspend_threshold <= GET_SUSPEND_RESUME_LATENCY() ||
	    pm_idle_suspend_get_period() <= pis_param.suspend_threshold) {
		goto exit;
	}
	OS_SemaphoreRelease(&pis_task_sem);
	ret_time = 0;
exit:
	PIS_IRQ_RESTORE(irqflag);
	return ret_time;
}

static void pm_idle_suspend_task(void *arg)
{
	unsigned long irqflag;
	uint8_t ret;

	while (1) {
		OS_SemaphoreWait(&pis_task_sem, OS_WAIT_FOREVER);
		PM_LOGD("standby period: %d\n", pm_idle_suspend_get_period());

		irqflag = PIS_IRQ_SAVE();
		ret = pm_idle_suspend_is_allow_standby(GET_SUSPEND_RESUME_LATENCY());
		PIS_IRQ_RESTORE(irqflag);
		if (!ret) {
			PM_LOGE("%s standby fail\n", __func__);
			continue;
		}
		pis_protect = 1;
		pm_enter_mode(PM_MODE_STANDBY);
		pis_protect = 0;
	}
}

static int pm_idle_suspend(struct soc_device *dev, enum suspend_state_t state)
{
	int ret = 0;

	if (state == PM_MODE_STANDBY) {
		ret = pm_idle_suspend_start_compensate();
	}
	return ret;
}

static int pm_idle_resume(struct soc_device *dev, enum suspend_state_t state)
{
	if (state == PM_MODE_STANDBY) {
		pm_idle_suspend_finish_compensate();
	}
	return 0;
}

static const struct soc_device_driver pis_drv = {
	.name    = "pm_idle_suspend",
	.suspend = pm_idle_suspend,
	.resume = pm_idle_resume,
};

static struct soc_device pis_dev = {
	.name   = "pm_idle_suspend",
	.driver = &pis_drv,
};

/**
 * @brief pm idle init
 */
void pm_idle_suspend_init(void)
{
	OS_SemaphoreCreate(&pis_task_sem, 0, 1);
	if (OS_ThreadCreate(&pis_task_thread,
	                    "pis_task",
	                    pm_idle_suspend_task,
	                    NULL,
	                    OS_THREAD_PRIO_APP,
	                    PIS_THREAD_STACK_SIZE) != OS_OK) {
		PM_LOGE("thread create error\n");
	}
	pm_register_ops(&pis_dev);
}
#endif /* CONFIG_PM_IDLE_SUSPEND */

