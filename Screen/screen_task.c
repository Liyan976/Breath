/**
 * @file    screen_task.c
 * @brief   淘晶驰串口屏 FreeRTOS 任务及协议解析模块
 * @note    
 *          
 */
#include "bsp_screen.h"
#include "bsp_audio.h"
#include "bsp_power.h"
#include "therapy_ctrl.h"
#include "cmsis_os.h"
#include <string.h>

/**
 * @brief  统一处理 0x05 上行控制/启动/页面事件帧（按 15 个页面枚举清晰分发）
 * @param  frame: 解析出的完整数据帧（header 固定为 0x05）
 * @retval 无
 */
static void Screen_HandleCtrlFrame(const ScreenFrame_t *frame)
{
    if (frame == NULL || frame->len < 3U) return;

    uint8_t page   = frame->page;       /* raw[1]: 页面号 (0~14，ScreenPageId_t) */
    uint8_t action = frame->control;    /* raw[2]: 动作码 (0x01启动, 0x00暂停, 0x02返回, 0x08页面就绪, 0x10音量, 0x11蓝牙) */

    BSP_Screen_SetCurrentPage(page);

    switch (page)
    {
        /* ================= 1. 模式选择与主菜单页面 ================= */
        case SCREEN_PAGE_START: /* Page 0: start (主界面/开机欢迎页) */
            if (action == 0x08U)
            {
                App_Therapy_StopToMenu();
                App_Therapy_ResetBioCalibrationSession();
                BSP_Audio_PlayVoice(AUDIO_VOICE_WELCOME); /* 0001: 欢迎使用呼吸调控仪... */
            }
            break;

        case SCREEN_PAGE_ELECMODE:    /* Page 1:  elecmode (电刺激模式选择：连续/断续/疏密) */
        case SCREEN_PAGE_LIANHEMODE2: /* Page 10: lianhemode2 (联合模式第2步：选电刺激) */
            if (action == 0x08U)
            {
                App_Therapy_StopToMenu();
                App_Therapy_ResetBioCalibrationSession();
                BSP_Audio_PlayVoice(AUDIO_VOICE_SEL_STIM_MODE); /* 0011: 请选择电刺激模式 */
            }
            break;

        case SCREEN_PAGE_BREATHMODE:  /* Page 2:  breathmode (呼吸引导模式选择：简易/生物反馈) */
            if (action == 0x08U)
            {
                App_Therapy_StopToMenu();
                App_Therapy_ResetBioCalibrationSession();
                BSP_Audio_PlayVoice(AUDIO_VOICE_SEL_BREATH_MODE); /* 0002: 请选择呼吸引导模式 */
            }
            break;

        case SCREEN_PAGE_LIANHEMODE1: /* Page 3:  lianhemode1 (联合模式第1步：选呼吸引导) */
            if (action == 0x08U)
            {
                App_Therapy_StopToMenu();
                App_Therapy_ResetBioCalibrationSession();
                BSP_Audio_PlayVoice(AUDIO_VOICE_SEL_BREATH_MODE); /* 0002: 请选择呼吸引导模式 */
            }
            break;

        /* ================= 2. 5 个治疗测量与运行页面 ================= */
        case SCREEN_PAGE_JIANYI: /* Page 4: jianyi (单纯简易呼吸引导测量运行) */
            if (action == 0x08U) /* 刚进页面：页面加载就绪 */
            {
                /* 禁用屏幕内部定时器 tm0，防止屏幕自带粗糙逻辑反向翻转呼吸箭头 */
                BSP_Screen_SendRawCmd("tm0.en=0");
                /* 同步查询屏幕内部保存的蓝牙开关状态 */
                BSP_Screen_SendRawCmd("get start.bt_en.val");

                if (frame->len >= 4U && frame->raw[3] > 0U)
                {
                    App_Therapy_SetTimerMin(frame->raw[3]);
                }

                if (App_Therapy_IsPausedInPage(SCREEN_PAGE_JIANYI))
                {
                    /* 之前在简易治疗中暂停过，恢复显示之前的剩余时间与频率 */
                    uint32_t total_s = (uint32_t)g_app.timer_min * 60U;
                    uint32_t elap_s  = g_app.active_elapsed_ms / 1000U;
                    uint16_t rem_s   = (total_s > elap_s) ? (uint16_t)(total_s - elap_s) : 0U;
                    BSP_Screen_SetRemainTimeSec(rem_s);
                    BSP_Screen_SetGuideBPM(g_app.guide_bpm);
                    BSP_Screen_SetLungFill(0U);
                    BSP_Screen_SetBreathArrow(0U);
                    BSP_Screen_SetControlBtnPic(0U);
                }
                else
                {
                    /* 全新进入模式：显示初始默认设定值（默认10次/分，初始设定时长如10分钟） */
                    App_Therapy_ResetBioCalibrationSession();
                    g_app.Breath_Mode = BREATH_MODE_SIMPLE;
                    g_app.Stim_Mode   = STIM_MODE_OFF;
                    g_app.guide_bpm   = 10U;
                    BSP_Screen_SetGuideBPM(10U);
                    /* 屏幕工程 codesload 已设置 t0.txt=start.set_time.val+"分钟"，下位机切勿发送 10:00 覆写 */
                    BSP_Screen_SetLungFill(0U);
                    BSP_Screen_SetBreathArrow(0U);
                    BSP_Screen_SetControlBtnPic(0U);
                }
                BSP_Audio_PlayVoice(AUDIO_VOICE_CLICK_START_PROMPT); /* 0009: 请点击启停键... */
            }
            else if (action == 0x01U) /* 启动 / 继续 */
            {
                /* 启动时再次关闭屏幕 tm0 */
                BSP_Screen_SendRawCmd("tm0.en=0");

                if (App_Therapy_IsPausedInPage(SCREEN_PAGE_JIANYI))
                {
                    /* 同模式暂停后继续：时间不归零，从吸气0开始 */
                    App_Therapy_Resume();
                }
                else
                {
                    /* 启动新模式或从头开始：清空旧模式暂停数据，从头开始计时 */
                    uint8_t timer_min = (frame->len >= 4U) ? frame->raw[3] : 10U;
                    uint8_t volume    = (frame->len >= 5U) ? frame->raw[4] : 5U;
                    uint8_t ble_en    = (frame->len >= 6U) ? frame->raw[5] : g_app.ble_enabled;
                    App_Therapy_StartSimpleBreath(timer_min, volume, ble_en);
                }
            }
            else if (action == 0x00U) /* 暂停 */
            {
                App_Therapy_Pause();
                BSP_Screen_SetLungFill(0U);
                BSP_Screen_SetBreathArrow(0U);
                BSP_Screen_SendRawCmd("tm0.en=0");
            }
            else if (action == 0x02U || action == 0x0FU) /* 返回主页 */
            {
                App_Therapy_StopToMenu();
            }
            break;

        case SCREEN_PAGE_JIANYIELEC: /* Page 6: jianyielec (简易呼吸 + 电刺激联合测量运行) */
            if (action == 0x08U)
            {
                /* 禁用屏幕内部定时器 tm0，防止屏幕自带的粗糙呼吸逻辑与下位机算法冲突闪烁 */
                BSP_Screen_SendRawCmd("tm0.en=0");
                BSP_Screen_SendRawCmd("get start.bt_en.val");

                if (frame->len >= 4U && frame->raw[3] > 0U)
                {
                    App_Therapy_SetTimerMin(frame->raw[3]);
                }

                if (App_Therapy_IsPausedInPage(SCREEN_PAGE_JIANYIELEC))
                {
                    uint32_t total_s = (uint32_t)g_app.timer_min * 60U;
                    uint32_t elap_s  = g_app.active_elapsed_ms / 1000U;
                    uint16_t rem_s   = (total_s > elap_s) ? (uint16_t)(total_s - elap_s) : 0U;
                    BSP_Screen_SetRemainTimeSec(rem_s);
                    BSP_Screen_SetGuideBPM(g_app.guide_bpm);
                    BSP_Screen_SetLungFill(0U);
                    BSP_Screen_SetBreathArrow(0U);
                    BSP_Screen_SetControlBtnPic(0U);
                }
                else
                {
                    App_Therapy_ResetBioCalibrationSession();
                    g_app.Breath_Mode = BREATH_MODE_SIMPLE;
                    g_app.guide_bpm   = 10U;
                    BSP_Screen_SetGuideBPM(10U);
                    /* 保持屏幕自身 codesload 加载的初始定时时间，不发送 10:00 覆写 */
                    BSP_Screen_SetLungFill(0U);
                    BSP_Screen_SetBreathArrow(0U);
                    BSP_Screen_SetControlBtnPic(0U);
                }
                BSP_Audio_PlayVoice(AUDIO_VOICE_CLICK_START_PROMPT); /* 0009 */
            }
            else if (action == 0x01U) /* 启动 / 继续 */
            {
                /* 屏幕 b1 启动事件会置位 tm0.en=1，下位机覆写关闭 tm0 */
                BSP_Screen_SendRawCmd("tm0.en=0");

                if (App_Therapy_IsPausedInPage(SCREEN_PAGE_JIANYIELEC))
                {
                    App_Therapy_Resume();
                }
                else if (frame->len >= 8U)
                {
                    uint8_t mode_sel  = frame->raw[3];
                    uint8_t freq_hz   = frame->raw[4];
                    uint8_t ch1       = frame->raw[5];
                    uint8_t ch2       = frame->raw[6];
                    uint8_t ch3       = frame->raw[7];
                    uint8_t timer_min = (frame->len >= 9U) ? frame->raw[8] : 10U;
                    if (freq_hz < 1U) freq_hz = (g_app.stim_freq_hz >= 1U) ? g_app.stim_freq_hz : 20U;
                    App_Therapy_StartCombo(0U /* 简易呼吸 */, mode_sel, freq_hz, ch1, ch2, ch3, timer_min);
                }
                else
                {
                    App_Therapy_StartOrPause();
                }
            }
            else if (action == 0x00U)
            {
                App_Therapy_Pause();
                BSP_Screen_SetLungFill(0U);
                BSP_Screen_SetBreathArrow(0U);
                BSP_Screen_SendRawCmd("tm0.en=0");
            }
            else if (action == 0x02U || action == 0x0FU)
            {
                App_Therapy_StopToMenu();
            }
            break;

        case SCREEN_PAGE_FANKUI: /* Page 7: fankui (单纯生物反馈呼吸测量运行) */
            if (action == 0x08U) /* 刚进页面：页面加载就绪 */
            {
                BSP_Screen_SendRawCmd("tm0.en=0");
                BSP_Screen_SendRawCmd("get start.bt_en.val");

                if (frame->len >= 4U && frame->raw[3] > 0U)
                {
                    App_Therapy_SetTimerMin(frame->raw[3]);
                }

                if (App_Therapy_IsPausedInPage(SCREEN_PAGE_FANKUI))
                {
                    uint32_t total_s = (uint32_t)g_app.timer_min * 60U;
                    uint32_t elap_s  = g_app.active_elapsed_ms / 1000U;
                    uint16_t rem_s   = (total_s > elap_s) ? (uint16_t)(total_s - elap_s) : 0U;
                    BSP_Screen_SetRemainTimeSec(rem_s);
                    BSP_Screen_SetGuideBPM(g_app.guide_bpm);
                    BSP_Screen_SetLungFill(0U);
                    BSP_Screen_SetBreathArrow(0U);
                    BSP_Screen_SetControlBtnPic(0U);
                    BSP_Screen_SendRawCmd("tsw b1,1");
                    BSP_Screen_SendRawCmd("tsw b0,1");
                    BSP_Audio_PlayVoice(AUDIO_VOICE_CLICK_START_PROMPT);
                }
                else if (App_Therapy_IsBioFreqValid() && App_Therapy_GetBioOwner() == SCREEN_PAGE_FANKUI)
                {
                    /* 从设置界面返回：已采集过有效呼吸频率，不需要重新采集，保留频率！ */
                    g_app.Breath_Mode = BREATH_MODE_BIOFEEDBACK;
                    g_app.Stim_Mode   = STIM_MODE_OFF;
                    BSP_Screen_SetGuideBPM(g_app.guide_bpm);
                    BSP_Screen_SetLungFill(0U);
                    BSP_Screen_SetBreathArrow(0U);
                    BSP_Screen_SetControlBtnPic(0U);
                    BSP_Screen_SendRawCmd("tsw b1,1");
                    BSP_Screen_SendRawCmd("tsw b0,1");
                    BSP_Audio_PlayVoice(AUDIO_VOICE_CLICK_START_PROMPT);
                }
                else
                {
                    /* 首次进入或换模式后进入：启动语音引导检测与采集，按键锁定 */
                    g_app.Breath_Mode = BREATH_MODE_BIOFEEDBACK;
                    g_app.Stim_Mode   = STIM_MODE_OFF;
                    BSP_Screen_SetGuideBPM(10U);
                    App_Therapy_StartBioCalib(SCREEN_PAGE_FANKUI);
                }
            }
            else if (action == 0x01U) /* 启动 / 继续 */
            {
                BSP_Screen_SendRawCmd("tm0.en=0");

                /* 采集中禁止响应启动 */
                if (g_app.sys_state == APP_STATE_BIO_CALIB)
                {
                    return;
                }

                if (App_Therapy_IsPausedInPage(SCREEN_PAGE_FANKUI))
                {
                    App_Therapy_Resume();
                }
                else if (frame->len >= 4U)
                {
                    uint8_t timer_min = frame->raw[3];
                    uint8_t volume    = (frame->len >= 5U) ? frame->raw[4] : 5U;
                    uint8_t ble_en    = (frame->len >= 6U) ? frame->raw[5] : g_app.ble_enabled;
                    App_Therapy_StartBiofeedbackBreath(timer_min, volume, ble_en);
                }
                else
                {
                    App_Therapy_StartOrPause();
                }
            }
            else if (action == 0x00U)
            {
                if (g_app.sys_state == APP_STATE_BIO_CALIB)
                {
                    return;
                }
                App_Therapy_Pause();
                BSP_Screen_SetLungFill(0U);
                BSP_Screen_SetBreathArrow(0U);
                BSP_Screen_SendRawCmd("tm0.en=0");
            }
            else if (action == 0x02U || action == 0x0FU)
            {
                App_Therapy_StopToMenu();
                App_Therapy_ResetBioCalibrationSession();
            }
            break;

        case SCREEN_PAGE_FANKUIELEC: /* Page 9: fankuielec (生物反馈呼吸 + 电刺激联合测量运行) */
            if (action == 0x08U)
            {
                /* 禁用屏幕内部定时器 tm0，防止屏幕自带逻辑冲突 */
                BSP_Screen_SendRawCmd("tm0.en=0");
                BSP_Screen_SendRawCmd("get start.bt_en.val");

                if (frame->len >= 4U && frame->raw[3] > 0U)
                {
                    App_Therapy_SetTimerMin(frame->raw[3]);
                }

                if (App_Therapy_IsPausedInPage(SCREEN_PAGE_FANKUIELEC))
                {
                    uint32_t total_s = (uint32_t)g_app.timer_min * 60U;
                    uint32_t elap_s  = g_app.active_elapsed_ms / 1000U;
                    uint16_t rem_s   = (total_s > elap_s) ? (uint16_t)(total_s - elap_s) : 0U;
                    BSP_Screen_SetRemainTimeSec(rem_s);
                    BSP_Screen_SetGuideBPM(g_app.guide_bpm);
                    BSP_Screen_SetStimParams(g_app.stim_freq_hz, g_app.stim_level);
                    BSP_Screen_SetLungFill(0U);
                    BSP_Screen_SetBreathArrow(0U);
                    BSP_Screen_SetControlBtnPic(0U);
                    BSP_Screen_SendRawCmd("tsw b1,1");
                    BSP_Screen_SendRawCmd("tsw b0,1");
                    BSP_Audio_PlayVoice(AUDIO_VOICE_CLICK_START_PROMPT);
                }
                else if (App_Therapy_IsBioFreqValid() && App_Therapy_GetBioOwner() == SCREEN_PAGE_FANKUIELEC)
                {
                    /* 从设置界面返回：已采集过有效呼吸频率，不需要重新采集，保留频率！ */
                    g_app.Breath_Mode = BREATH_MODE_BIOFEEDBACK;
                    BSP_Screen_SetGuideBPM(g_app.guide_bpm);
                    BSP_Screen_SetStimParams(g_app.stim_freq_hz, g_app.stim_level);
                    BSP_Screen_SetLungFill(0U);
                    BSP_Screen_SetBreathArrow(0U);
                    BSP_Screen_SetControlBtnPic(0U);
                    BSP_Screen_SendRawCmd("tsw b1,1");
                    BSP_Screen_SendRawCmd("tsw b0,1");
                    BSP_Audio_PlayVoice(AUDIO_VOICE_CLICK_START_PROMPT);
                }
                else
                {
                    /* 首次进入或换模式后进入：启动语音引导检测与采集，按键锁定 */
                    g_app.Breath_Mode = BREATH_MODE_BIOFEEDBACK;
                    BSP_Screen_SetGuideBPM(10U);
                    BSP_Screen_SetStimParams(g_app.stim_freq_hz, g_app.stim_level);
                    App_Therapy_StartBioCalib(SCREEN_PAGE_FANKUIELEC);
                }
            }
            else if (action == 0x01U) /* 启动 / 继续 */
            {
                /* 屏幕 b1 启动事件会置位 tm0.en=1，下位机覆写关闭 tm0 */
                BSP_Screen_SendRawCmd("tm0.en=0");

                if (g_app.sys_state == APP_STATE_BIO_CALIB)
                {
                    return;
                }

                if (App_Therapy_IsPausedInPage(SCREEN_PAGE_FANKUIELEC))
                {
                    App_Therapy_Resume();
                }
                else if (frame->len >= 8U)
                {
                    uint8_t mode_sel  = frame->raw[3];
                    uint8_t freq_hz   = frame->raw[4];
                    uint8_t ch1       = frame->raw[5];
                    uint8_t ch2       = frame->raw[6];
                    uint8_t ch3       = frame->raw[7];
                    uint8_t timer_min = (frame->len >= 9U) ? frame->raw[8] : 10U;
                    if (freq_hz < 1U) freq_hz = (g_app.stim_freq_hz >= 1U) ? g_app.stim_freq_hz : 20U;
                    App_Therapy_StartCombo(1U /* 反馈呼吸 */, mode_sel, freq_hz, ch1, ch2, ch3, timer_min);
                }
                else
                {
                    App_Therapy_StartOrPause();
                }
            }
            else if (action == 0x00U)
            {
                if (g_app.sys_state == APP_STATE_BIO_CALIB)
                {
                    return;
                }
                App_Therapy_Pause();
                /* 关键修复（问题6）：屏幕暂停事件未清除箭头，下位机显式清除箭头与肺部动画 */
                BSP_Screen_SetLungFill(0U);
                BSP_Screen_SetBreathArrow(0U);
                BSP_Screen_SendRawCmd("tm0.en=0");
            }
            else if (action == 0x02U || action == 0x0FU)
            {
                App_Therapy_StopToMenu();
                App_Therapy_ResetBioCalibrationSession();
            }
            break;

        case SCREEN_PAGE_ELEC: /* Page 11: elec (单纯电刺激治疗测量运行) */
            if (action == 0x08U)
            {
                BSP_Screen_SendRawCmd("tm0.en=0");
                BSP_Screen_SendRawCmd("get start.bt_en.val");

                if (frame->len >= 4U && frame->raw[3] > 0U)
                {
                    App_Therapy_SetTimerMin(frame->raw[3]);
                }

                if (App_Therapy_IsPausedInPage(SCREEN_PAGE_ELEC))
                {
                    uint32_t total_s = (uint32_t)g_app.timer_min * 60U;
                    uint32_t elap_s  = g_app.active_elapsed_ms / 1000U;
                    uint16_t rem_s   = (total_s > elap_s) ? (uint16_t)(total_s - elap_s) : 0U;
                    BSP_Screen_SetRemainTimeSec(rem_s);
                    BSP_Screen_SetControlBtnPic(0U);
                }
                else
                {
                    App_Therapy_ResetBioCalibrationSession();
                    /* 保持屏幕自身 codesload 加载的初始定时时间，不发送 10:00 覆写 */
                    BSP_Screen_SetControlBtnPic(0U);
                }
                BSP_Audio_PlayVoice(AUDIO_VOICE_CLICK_START_PROMPT); /* 0009 */
            }
            else if (action == 0x01U) /* 启动 / 继续 */
            {
                BSP_Screen_SendRawCmd("tm0.en=0");

                if (App_Therapy_IsPausedInPage(SCREEN_PAGE_ELEC))
                {
                    App_Therapy_Resume();
                }
                else if (frame->len >= 8U)
                {
                    uint8_t mode_sel  = frame->raw[3]; /* 0:连续波, 1:断续波, 2:疏密波 */
                    uint8_t freq_hz   = frame->raw[4]; /* 频率 (Hz) */
                    uint8_t ch1       = frame->raw[5]; /* 通道1档位 (0~10) */
                    uint8_t ch2       = frame->raw[6]; /* 通道2档位 (0~10) */
                    uint8_t ch3       = frame->raw[7]; /* 通道3档位 (0~10) */
                    uint8_t timer_min = (frame->len >= 9U) ? frame->raw[8] : 10U;
                    if (freq_hz < 1U) freq_hz = (g_app.stim_freq_hz >= 1U) ? g_app.stim_freq_hz : 20U;
                    App_Therapy_StartPureStim(mode_sel, freq_hz, ch1, ch2, ch3, timer_min);
                }
                else
                {
                    App_Therapy_StartOrPause();
                }
            }
            else if (action == 0x00U)
            {
                App_Therapy_Pause();
                BSP_Screen_SendRawCmd("tm0.en=0");
            }
            else if (action == 0x02U || action == 0x0FU)
            {
                App_Therapy_StopToMenu();
            }
            break;

        /* ================= 3. 5 个参数设置页面 ================= */
        case SCREEN_PAGE_JIANYISET:  /* Page 5:  jianyiset (简易呼吸参数设置) */
        case SCREEN_PAGE_ELECSET:    /* Page 12: elecset (单纯电刺激参数设置) */
        case SCREEN_PAGE_LIANHESET1: /* Page 13: lianheset1 (联合简易参数设置) */
            if (action == 0x08U)
            {
                /* 只要进入设置界面，立即停止当前治疗计时并重置会话，按新时间准备重新开始 */
                App_Therapy_StopToMenu();
                App_Therapy_ClearPausedSession();
                App_Therapy_ResetBioCalibrationSession();
                BSP_Screen_SendRawCmd("get start.bt_en.val");
                BSP_Screen_SetSettingsDisplay(g_app.volume, g_app.timer_min, g_app.ble_enabled);
            }
            else if (action == 0x10U && frame->len >= 4U) /* 实时音量调节 */
            {
                App_Therapy_SetVolume(frame->raw[3]);
            }
            else if (action == 0x11U && frame->len >= 4U) /* 实时蓝牙开关 */
            {
                App_Therapy_SetBleEnable(frame->raw[3]);
            }
            else if (action == 0x12U && frame->len >= 4U) /* 实时定时调节 */
            {
                App_Therapy_SetTimerMin(frame->raw[3]);
            }
            break;

        case SCREEN_PAGE_FANKUISET:  /* Page 8:  fankuiset (生物反馈呼吸参数设置) */
        case SCREEN_PAGE_LIANHESET2: /* Page 14: lianheset2 (联合反馈参数设置) */
            if (action == 0x08U)
            {
                /* 只要进入设置界面，立即停止当前治疗计时并重置会话，按新时间准备重新开始 */
                App_Therapy_StopToMenu();
                App_Therapy_ClearPausedSession();
                /* 关键保留机制：生物反馈设置界面进入时不清除 s_bio_freq_valid，以便返回时保留呼吸频率 */
                BSP_Screen_SendRawCmd("get start.bt_en.val");
                BSP_Screen_SetSettingsDisplay(g_app.volume, g_app.timer_min, g_app.ble_enabled);
            }
            else if (action == 0x10U && frame->len >= 4U) /* 实时音量调节 */
            {
                App_Therapy_SetVolume(frame->raw[3]);
            }
            else if (action == 0x11U && frame->len >= 4U) /* 实时蓝牙开关 */
            {
                App_Therapy_SetBleEnable(frame->raw[3]);
            }
            else if (action == 0x12U && frame->len >= 4U) /* 实时定时调节 */
            {
                App_Therapy_SetTimerMin(frame->raw[3]);
            }
            break;

        default:
            /* 全局参数调节兜底 */
            if (action == 0x10U && frame->len >= 4U)
            {
                App_Therapy_SetVolume(frame->raw[3]);
            }
            else if (action == 0x11U && frame->len >= 4U)
            {
                App_Therapy_SetBleEnable(frame->raw[3]);
            }
            break;
    }
}

/**
 * @brief  处理淘晶驰原生 0x65 触摸事件帧（作为未编写 printh 时的兼容回退通道）
 * @param  page: 屏幕原生页面 ID
 * @param  comp_id: 控件 ID
 * @param  event: 触摸状态（0x01=按下，0x00=弹起）
 * @retval 无
 */
static void Screen_HandleTouchFrame(uint8_t page, uint8_t comp_id, uint8_t event)
{
    ScreenFrame_t fake;
    /* 仅响应弹起事件（0x00），避免按下期间重复触发 */
    if (event != 0x00U) return;

    memset(&fake, 0, sizeof(fake));
    fake.header  = SCREEN_FRAME_HDR_CTRL;
    fake.page    = page;
    fake.control = comp_id;
    fake.value   = 0x01U;
    fake.len     = 4U;
    fake.raw[0]  = 0x05U;
    fake.raw[1]  = page;
    fake.raw[2]  = comp_id;
    fake.raw[3]  = 0x01U;

    Screen_HandleCtrlFrame(&fake);
}

/**
 * @brief  串口屏 FreeRTOS 任务入口函数（周期 20 ms）
 * @note   1. 持续从环形缓冲区提取完整帧并进入三级 switch-case 分发
 *         2. 治疗运行期按需节流刷新肺部动画（0~100）与剩余倒计时
 *         3. 每 10 秒刷新一次电池电量图标
 * @param  argument: FreeRTOS 任务参数（未使用）
 * @retval 无
 */
void ScreenTask(void const *argument)
{
    ScreenFrame_t frame;
    uint32_t tick_cnt = 0U;
    uint8_t  last_lung = 255U;      /* 上次已发送的肺部进度，用于变化检测节流 */
    uint16_t last_remain_s = 0xFFFFU;
    (void)argument;

    osDelay(50U);

    /* 启动初期显式关闭屏幕休眠与自动休眠定时器，确保屏幕始终常亮不熄屏 */
    BSP_Screen_SendRawCmd("sleep=0");
    BSP_Screen_SendRawCmd("usleep=0");

    for (;;)
    {
        /* 1. 排空并解析所有以 0xFF 0xFF 0xFF 结尾的完整上行数据帧 */
        while (BSP_Screen_ReadFrame(&frame))
        {
            if (frame.header == SCREEN_FRAME_HDR_CTRL || frame.header == SCREEN_FRAME_HDR_CUSTOM)
            {
                if (frame.len >= 3U)
                {
                    Screen_HandleCtrlFrame(&frame);
                }
            }
            else if (frame.header == SCREEN_FRAME_HDR_NUM && frame.len >= 5U)
            {
                /* 串口屏返回的数值帧：raw[1..4] 为 32 位小端整数。用于同步 start.bt_en.val 等屏幕全局变量 */
                uint8_t ble_en = (frame.raw[1] != 0U) ? 1U : 0U;
                App_Therapy_SetBleEnable(ble_en);
            }
            else if (frame.header == SCREEN_FRAME_HDR_TOUCH) /* 淘晶驰原生触摸帧兜底：0x65 [page] [comp] [event] */
            {
                if (frame.len >= 4U)
                {
                    Screen_HandleTouchFrame(frame.page, frame.control, frame.value);
                }
            }
        }

        /* 2. 治疗运行中定时刷新肺部动画与剩余时间（值变化时才发送，降低串口负载） */
        if (g_app.running && !g_app.paused)
        {
            if (g_app.Breath_Mode != BREATH_MODE_OFF && g_app.lung_pct != last_lung)
            {
                last_lung = g_app.lung_pct;
                BSP_Screen_SetLungFill(last_lung);
            }

            if ((tick_cnt % 10U) == 0U) /* 每 200 ms 检查一次倒计时秒数变化 */
            {
                uint32_t total_s = (uint32_t)g_app.timer_min * 60U;
                uint32_t elap_s  = g_app.active_elapsed_ms / 1000U;
                uint16_t rem_s   = (total_s > elap_s) ? (uint16_t)(total_s - elap_s) : 0U;
                if (rem_s != last_remain_s)
                {
                    last_remain_s = rem_s;
                    BSP_Screen_SetRemainTimeSec(rem_s);
                }
            }
        }
        else
        {
            last_lung = 255U;
            last_remain_s = 0xFFFFU;
        }

        /* 3. 电池图标暂定不进行变化控制，下位机不参与赋值 */
        /*
        if ((tick_cnt % 500U) == 0U)
        {
            BSP_Screen_SetBattery(BSP_Power_GetBatteryPercent());
        }
        */

        tick_cnt++;
        osDelay(20U);
    }
}
