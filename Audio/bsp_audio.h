#ifndef __BSP_AUDIO_H
#define __BSP_AUDIO_H

#include "main.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AUDIO_VOL_UI_MAX    10U
#define AUDIO_VOL_HW_MAX    30U

typedef enum {
    AUDIO_VOICE_WELCOME             = 1,  /**< 0001: 欢迎使用呼吸调控仪，请选择治疗模式 */
    AUDIO_VOICE_SEL_BREATH_MODE     = 2,  /**< 0002: 请选择呼吸引导模式 */
    AUDIO_VOICE_INHALE              = 3,  /**< 0003: 吸 */
    AUDIO_VOICE_EXHALE              = 4,  /**< 0004: 呼 */
    AUDIO_VOICE_START_TREAT         = 5,  /**< 0005: 开始治疗 */
    AUDIO_VOICE_PAUSE_TREAT         = 6,  /**< 0006: 暂停治疗 */
    AUDIO_VOICE_BIO_CALIB           = 7,  /**< 0007: 请佩戴好呼吸传感器并根据提示呼吸... */
    AUDIO_VOICE_BIO_PREFIX          = 8,  /**< 0008: 本次你的起始呼吸引导频率为 */
    AUDIO_VOICE_BIO_RESULT          = 8,  /**< 兼容旧命名 */
    AUDIO_VOICE_CLICK_START_PROMPT  = 9,  /**< 0009: 请点击启停键或右下角设置按钮设置好参数后开启治疗 */
    AUDIO_VOICE_TREAT_END_PREFIX    = 10, /**< 0010: 本次治疗结束，有效时长为 */
    AUDIO_VOICE_TREAT_END           = 10, /**< 兼容旧命名 */
    AUDIO_VOICE_SEL_STIM_MODE       = 11, /**< 0011: 请选择电刺激模式 */
    AUDIO_VOICE_POWER_OFF_PROMPT    = 12, /**< 0012: 本次治疗结束，请关闭仪器 */
    AUDIO_VOICE_BIO_FALLBACK        = 13, /**< 0013: 本次你的起始呼吸引导频率为 5 次每分钟... */

    /* 辅助告警音（46以上，避免与14~45参数音轨冲突） */
    AUDIO_VOICE_ELECTRODE_LOST      = 46,
    AUDIO_VOICE_LOW_BATTERY         = 47,
    AUDIO_VOICE_FAULT               = 48,
    AUDIO_VOICE_BGM_RELAX           = 49
} AudioVoiceId_t;

/* 频率与定时参数播报常量定义 */
#define AUDIO_TRACK_FREQ_BASE       14U   /**< 0014: 5次每分钟 (14~19对应5~10次/分) */
#define AUDIO_TRACK_TIME_BASE       20U   /**< 0020: 5分钟 (20~45对应5~30分钟) */

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
void BSP_Audio_PlayQueue(const uint16_t *tracks, uint8_t count);
void BSP_Audio_PlayBioResult(uint8_t bpm);
void BSP_Audio_PlayTreatEnd(uint8_t timer_min);
void BSP_Audio_Stop(void);
void BSP_Audio_Pause(void);

uint8_t BSP_Audio_IsBusy(void);
uint8_t BSP_Audio_IsHeadphoneInserted(void);
void BSP_Audio_GetStatus(AudioStatus_t *out);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_AUDIO_H */
