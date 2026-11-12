#ifndef __BSP_BREATH_SENSOR_H
#define __BSP_BREATH_SENSOR_H

#include "main.h"

#ifdef __cplusplus
extern "C" {
#endif

void     BSP_BreathSensor_Init(void);
uint8_t  BSP_BreathSensor_IsInserted(void);
uint16_t BSP_BreathSensor_ReadRaw(void);
float    BSP_BreathSensor_ReadVoltageMv(void);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_BREATH_SENSOR_H */
