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
#include "driver/chip/flashchip/flash_chip.h"
#include "sys/param.h"
#include "../hal_base.h"
#include "sys/xr_debug.h"

#define FLASH_DEBUG(fmt, arg...)    XR_DEBUG((DBG_OFF | XR_LEVEL_ALL), NOEXPAND, "[Flash chip DBG] <%s : %d> " fmt "\n", __func__, __LINE__, ##arg)
#define FLASH_ALERT(fmt, arg...)    XR_ALERT((DBG_ON | XR_LEVEL_ALL), NOEXPAND, "[Flash chip ALT] <%s : %d> " fmt "\n", __func__, __LINE__, ##arg)
#define FLASH_ERROR(fmt, arg...)    XR_ERROR((DBG_ON | XR_LEVEL_ALL), NOEXPAND, "[Flash chip ERR] <%s : %d> " fmt "\n", __func__, __LINE__, ##arg)
#define FLASH_NOWAY()               XR_ERROR((DBG_ON | XR_LEVEL_ALL), NOEXPAND, "[Flash chip should not be here] <%s : %d> \n", __func__, __LINE__)
#define FLASH_NOTSUPPORT()          FLASH_ALERT("not support CMD")
#define PCHECK(p)

#define XT25W32B_JEDEC 0x16600B

typedef enum {
	FLASH_INSTRUCTION_RDSR = 0x05,				/* read status register */
	FLASH_INSTRUCTION_WRSR = 0x01,				/* write status register */
	FLASH_INSTRUCTION_RDSR2 = 0x35,				/* read status register-1 */
	FLASH_INSTRUCTION_SRWREN = 0x50,
} eSF_Instruction;

/* internal macros for flash chip instruction */
#define FCI_CMD(idx)    instruction[idx]
#define FCI_ADDR(idx)   instruction[idx]
#define FCI_DUMMY(idx)  instruction[idx]
#define FCI_DATA(idx)   instruction[idx]

static int XT25WXXB_SwitchReadMode(struct FlashChip *chip, FlashReadMode mode)
{
	uint8_t status[2];
	int ret;

	PCHECK(chip);
	if (!(mode & chip->cfg.mReadSupport)) {
		FLASH_NOTSUPPORT();
		return HAL_INVALID;
	}

	if ((!((chip->cfg.mReadStausSupport & FLASH_STATUS1) && (chip->cfg.mWriteStatusSupport & FLASH_STATUS1)))
	    || (!((chip->cfg.mReadStausSupport & FLASH_STATUS2) && (chip->cfg.mWriteStatusSupport & FLASH_STATUS2)))) {
		//do not need switch
		return 0;
	}

	if (mode == FLASH_READ_QUAD_O_MODE || mode == FLASH_READ_QUAD_IO_MODE || mode == FLASH_READ_QPI_MODE) {
		ret = chip->readStatus(chip, FLASH_STATUS1, status);
		if (ret < 0)
			return -1;
		ret = chip->readStatus(chip, FLASH_STATUS2, status + 1);
		if (ret < 0)
			return -1;
		status[1] |= 1 << 1;
		ret = chip->writeStatus(chip, FLASH_STATUS2, status);
	} else {
		ret = chip->readStatus(chip, FLASH_STATUS1, status);
		if (ret < 0)
			return -1;
		ret = chip->readStatus(chip, FLASH_STATUS2, status + 1);
		if (ret < 0)
			return -1;
		status[1] &= ~(1 << 1);
		ret = chip->writeStatus(chip, FLASH_STATUS2, status);
	}

	return ret;
}

static int XT25WXXB_ReadStatus(struct FlashChip *chip, FlashStatus reg, uint8_t *status)
{
	int ret;
	PCHECK(chip);
	PCHECK(status);
	InstructionField instruction[2];

	HAL_Memset(&instruction, 0, sizeof(instruction));

	if (reg == FLASH_STATUS1) {
		FCI_CMD(0).data = FLASH_INSTRUCTION_RDSR;
	} else if (reg == FLASH_STATUS2) {
		FCI_CMD(0).data = FLASH_INSTRUCTION_RDSR2;
	} else {
		FLASH_NOWAY();
	}

	FCI_DATA(1).pdata = (uint8_t *)status;
	FCI_DATA(1).len = 1;
	FCI_DATA(1).line = 1;

	ret = chip->driverRead(chip, &FCI_CMD(0), NULL, NULL, &FCI_DATA(1));

	return ret;
}

static int XT25WXXB_WriteStatus(struct FlashChip *chip, FlashStatus reg, uint8_t *status)
{
	int ret;
	PCHECK(chip);
	PCHECK(status);
	InstructionField instruction[2];

	HAL_Memset(&instruction, 0, sizeof(instruction));

	FCI_CMD(0).data = FLASH_INSTRUCTION_SRWREN;
	FCI_CMD(0).line = 1;

	chip->driverWrite(chip, &FCI_CMD(0), NULL, NULL, NULL);

	HAL_Memset(&instruction, 0, sizeof(instruction));

	FCI_CMD(0).data = FLASH_INSTRUCTION_WRSR;
	FCI_DATA(1).pdata = (uint8_t *)status;
	FCI_DATA(1).len = 2;
	FCI_DATA(1).line = 1;

	chip->writeEnable(chip);

	ret = chip->driverWrite(chip, &FCI_CMD(0), NULL, NULL, &FCI_DATA(1));

	chip->writeDisable(chip);

	return ret;
}

static int XT25WXXB_FlashInit(struct FlashChip *chip)
{
	PCHECK(chip);

	chip->writeEnable = defaultWriteEnable;
	chip->writeDisable = defaultWriteDisable;
	chip->readStatus = XT25WXXB_ReadStatus;
	chip->writeStatus = XT25WXXB_WriteStatus;
	chip->erase = defaultErase;
	chip->jedecID = defaultGetJedecID;
	chip->pageProgram = defaultPageProgram;
	chip->read = defaultRead;

	chip->driverWrite = defaultDriverWrite;
	chip->driverRead = defaultDriverRead;
	chip->setFreq = defaultSetFreq;
	chip->switchReadMode = XT25WXXB_SwitchReadMode;
	chip->xipDriverCfg = defaultXipDriverCfg;
	chip->enableXIP = defaultEnableXIP;
	chip->disableXIP = defaultDisableXIP;
	chip->isBusy = defaultIsBusy;
	chip->control = defaultControl;
	chip->minEraseSize = defaultGetMinEraseSize;
	chip->enableQPIMode = defaultEnableQPIMode;
	chip->disableQPIMode = defaultDisableQPIMode;
	/* chip->enableReset = defaultEnableReset; */
	chip->reset = defaultReset;

	chip->suspendErasePageprogram = defaultSuspendErasePageprogram;
	chip->resumeErasePageprogram = defaultResumeErasePageprogram;
	//chip->isSuspend = defaultIsSuspend;
	chip->powerDown = NULL;
	chip->releasePowerDown = NULL;
	chip->uniqueID = NULL;

	return 0;
}

static int XT25WXXB_FlashDeinit(struct FlashChip *chip)
{
	PCHECK(chip);

//	HAL_Free(chip);

	return 0;
}

static struct FlashChip *XT25WXXB_FlashCtor(struct FlashChip *chip, uint32_t arg)
{
	uint32_t jedec = arg;
	uint32_t size;
	PCHECK(chip);

	if (jedec == XT25W32B_JEDEC) {
		size = 32 * 32 * 0x1000;
	} else {
		return NULL;
	}

	/* TODO: use HAL_Memcpy() and const to init chip->cfg to save code size */
	chip->cfg.mJedec = jedec;
	chip->cfg.mSize = size;
	chip->cfg.mMaxFreq = 80 * 1000 * 1000;
	chip->cfg.mMaxReadFreq = 80 * 1000 * 1000;
	chip->cfg.mEraseSizeSupport = FLASH_ERASE_64KB | FLASH_ERASE_32KB | FLASH_ERASE_4KB | FLASH_ERASE_CHIP;
	chip->cfg.mPageProgramSupport = FLASH_PAGEPROGRAM | FLASH_QUAD_PAGEPROGRAM;
	chip->cfg.mReadStausSupport = FLASH_STATUS1 | FLASH_STATUS2;
	chip->cfg.mWriteStatusSupport = FLASH_STATUS1 | FLASH_STATUS2;
	chip->cfg.mReadSupport = FLASH_READ_NORMAL_MODE | FLASH_READ_FAST_MODE | FLASH_READ_DUAL_O_MODE
				| FLASH_READ_DUAL_IO_MODE | FLASH_READ_QUAD_O_MODE | FLASH_READ_QUAD_IO_MODE;
	chip->mPageSize = 256;
	chip->mFlashStatus = 0;
	chip->mDummyCount = 1;

	return chip;
}

FlashChipCtor  XT25W32B_FlashChip = {
		.mJedecId = XT25W32B_JEDEC,
		.enumerate = XT25WXXB_FlashCtor,
		.init = XT25WXXB_FlashInit,
		.destory = XT25WXXB_FlashDeinit,
};

