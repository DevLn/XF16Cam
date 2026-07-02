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
#include "driver/chip/hal_flash.h"
#include "../hal_base.h"
#include "flash_debug.h"

#define GD25Q16X_JEDEC    0x1540c8 /* for GD25Q16X */

/* internal macros for flash chip instruction */
#define FCI_CMD(idx)    instruction[idx]
#define FCI_ADDR(idx)   instruction[idx]
#define FCI_DUMMY(idx)  instruction[idx]
#define FCI_DATA(idx)   instruction[idx]

typedef enum {
	FLASH_INSTRUCTION_RDSR1 = 0x05,
	FLASH_INSTRUCTION_RDSR2 = 0x35,
	FLASH_INSTRUCTION_SRWREN = 0x50,              /* write enable for volatile */
	FLASH_INSTRUCTION_WRSR = 0x01,              /* write enable for volatile */
} gdSF_Instruction;

__sram_rodata
static const FlashChipCfg _GD25Q16X_FlashChipCfg = {
	/* default config must be at first */
	.mJedec = GD25Q16X_JEDEC,    /* ID7-ID0 ID15-ID8 M7-M0 */
	.mSize = 2 * 1024 * 1024,
	.mEraseSizeSupport = FLASH_ERASE_64KB | FLASH_ERASE_32KB | FLASH_ERASE_4KB | FLASH_ERASE_CHIP,
	.mPageProgramSupport = FLASH_PAGEPROGRAM,
	.mReadStausSupport = FLASH_STATUS1 | FLASH_STATUS2,
	.mWriteStatusSupport = FLASH_STATUS1 | FLASH_STATUS2,
	.mReadSupport = FLASH_READ_NORMAL_MODE | FLASH_READ_FAST_MODE | FLASH_READ_DUAL_O_MODE |
	                FLASH_READ_DUAL_IO_MODE | FLASH_READ_QUAD_O_MODE | FLASH_READ_QUAD_IO_MODE |
	                FLASH_READ_QPI_MODE,
	.mMaxFreq = 133 * 1000 * 1000,
	.mMaxReadFreq = 133 * 1000 * 1000,
	//.mSuspendSupport = 1,
	//.mSuspend_Latency = 100,
	//.mResume_Latency = 200,
};

static int GD25Q16X_SwitchReadMode(struct FlashChip *chip, FlashReadMode mode)
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

static int GD25Q16X_ReadStatus(struct FlashChip *chip, FlashStatus reg, uint8_t *status)
{
	int ret;
	PCHECK(chip);
	PCHECK(status);
	InstructionField instruction[2];

	HAL_Memset(&instruction, 0, sizeof(instruction));

	if (reg == FLASH_STATUS1) {
		FCI_CMD(0).data = FLASH_INSTRUCTION_RDSR1;
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

static int GD25Q16X_WriteStatus(struct FlashChip *chip, FlashStatus reg, uint8_t *status)
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
	ret = chip->driverWrite(chip, &FCI_CMD(0), NULL, NULL, &FCI_DATA(1));

	return ret;
}

static int GD25Q16X_FlashInit(struct FlashChip *chip)
{
	PCHECK(chip);

	chip->writeEnable = defaultWriteEnable;
	chip->writeDisable = defaultWriteDisable;
	chip->readStatus = GD25Q16X_ReadStatus;
	chip->writeStatus = GD25Q16X_WriteStatus;
	chip->erase = defaultErase;
	chip->jedecID = defaultGetJedecID;
	chip->pageProgram = defaultPageProgram;
	chip->read = defaultRead;

	chip->driverWrite = defaultDriverWrite;
	chip->driverRead = defaultDriverRead;
	chip->setFreq = defaultSetFreq;
	chip->switchReadMode = GD25Q16X_SwitchReadMode;
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

static int GD25Q16X_FlashDeinit(struct FlashChip *chip)
{
	PCHECK(chip);

	/* HAL_Free(chip); */

	return 0;
}

static struct FlashChip *GD25Q16X_FlashCtor(struct FlashChip *chip,
                                             uint32_t arg)
{
	uint32_t jedec = arg;
	PCHECK(chip);

	if (jedec != GD25Q16X_JEDEC) {
		return NULL;
	}

	HAL_Memcpy(&chip->cfg, &_GD25Q16X_FlashChipCfg, sizeof(FlashChipCfg));

	chip->mPageSize = 256;
	chip->mFlashStatus = 0;
	chip->mDummyCount = 1;

	return chip;
}

FlashChipCtor GD25Q16X_FlashChip = {
	.mJedecId = GD25Q16X_JEDEC,
	.enumerate = GD25Q16X_FlashCtor,
	.init = GD25Q16X_FlashInit,
	.destory = GD25Q16X_FlashDeinit,
};
