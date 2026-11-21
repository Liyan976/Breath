#ifndef __ALGO_BREATH_H
#define __ALGO_BREATH_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BREATH_PHASE_IDLE = 0,
    BREATH_PHASE_INHALE = 1,
    BREATH_PHASE_EXHALE = 2
} BreathPhase_t;

typedef struct {
    BreathPhase_t phase;
    uint8_t       phase_changed; /* 1 on the tick a new inhale/exhale phase starts */
    uint8_t       lung_pct;      /* 0..100 visual fill level */
    uint8_t       guide_bpm;     /* Active guidance frequency (5..30 BPM) */
    uint8_t       real_cbpm;     /* Latest detected user CBPM */
    uint8_t       calib_ready;   /* 1 once initial 3-breath GBPM0 is computed */
} BreathGuideOut_t;

void Algo_Breath_Init(void);
void Algo_Breath_ResetBioCalib(void);

/* Feed 50 Hz (20 ms) NTC ADC sample for biofeedback valley/CBPM/GBPM calculation */
void Algo_Breath_FeedSample(uint16_t raw_adc, uint32_t now_ms);

/* Simple mode step-down lookup (10 -> 5 BPM) */
uint8_t Algo_Breath_GetSimpleBPM(uint32_t active_elapsed_ms);

/* Start/reset unified 4:6 guide time-axis */
void Algo_Breath_StartGuide(uint8_t initial_bpm);
void Algo_Breath_StopGuide(void);

/* Advance unified time-axis by dt_ms; guarantees 4:6 inhale/exhale sync */
void Algo_Breath_StepGuide(uint32_t dt_ms, uint8_t target_bpm, BreathGuideOut_t *out);

uint8_t Algo_Breath_GetBioGBPM(void);
void    Algo_Breath_SetBioGBPM(uint8_t bpm);
uint8_t Algo_Breath_GetRealCBPM(void);
uint8_t Algo_Breath_IsBioCalibReady(void);
uint8_t Algo_Breath_IsSignalDetected(void);

#ifdef __cplusplus
}
#endif

#endif /* __ALGO_BREATH_H */
