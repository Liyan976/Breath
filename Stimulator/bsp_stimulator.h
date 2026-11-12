#ifndef __BSP_STIMULATOR_H
#define __BSP_STIMULATOR_H

#include "main.h"

#ifdef __cplusplus
extern "C" {
#endif

#define STIM_CH_COUNT               3U
#define STIM_LEVEL_MAX              10U
#define STIM_FREQ_MIN_HZ            1U
#define STIM_FREQ_MAX_HZ            100U
#define STIM_PEAK_CURRENT_MAX_MA    30.0f
#define STIM_BOOST_MAX_MV           30000U
#define STIM_BOOST_MIN_MV           5000U
#define STIM_ELEC_DROP_DEBOUNCE_MS  5000U
#define STIM_ELEC_RECOVER_RAMP_MS   3000U

typedef enum {
    STIM_WAVE_CONT = 0,          /* Continuous wave */
    STIM_WAVE_INTERMITTENT = 1,  /* Intermittent (3:1 work:pause in standalone; sparse/dense in combo) */
    STIM_WAVE_DENSE_SPARSE = 2   /* Dense-disperse (1:5 freq, 2:1 sparse:dense; inhale-only in combo) */
} StimWaveType_t;

typedef struct {
    uint8_t  target_level[STIM_CH_COUNT];
    uint8_t  effective_level[STIM_CH_COUNT];
    uint8_t  electrode_dropped[STIM_CH_COUNT];
    uint16_t boost_mv[STIM_CH_COUNT];
    uint16_t elec_adc_mv[STIM_CH_COUNT];
    uint8_t  active_freq_hz;
    uint8_t  output_enabled;
} StimStatus_t;

void BSP_Stim_Init(void);
void BSP_Stim_Enable(uint8_t enable);
void BSP_Stim_EmergencyStop(void);

void BSP_Stim_SetChannelLevel(uint8_t ch, uint8_t level);
void BSP_Stim_SetAllLevels(const uint8_t levels[STIM_CH_COUNT]);
void BSP_Stim_SetWaveType(StimWaveType_t wave);
void BSP_Stim_SetBaseFreqHz(uint8_t freq_hz);
void BSP_Stim_SetRuntimeFreqHz(uint8_t freq_hz); /* Used by combined mode / dense-sparse modulator */
void BSP_Stim_SetComboOverride(uint8_t enable_override, uint8_t gate_on, uint8_t runtime_freq_hz);

/* Call periodically (e.g. every 10 ms) from Stim_Task for Boost PI loop, ramp, and electrode check */
void BSP_Stim_UpdateLoop10ms(uint32_t now_ms);

/* Called from TIM2 50 us ISR */
void BSP_Stim_PulseTickISR(void);

void BSP_Stim_GetStatus(StimStatus_t *out);
uint8_t BSP_Stim_IsElectrodeDropped(uint8_t ch);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_STIMULATOR_H */
