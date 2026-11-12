#ifndef __BSP_AUDIO_H
#define __BSP_AUDIO_H

#include "main.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AUDIO_VOL_UI_MAX    10U
#define AUDIO_VOL_HW_MAX    30U

typedef enum {
    AUDIO_VOICE_WELCOME = 1,
    AUDIO_VOICE_SEL_BREATH_MODE,
    AUDIO_VOICE_SEL_STIM_MODE,
    AUDIO_VOICE_BIO_CALIB,
    AUDIO_VOICE_BIO_RESULT,
    AUDIO_VOICE_START_TREAT,
    AUDIO_VOICE_INHALE,
    AUDIO_VOICE_EXHALE,
    AUDIO_VOICE_TREAT_END,
    AUDIO_VOICE_POWER_OFF_PROMPT,
    AUDIO_VOICE_LOW_BATTERY,
    AUDIO_VOICE_ELECTRODE_LOST,
    AUDIO_VOICE_FAULT,
    AUDIO_VOICE_BGM_RELAX
} AudioVoiceId_t;

typedef struct {
    uint8_t busy;
    uint8_t headphone_inserted;
    uint8_t last_ack_cmd;
    uint8_t online;
} AudioStatus_t;

void BSP_Audio_Init(void);
void BSP_Audio_OnUartRxCplt(void);
void BSP_Audio_Poll(void);

void BSP_Audio_SetVolume(uint8_t ui_vol_0_to_10);
void BSP_Audio_PlayTrack(uint16_t track_id);
void BSP_Audio_PlayVoice(AudioVoiceId_t id);
void BSP_Audio_Stop(void);
void BSP_Audio_Pause(void);

uint8_t BSP_Audio_IsBusy(void);
uint8_t BSP_Audio_IsHeadphoneInserted(void);
void BSP_Audio_GetStatus(AudioStatus_t *out);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_AUDIO_H */
