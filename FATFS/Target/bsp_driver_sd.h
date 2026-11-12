#ifndef __BSP_DRIVER_SD_H
#define __BSP_DRIVER_SD_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f4xx_hal.h"

#define BSP_SD_CardInfo HAL_SD_CardInfoTypeDef

#define MSD_OK                  ((uint8_t)0x00U)
#define MSD_ERROR               ((uint8_t)0x01U)
#define MSD_ERROR_SD_NOT_PRESENT ((uint8_t)0x02U)

#define SD_TRANSFER_OK          ((uint8_t)0x00U)
#define SD_TRANSFER_BUSY        ((uint8_t)0x01U)

#define SD_PRESENT              ((uint8_t)0x01U)
#define SD_NOT_PRESENT          ((uint8_t)0x00U)

#define SD_DATATIMEOUT          ((uint32_t)100000000U)

uint8_t BSP_SD_Init(void);
uint8_t BSP_SD_GetCardState(void);
uint8_t BSP_SD_ReadBlocks(uint32_t *pData, uint32_t ReadAddr, uint32_t NumOfBlocks, uint32_t Timeout);
uint8_t BSP_SD_WriteBlocks(uint32_t *pData, uint32_t WriteAddr, uint32_t NumOfBlocks, uint32_t Timeout);
uint8_t BSP_SD_Erase(uint32_t StartAddr, uint32_t EndAddr);
void    BSP_SD_GetCardInfo(BSP_SD_CardInfo *CardInfo);
uint8_t BSP_SD_IsDetected(void);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_DRIVER_SD_H */
