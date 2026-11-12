#include "main.h"
#include "cmsis_os.h"
#include "adc.h"
#include "dma.h"
#include "fatfs.h"
#include "sdio.h"
#include "tim.h"
#include "usart.h"
#include "gpio.h"

void SystemClock_Config(void);
void MX_FREERTOS_Init(void);

int main(void)
{
  HAL_Init();
  SystemClock_Config();

  MX_GPIO_Init();
  MX_DMA_Init();
  MX_ADC1_Init();
  MX_TIM2_Init();
  MX_TIM3_Init();
  MX_TIM4_Init();
  MX_TIM5_Init();
  MX_TIM12_Init();
  MX_TIM14_Init();
  MX_USART1_UART_Init();
  MX_USART2_UART_Init();
  MX_USART3_UART_Init();
  MX_SDIO_SD_Init();
  MX_FATFS_Init();

  MX_FREERTOS_Init();
  osKernelStart();

  while (1)
  {
  }
}

void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  /* HSE = 8 MHz -> SYSCLK = 168 MHz, SDIO/USB = 48 MHz */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 4;
  RCC_OscInitStruct.PLL.PLLN = 168;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 7;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
                                RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;
  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5) != HAL_OK)
  {
    Error_Handler();
  }
}

void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  extern void BSP_Stim_PulseTickISR(void);

  if (htim->Instance == TIM6)
  {
    HAL_IncTick();
  }
  else if (htim->Instance == TIM2)
  {
    BSP_Stim_PulseTickISR();
  }
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
  extern void BSP_Screen_OnUartRxCplt(void);
  extern void BSP_BLE_OnUartRxCplt(void);
  extern void BSP_Audio_OnUartRxCplt(void);

  if (huart->Instance == USART3)
  {
    BSP_Screen_OnUartRxCplt();
  }
  else if (huart->Instance == USART1)
  {
    BSP_BLE_OnUartRxCplt();
  }
  else if (huart->Instance == USART2)
  {
    BSP_Audio_OnUartRxCplt();
  }
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
  extern void BSP_Screen_OnUartError(void);

  if (huart->Instance == USART3)
  {
    BSP_Screen_OnUartError();
  }
}

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
  extern void BSP_BLE_OnDataReadyExti(void);

  if (GPIO_Pin == BLE_INT_Pin)
  {
    BSP_BLE_OnDataReadyExti();
  }
}

void Error_Handler(void)
{
  __disable_irq();
  while (1)
  {
  }
}
