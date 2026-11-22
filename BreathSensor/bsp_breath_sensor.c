#include "bsp_breath_sensor.h"
#include "adc.h"

void BSP_BreathSensor_Init(void)
{
    /* PB1 (ADC1_IN9) initialized in MX_ADC1_Init */
}

uint8_t BSP_BreathSensor_IsInserted(void)
{
    /* 硬件改板说明：呼吸传感器插座上的 Breath_Det 引脚已物理割线去除，单片机 PA5 引脚已不发挥检测作用。
     * 故此处恒返回 1，系统无需判断插拔状态，直接由 PB1 (BREATH_RECEIVE) 采集呼吸模拟信号运行算法。 */
    return 1U;
}

uint16_t BSP_BreathSensor_ReadRaw(void)
{
    return ADC1_GetChannelAvg(ADC_IDX_BREATH);
}

float BSP_BreathSensor_ReadVoltageMv(void)
{
    return ((float)BSP_BreathSensor_ReadRaw() * 3300.0f) / 4095.0f;
}
