#ifndef __ADC_H__
#define __ADC_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

/**
 * ADC1 DMA regular channel scan indices:
 *   [0] PA1  (ADC_CHANNEL_1)  : PWM1_OUT (Boost CH1 voltage feedback)
 *   [1] PC0  (ADC_CHANNEL_10) : PWM2_OUT (Boost CH2 voltage feedback)
 *   [2] PC1  (ADC_CHANNEL_11) : PWM3_OUT (Boost CH3 voltage feedback)
 *   [3] PC2  (ADC_CHANNEL_12) : PWM_1IN  (CH1 electrode drop detect)
 *   [4] PC3  (ADC_CHANNEL_13) : PWM_2IN  (CH2 electrode drop detect)
 *   [5] PC4  (ADC_CHANNEL_14) : PWM_3IN  (CH3 electrode drop detect)
 *   [6] PB1  (ADC_CHANNEL_9)  : BREATH_RECEIVE (NTC breath signal)
 */
#define ADC1_CHANNEL_COUNT      7U

typedef enum {
    ADC_IDX_BOOST1_FB = 0,
    ADC_IDX_BOOST2_FB = 1,
    ADC_IDX_BOOST3_FB = 2,
    ADC_IDX_ELEC1_IN  = 3,
    ADC_IDX_ELEC2_IN  = 4,
    ADC_IDX_ELEC3_IN  = 5,
    ADC_IDX_BREATH    = 6
} AdcChannelIndex_t;

extern ADC_HandleTypeDef hadc1;
extern DMA_HandleTypeDef hdma_adc1;

void MX_ADC1_Init(void);
uint16_t ADC1_GetChannelRaw(AdcChannelIndex_t idx);
uint16_t ADC1_GetChannelAvg(AdcChannelIndex_t idx);

#ifdef __cplusplus
}
#endif
#endif /* __ADC_H__ */
