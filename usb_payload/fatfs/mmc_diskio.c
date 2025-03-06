#include "ff.h"
#include "diskio.h"
#include "stm32h7xx_hal.h"
#include "main.h"

// MMC status
DSTATUS MMC_disk_status(void)
{
    if (HAL_MMC_GetCardState(&MMCHandle) == HAL_MMC_CARD_TRANSFER)
        return 0; // Status OK
    return STA_NOINIT;
}

// MMC initialization
DSTATUS MMC_disk_initialize(void)
{
    // MMC should already be initialized in the main initialization
    // Just check if it's ready
    if (HAL_MMC_GetCardState(&MMCHandle) == HAL_MMC_CARD_TRANSFER)
        return 0; // Status OK
    return STA_NOINIT;
}

// MMC read function
DRESULT MMC_disk_read(BYTE *buff, LBA_t sector, UINT count)
{
    if (HAL_MMC_ReadBlocks(&MMCHandle, (uint8_t *)buff, sector, count, 1000) == HAL_OK) {
        // Wait for DMA to complete
        uint32_t timeout = HAL_GetTick() + 1000;
        while (HAL_MMC_GetCardState(&MMCHandle) != HAL_MMC_CARD_TRANSFER) {
            if (HAL_GetTick() >= timeout)
                return RES_ERROR;
        }
        return RES_OK;
    }
    return RES_ERROR;
}

// MMC write function
DRESULT MMC_disk_write(const BYTE *buff, LBA_t sector, UINT count)
{
    if (HAL_MMC_WriteBlocks(&MMCHandle, (uint8_t *)buff, sector, count, 1000) == HAL_OK) {
        // Wait for DMA to complete
        uint32_t timeout = HAL_GetTick() + 1000;
        while (HAL_MMC_GetCardState(&MMCHandle) != HAL_MMC_CARD_TRANSFER) {
            if (HAL_GetTick() >= timeout)
                return RES_ERROR;
        }
        return RES_OK;
    }
    return RES_ERROR;
}

// MMC ioctl function
DRESULT MMC_disk_ioctl(BYTE cmd, void *buff)
{
    switch (cmd) {
        case CTRL_SYNC:
            return RES_OK;
        
        case GET_SECTOR_COUNT:
            *(DWORD *)buff = MMCHandle.MmcCard.LogBlockNbr;
            return RES_OK;
        
        case GET_SECTOR_SIZE:
            *(WORD *)buff = MMCHandle.MmcCard.LogBlockSize;
            return RES_OK;
        
        case GET_BLOCK_SIZE:
            *(DWORD *)buff = MMCHandle.MmcCard.LogBlockSize / 512;
            return RES_OK;
    }
    return RES_PARERR;
}

// RAM and USB are not used in this project, so provide stub implementations

DSTATUS RAM_disk_status(void) { return STA_NOINIT; }
DSTATUS RAM_disk_initialize(void) { return STA_NOINIT; }
DRESULT RAM_disk_read(BYTE *buff, LBA_t sector, UINT count) { return RES_ERROR; }
DRESULT RAM_disk_write(const BYTE *buff, LBA_t sector, UINT count) { return RES_ERROR; }

DSTATUS USB_disk_status(void) { return STA_NOINIT; }
DSTATUS USB_disk_initialize(void) { return STA_NOINIT; }
DRESULT USB_disk_read(BYTE *buff, LBA_t sector, UINT count) { return RES_ERROR; }
DRESULT USB_disk_write(const BYTE *buff, LBA_t sector, UINT count) { return RES_ERROR; }

// Time function implementation
DWORD get_fattime(void)
{
    // Return a fixed date if no RTC is available: 2024-01-01 12:00:00
    return ((DWORD)(2024 - 1980) << 25) | // Year (0-127, from 1980)
           ((DWORD)1 << 21) |             // Month (1-12)
           ((DWORD)1 << 16) |             // Day (1-31)
           ((DWORD)12 << 11) |            // Hour (0-23)
           ((DWORD)0 << 5) |              // Minute (0-59)
           ((DWORD)0 >> 1);               // Second (0-29, 2-second resolution)
}