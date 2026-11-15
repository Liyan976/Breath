#ifndef __THERAPY_CTRL_H
#define __THERAPY_CTRL_H

#include "app_types.h"
#include "bsp_ble.h"

#ifdef __cplusplus
extern "C" {
#endif

void App_Therapy_Init(void);

/* Menu & mode selection handlers (called from ScreenTask switch-case) */
void App_Therapy_EnterBreathMenu(uint8_t is_combo);
void App_Therapy_EnterStimMenu(void);
void App_Therapy_SelectBreathMode(BreathMode_t mode);
void App_Therapy_SelectStimMode(StimMode_t mode);
void App_Therapy_EnterSettings(void);
void App_Therapy_ReturnFromSettings(void);
void App_Therapy_StartOrPause(void);
void App_Therapy_Pause(void);
void App_Therapy_Resume(void);
void App_Therapy_StopToMenu(void);
void App_Therapy_PowerOffRequest(void);

/* Direct mode launch handlers from HMI Start Button (page-specific parameter packet) */
void App_Therapy_StartSimpleBreath(uint8_t timer_min, uint8_t volume, uint8_t ble_en);
void App_Therapy_StartBiofeedbackBreath(uint8_t timer_min, uint8_t volume, uint8_t ble_en);
void App_Therapy_StartPureStim(uint8_t mode_sel, uint8_t freq_hz, uint8_t ch1, uint8_t ch2, uint8_t ch3, uint8_t timer_min);
void App_Therapy_StartCombo(uint8_t breath_mode, uint8_t mode_sel, uint8_t freq_hz, uint8_t ch1, uint8_t ch2, uint8_t ch3, uint8_t timer_min);

/* Parameter setters (called from ScreenTask & BleTask) */
void App_Therapy_SetVolume(uint8_t vol_0_to_10);
void App_Therapy_SetTimerMin(uint8_t timer_min);
void App_Therapy_SetBleEnable(uint8_t enable);
void App_Therapy_SetStimFreq(uint8_t freq_hz);
void App_Therapy_SetStimLevel(uint8_t ch, uint8_t level_0_to_10);

/* Periodic ticks from RTOS tasks */
void App_Therapy_UnifiedTimeAxisStep20ms(uint32_t now_ms);
void App_Therapy_MainTick10ms(uint32_t now_ms);

/* BLE protocol callback */
void App_Therapy_OnBleCommand(const BleRxCommand_t *cmd);

#ifdef __cplusplus
}
#endif

#endif /* __THERAPY_CTRL_H */
