#ifndef __APP_TYPES_H
#define __APP_TYPES_H

#include <stdint.h>
#include "algo_breath.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BREATH_MODE_OFF         = 0,
    BREATH_MODE_SIMPLE      = 1,
    BREATH_MODE_BIOFEEDBACK = 2
} BreathMode_t;

typedef enum {
    STIM_MODE_OFF           = 0,
    STIM_MODE_CONTINUOUS    = 1,
    STIM_MODE_INTERMITTENT  = 2,
    STIM_MODE_DENSE_DISPERSE = 3
} StimMode_t;

typedef enum {
    APP_STATE_MENU = 0,
    APP_STATE_SELECT_BREATH,
    APP_STATE_SELECT_STIM,
    APP_STATE_BIO_CALIB,
    APP_STATE_READY,
    APP_STATE_RUNNING,
    APP_STATE_PAUSED,
    APP_STATE_FINISHED,
    APP_STATE_SETTINGS,
    APP_STATE_FAULT
} AppSysState_t;

#define APP_TIMER_MIN_DEFAULT   10U
#define APP_TIMER_MIN_MIN       5U
#define APP_TIMER_MIN_MAX       30U
#define APP_VOL_DEFAULT         5U
#define APP_VOL_MAX             10U
#define APP_FINISH_AUTO_OFF_MS  10000U

typedef struct {
    BreathMode_t   Breath_Mode;
    StimMode_t     Stim_Mode;
    AppSysState_t  sys_state;

    uint8_t        stim_freq_hz;
    uint8_t        stim_level[3];
    uint8_t        timer_min;
    uint8_t        volume;
    uint8_t        ble_enabled;

    uint8_t        running;
    uint8_t        paused;
    uint32_t       active_elapsed_ms;
    uint32_t       finish_tick_ms;

    BreathPhase_t  breath_phase;
    uint8_t        lung_pct;
    uint8_t        guide_bpm;
    uint8_t        real_cbpm;
    uint8_t        elec_drop_mask;
} AppContext_t;

extern AppContext_t g_app;

#ifdef __cplusplus
}
#endif

#endif /* __APP_TYPES_H */
