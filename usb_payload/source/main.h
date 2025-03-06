#pragma once

#include <stm32h7xx_hal.h>
#include <stm32h7xx_hal_sai.h>
#include "stm32h7xx_hal_mmc.h"

extern SRAM_HandleTypeDef fmcHandle;
extern TIM_HandleTypeDef tim3Handle;
extern MDMA_HandleTypeDef mdmaHandle;
extern ADC_HandleTypeDef adcHandle;
extern DMA_HandleTypeDef dmaHandle;
extern ADC_HandleTypeDef adc2Handle;
extern DMA_HandleTypeDef dma2Handle;
extern SAI_HandleTypeDef hsaiHandle;
extern MMC_HandleTypeDef MMCHandle;
//extern MMC_HandleTypeDef hmmc;