#include "sd_diskio.h"
#include <string.h>

#define SD_TIMEOUT 30000U

static volatile DSTATUS Stat = STA_NOINIT;

static DSTATUS SD_CheckStatus(BYTE lun)
{
    (void)lun;
    Stat = STA_NOINIT;
    if (BSP_SD_IsDetected() != SD_PRESENT)
    {
        Stat |= STA_NODISK;
    }
    else if (BSP_SD_GetCardState() == SD_TRANSFER_OK)
    {
        Stat &= ~STA_NOINIT;
    }
    return Stat;
}

static DSTATUS SD_initialize(BYTE lun)
{
    Stat = STA_NOINIT;
    if (BSP_SD_IsDetected() != SD_PRESENT)
    {
        Stat |= STA_NODISK;
        return Stat;
    }
    if (BSP_SD_Init() == MSD_OK)
    {
        Stat = SD_CheckStatus(lun);
    }
    return Stat;
}

static DSTATUS SD_status(BYTE lun)
{
    return SD_CheckStatus(lun);
}

static DRESULT SD_read(BYTE lun, BYTE *buff, DWORD sector, UINT count)
{
    uint32_t timeout;
    (void)lun;

    if (BSP_SD_ReadBlocks((uint32_t *)buff, (uint32_t)sector, (uint32_t)count, SD_TIMEOUT) != MSD_OK)
    {
        return RES_ERROR;
    }
    timeout = HAL_GetTick();
    while (BSP_SD_GetCardState() != SD_TRANSFER_OK)
    {
        if ((HAL_GetTick() - timeout) >= SD_TIMEOUT) return RES_ERROR;
    }
    return RES_OK;
}

#if _USE_WRITE == 1
static DRESULT SD_write(BYTE lun, const BYTE *buff, DWORD sector, UINT count)
{
    uint32_t timeout;
    (void)lun;

    if (BSP_SD_WriteBlocks((uint32_t *)buff, (uint32_t)sector, (uint32_t)count, SD_TIMEOUT) != MSD_OK)
    {
        return RES_ERROR;
    }
    timeout = HAL_GetTick();
    while (BSP_SD_GetCardState() != SD_TRANSFER_OK)
    {
        if ((HAL_GetTick() - timeout) >= SD_TIMEOUT) return RES_ERROR;
    }
    return RES_OK;
}
#endif

#if _USE_IOCTL == 1
static DRESULT SD_ioctl(BYTE lun, BYTE cmd, void *buff)
{
    DRESULT res = RES_ERROR;
    BSP_SD_CardInfo CardInfo;
    (void)lun;

    if (Stat & STA_NOINIT) return RES_NOTRDY;

    switch (cmd)
    {
        case CTRL_SYNC:
            res = RES_OK;
            break;
        case GET_SECTOR_COUNT:
            BSP_SD_GetCardInfo(&CardInfo);
            *(DWORD *)buff = CardInfo.LogBlockNbr;
            res = RES_OK;
            break;
        case GET_SECTOR_SIZE:
            BSP_SD_GetCardInfo(&CardInfo);
            *(WORD *)buff = (WORD)CardInfo.LogBlockSize;
            res = RES_OK;
            break;
        case GET_BLOCK_SIZE:
            BSP_SD_GetCardInfo(&CardInfo);
            *(DWORD *)buff = CardInfo.LogBlockSize / 512U;
            res = RES_OK;
            break;
        default:
            res = RES_PARERR;
            break;
    }
    return res;
}
#endif

const Diskio_drvTypeDef SD_Driver =
{
    SD_initialize,
    SD_status,
    SD_read,
#if _USE_WRITE == 1
    SD_write,
#endif
#if _USE_IOCTL == 1
    SD_ioctl,
#endif
};
