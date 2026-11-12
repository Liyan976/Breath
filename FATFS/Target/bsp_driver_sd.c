#include "bsp_driver_sd.h"
#include "main.h"
#include "sdio.h"

uint8_t BSP_SD_Init(void)
{
    uint8_t sd_state = MSD_OK;

    if (BSP_SD_IsDetected() != SD_PRESENT)
    {
        return MSD_ERROR_SD_NOT_PRESENT;
    }

    if (HAL_SD_Init(&hsd) != HAL_OK)
    {
        sd_state = MSD_ERROR;
    }

    if (sd_state == MSD_OK)
    {
        if (HAL_SD_ConfigWideBusOperation(&hsd, SDIO_BUS_WIDE_4B) != HAL_OK)
        {
            sd_state = MSD_ERROR;
        }
    }

    return sd_state;
}

uint8_t BSP_SD_GetCardState(void)
{
    return ((HAL_SD_GetCardState(&hsd) == HAL_SD_CARD_TRANSFER) ? SD_TRANSFER_OK : SD_TRANSFER_BUSY);
}

uint8_t BSP_SD_ReadBlocks(uint32_t *pData, uint32_t ReadAddr, uint32_t NumOfBlocks, uint32_t Timeout)
{
    if (HAL_SD_ReadBlocks(&hsd, (uint8_t *)pData, ReadAddr, NumOfBlocks, Timeout) != HAL_OK)
    {
        return MSD_ERROR;
    }
    return MSD_OK;
}

uint8_t BSP_SD_WriteBlocks(uint32_t *pData, uint32_t WriteAddr, uint32_t NumOfBlocks, uint32_t Timeout)
{
    if (HAL_SD_WriteBlocks(&hsd, (uint8_t *)pData, WriteAddr, NumOfBlocks, Timeout) != HAL_OK)
    {
        return MSD_ERROR;
    }
    return MSD_OK;
}

uint8_t BSP_SD_Erase(uint32_t StartAddr, uint32_t EndAddr)
{
    if (HAL_SD_Erase(&hsd, StartAddr, EndAddr) != HAL_OK)
    {
        return MSD_ERROR;
    }
    return MSD_OK;
}

void BSP_SD_GetCardInfo(BSP_SD_CardInfo *CardInfo)
{
    HAL_SD_GetCardInfo(&hsd, CardInfo);
}

uint8_t BSP_SD_IsDetected(void)
{
    return (HAL_GPIO_ReadPin(SDIO_CD_GPIO_Port, SDIO_CD_Pin) == GPIO_PIN_RESET) ? SD_PRESENT : SD_NOT_PRESENT;
}
