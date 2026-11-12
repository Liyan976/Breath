#include "gpio.h"

void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();

  /* Latch PWR_ON (PA12) HIGH immediately to sustain system power */
  HAL_GPIO_WritePin(PWR_ON_GPIO_Port, PWR_ON_Pin, GPIO_PIN_SET);

  /* Ensure all H-Bridge outputs start safely LOW */
  HAL_GPIO_WritePin(GPIOB, S11_Pin | S12_Pin | S21_Pin | S22_Pin | S31_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(S32_GPIO_Port, S32_Pin, GPIO_PIN_RESET);

  /* BLE control pins safe defaults: RST=HIGH (not reset), WKP=HIGH, MOD=HIGH (transparent) */
  HAL_GPIO_WritePin(GPIOB, BLE_RST_Pin | BLE_WKP_Pin | BLE_MOD_Pin, GPIO_PIN_SET);

  /* PA12: PWR_ON output */
  GPIO_InitStruct.Pin = PWR_ON_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(PWR_ON_GPIO_Port, &GPIO_InitStruct);

  /* PA11 (P_SW1), PA4 (HP_DET), PA5 (BREATH_DET), PA6 (BLE_LINK), PA8 (HP_MIC) inputs */
  GPIO_InitStruct.Pin = P_SW1_Pin | HP_DET_Pin | BREATH_DET_Pin | BLE_LINK_Pin | HP_MIC_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /* PB9, PB5, PB12, PB13, PB14 (H-Bridge), PB7 (BLE_RST), PB3 (BLE_WKP), PB4 (BLE_MOD) outputs */
  GPIO_InitStruct.Pin = S11_Pin | S12_Pin | S21_Pin | S22_Pin | S31_Pin |
                        BLE_RST_Pin | BLE_WKP_Pin | BLE_MOD_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /* PC5: S32 H-Bridge output */
  GPIO_InitStruct.Pin = S32_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(S32_GPIO_Port, &GPIO_InitStruct);

  /* PB0 (SDIO_CD), PB8 (AUDIO_BUSY) inputs */
  GPIO_InitStruct.Pin = SDIO_CD_Pin | AUDIO_BUSY_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /* PB2: BLE_INT EXTI2 rising/falling edge interrupt */
  GPIO_InitStruct.Pin = BLE_INT_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING_FALLING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(BLE_INT_GPIO_Port, &GPIO_InitStruct);

  HAL_NVIC_SetPriority(BLE_INT_EXTI_IRQn, 6, 0);
  HAL_NVIC_EnableIRQ(BLE_INT_EXTI_IRQn);
}
