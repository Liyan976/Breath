#ifndef __BSP_POWER_H
#define __BSP_POWER_H

#include "main.h"

#ifdef __cplusplus
extern "C" {
#endif

#define POWER_KEY_LONG_PRESS_MS     1500U

typedef void (*PowerOffRequestCallback_t)(void);

void BSP_Power_Init(PowerOffRequestCallback_t off_cb);
void BSP_Power_LatchOn(void);
void BSP_Power_ShutdownNow(void);
uint8_t BSP_Power_IsKeyPressed(void);
void BSP_Power_Poll10ms(uint32_t now_ms);

/* Note: Hardware schematic does not wire VBAT to an MCU ADC pin;
 * returns nominal 100% unless an external/fuel-gauge source updates it.
 */
uint8_t BSP_Power_GetBatteryPercent(void);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_POWER_H */
