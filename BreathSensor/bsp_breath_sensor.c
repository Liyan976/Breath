#include "bsp_breath_sensor.h"
#include "adc.h"

void BSP_BreathSensor_Init(void)
{
    /* PB1 (ADC1_IN9) and PA5 (BREATH_DET) initialized in MX_ADC1_Init / MX_GPIO_Init */
}

uint8_t BSP_BreathSensor_IsInserted(void)
{
    /* BREATH_DET (PA5) has 100k pull-down R1; jack insertion pulls HIGH */
    return (HAL_GPIO_ReadPin(BREATH_DET_GPIO_Port, BREATH_DET_Pin) == GPIO_PIN_SET) ? 1U : 0U;
}

uint16_t BSP_BreathSensor_ReadRaw(void)
{
    return ADC1_GetChannelAvg(ADC_IDX_BREATH);
}

float BSP_BreathSensor_ReadVoltageMv(void)
{
    return ((float)BSP_BreathSensor_ReadRaw() * 3300.0f) / 4095.0f;
}
