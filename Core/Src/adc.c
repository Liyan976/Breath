#include "adc.h"

#define ADC_DMA_DEPTH   16U

ADC_HandleTypeDef hadc1;
DMA_HandleTypeDef hdma_adc1;

static uint16_t s_adc_dma_buf[ADC_DMA_DEPTH][ADC1_CHANNEL_COUNT];

void MX_ADC1_Init(void)
{
  ADC_ChannelConfTypeDef sConfig = {0};

  hadc1.Instance = ADC1;
  hadc1.Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV4;
  hadc1.Init.Resolution = ADC_RESOLUTION_12B;
  hadc1.Init.ScanConvMode = ENABLE;
  hadc1.Init.ContinuousConvMode = ENABLE;
  hadc1.Init.DiscontinuousConvMode = DISABLE;
  hadc1.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
  hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
  hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
  hadc1.Init.NbrOfConversion = ADC1_CHANNEL_COUNT;
  hadc1.Init.DMAContinuousRequests = ENABLE;
  hadc1.Init.EOCSelection = ADC_EOC_SEQ_CONV;
  if (HAL_ADC_Init(&hadc1) != HAL_OK)
  {
    Error_Handler();
  }

  /* Rank 1: PA1 (PWM1_OUT) -> CH1 */
  sConfig.Channel = ADC_CHANNEL_1;
  sConfig.Rank = 1;
  sConfig.SamplingTime = ADC_SAMPLETIME_144CYCLES;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK) Error_Handler();

  /* Rank 2: PC0 (PWM2_OUT) -> CH10 */
  sConfig.Channel = ADC_CHANNEL_10;
  sConfig.Rank = 2;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK) Error_Handler();

  /* Rank 3: PC1 (PWM3_OUT) -> CH11 */
  sConfig.Channel = ADC_CHANNEL_11;
  sConfig.Rank = 3;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK) Error_Handler();

  /* Rank 4: PC2 (PWM_1IN) -> CH12 */
  sConfig.Channel = ADC_CHANNEL_12;
  sConfig.Rank = 4;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK) Error_Handler();

  /* Rank 5: PC3 (PWM_2IN) -> CH13 */
  sConfig.Channel = ADC_CHANNEL_13;
  sConfig.Rank = 5;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK) Error_Handler();

  /* Rank 6: PC4 (PWM_3IN) -> CH14 */
  sConfig.Channel = ADC_CHANNEL_14;
  sConfig.Rank = 6;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK) Error_Handler();

  /* Rank 7: PB1 (BREATH_RECEIVE) -> CH9 */
  sConfig.Channel = ADC_CHANNEL_9;
  sConfig.Rank = 7;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK) Error_Handler();

  HAL_ADC_Start_DMA(&hadc1, (uint32_t *)s_adc_dma_buf, ADC_DMA_DEPTH * ADC1_CHANNEL_COUNT);
}

void HAL_ADC_MspInit(ADC_HandleTypeDef *adcHandle)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  if (adcHandle->Instance == ADC1)
  {
    __HAL_RCC_ADC1_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();

    /* PA1 */
    GPIO_InitStruct.Pin = PWM1_OUT_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    /* PB1 */
    GPIO_InitStruct.Pin = BREATH_RECEIVE_Pin;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    /* PC0..PC4 */
    GPIO_InitStruct.Pin = PWM2_OUT_Pin | PWM3_OUT_Pin | PWM_1IN_Pin | PWM_2IN_Pin | PWM_3IN_Pin;
    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

    /* ADC1 DMA Init: DMA2 Stream0 Channel 0 */
    hdma_adc1.Instance = DMA2_Stream0;
    hdma_adc1.Init.Channel = DMA_CHANNEL_0;
    hdma_adc1.Init.Direction = DMA_PERIPH_TO_MEMORY;
    hdma_adc1.Init.PeriphInc = DMA_PINC_DISABLE;
    hdma_adc1.Init.MemInc = DMA_MINC_ENABLE;
    hdma_adc1.Init.PeriphDataAlignment = DMA_PDATAALIGN_HALFWORD;
    hdma_adc1.Init.MemDataAlignment = DMA_MDATAALIGN_HALFWORD;
    hdma_adc1.Init.Mode = DMA_CIRCULAR;
    hdma_adc1.Init.Priority = DMA_PRIORITY_HIGH;
    hdma_adc1.Init.FIFOMode = DMA_FIFOMODE_DISABLE;
    if (HAL_DMA_Init(&hdma_adc1) != HAL_OK)
    {
      Error_Handler();
    }

    __HAL_LINKDMA(adcHandle, DMA_Handle, hdma_adc1);
  }
}

void HAL_ADC_MspDeInit(ADC_HandleTypeDef *adcHandle)
{
  if (adcHandle->Instance == ADC1)
  {
    __HAL_RCC_ADC1_CLK_DISABLE();
    HAL_GPIO_DeInit(GPIOA, PWM1_OUT_Pin);
    HAL_GPIO_DeInit(GPIOB, BREATH_RECEIVE_Pin);
    HAL_GPIO_DeInit(GPIOC, PWM2_OUT_Pin | PWM3_OUT_Pin | PWM_1IN_Pin | PWM_2IN_Pin | PWM_3IN_Pin);
    HAL_DMA_DeInit(adcHandle->DMA_Handle);
  }
}

uint16_t ADC1_GetChannelRaw(AdcChannelIndex_t idx)
{
  if ((uint32_t)idx >= ADC1_CHANNEL_COUNT) return 0U;
  return s_adc_dma_buf[ADC_DMA_DEPTH - 1U][idx];
}

uint16_t ADC1_GetChannelAvg(AdcChannelIndex_t idx)
{
  uint32_t sum = 0U;
  uint32_t i;
  if ((uint32_t)idx >= ADC1_CHANNEL_COUNT) return 0U;
  for (i = 0U; i < ADC_DMA_DEPTH; i++)
  {
    sum += s_adc_dma_buf[i][idx];
  }
  return (uint16_t)(sum / ADC_DMA_DEPTH);
}
