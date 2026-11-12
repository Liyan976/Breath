#include "dma.h"

void MX_DMA_Init(void)
{
  /* DMA controller clock enable */
  __HAL_RCC_DMA2_CLK_ENABLE();

  /* DMA2_Stream0_IRQn (ADC1) */
  HAL_NVIC_SetPriority(DMA2_Stream0_IRQn, 6, 0);
  HAL_NVIC_EnableIRQ(DMA2_Stream0_IRQn);

  /* DMA2_Stream3_IRQn (SDIO RX) */
  HAL_NVIC_SetPriority(DMA2_Stream3_IRQn, 6, 0);
  HAL_NVIC_EnableIRQ(DMA2_Stream3_IRQn);

  /* DMA2_Stream6_IRQn (SDIO TX) */
  HAL_NVIC_SetPriority(DMA2_Stream6_IRQn, 6, 0);
  HAL_NVIC_EnableIRQ(DMA2_Stream6_IRQn);
}
