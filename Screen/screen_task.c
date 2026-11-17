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
                BSP_Audio_PlayVoice(AUDIO_VOICE_WELCOME); /* 0001: 欢迎使用呼吸调控仪... */
            }
            break;

        case SCREEN_PAGE_ELECMODE:    /* Page 1:  elecmode (电刺激模式选择：连续/断续/疏密) */
        case SCREEN_PAGE_LIANHEMODE2: /* Page 10: lianhemode2 (联合模式第2步：选电刺激) */
            if (action == 0x08U)
            {
                BSP_Audio_PlayVoice(AUDIO_VOICE_SEL_STIM_MODE); /* 0011: 请选择电刺激模式 */
            }
            break;

        case SCREEN_PAGE_BREATHMODE:  /* Page 2:  breathmode (呼吸引导模式选择：简易/生物反馈) */
            if (action == 0x08U)
            {
                /* 底层收发联调测试：收到 05 02 08 FF FF FF 以后，向串口回发 05 02 01 01 FF FF FF */
                static const uint8_t s_test_ack[7] = {0x05U, 0x02U, 0x01U, 0x01U, 0xFFU, 0xFFU, 0xFFU};
                BSP_Screen_SendRawBytes(s_test_ack, 7U);

                BSP_Audio_PlayVoice(AUDIO_VOICE_SEL_BREATH_MODE); /* 0002: 请选择呼吸引导模式 */
            }
            break;

        case SCREEN_PAGE_LIANHEMODE1: /* Page 3:  lianhemode1 (联合模式第1步：选呼吸引导) */
            if (action == 0x08U)
            {
                BSP_Audio_PlayVoice(AUDIO_VOICE_SEL_BREATH_MODE); /* 0002: 请选择呼吸引导模式 */
            }
            break;

        /* ================= 2. 5 个治疗测量与运行页面 ================= */
        case SCREEN_PAGE_JIANYI: /* Page 4: jianyi (单纯简易呼吸引导测量运行) */
            if (action == 0x08U) /* 刚进页面：页面加载就绪 */
            {
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
                }
                BSP_Audio_PlayVoice(AUDIO_VOICE_CLICK_START_PROMPT); /* 0009: 请点击启停键... */
            }
            else if (action == 0x01U) /* 启动 / 继续 */
            {
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
                    uint8_t ble_en    = (frame->len >= 6U) ? frame->raw[5] : 0U;
                    App_Therapy_StartSimpleBreath(timer_min, volume, ble_en);
                }
            }
            else if (action == 0x00U) /* 暂停 */
            {
                App_Therapy_Pause();
            }
            else if (action == 0x02U || action == 0x0FU) /* 返回主页 */
            {
                App_Therapy_StopToMenu();
            }
            break;

        case SCREEN_PAGE_JIANYIELEC: /* Page 6: jianyielec (简易呼吸 + 电刺激联合测量运行) */
            if (action == 0x08U)
            {
                if (App_Therapy_IsPausedInPage(SCREEN_PAGE_JIANYIELEC))
                {
                    uint32_t total_s = (uint32_t)g_app.timer_min * 60U;
                    uint32_t elap_s  = g_app.active_elapsed_ms / 1000U;
                    uint16_t rem_s   = (total_s > elap_s) ? (uint16_t)(total_s - elap_s) : 0U;
                    BSP_Screen_SetRemainTimeSec(rem_s);
                    BSP_Screen_SetGuideBPM(g_app.guide_bpm);
                    BSP_Screen_SetLungFill(0U);
                    BSP_Screen_SetBreathArrow(0U);
                }
                BSP_Audio_PlayVoice(AUDIO_VOICE_CLICK_START_PROMPT); /* 0009 */
            }
            else if (action == 0x01U) /* 启动 / 继续 */
            {
                if (App_Therapy_IsPausedInPage(SCREEN_PAGE_JIANYIELEC))
                {
                    App_Therapy_Resume();
                }
                else if (frame->len >= 9U)
                {
                    uint8_t offset    = (frame->len >= 10U) ? 1U : 0U;
                    uint8_t mode_sel  = frame->raw[3U + offset];
                    uint8_t freq_hz   = frame->raw[4U + offset];
                    uint8_t ch1       = frame->raw[5U + offset];
                    uint8_t ch2       = frame->raw[6U + offset];
                    uint8_t ch3       = frame->raw[7U + offset];
                    uint8_t timer_min = frame->raw[8U + offset];
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
            }
            else if (action == 0x02U || action == 0x0FU)
            {
                App_Therapy_StopToMenu();
            }
            break;

        case SCREEN_PAGE_FANKUI: /* Page 7: fankui (单纯生物反馈呼吸测量运行) */
            if (action == 0x08U) /* 刚进页面：自动启动呼吸传感器采集并播报 0007 引导呼吸 */
            {
                if (App_Therapy_IsPausedInPage(SCREEN_PAGE_FANKUI))
                {
                    uint32_t total_s = (uint32_t)g_app.timer_min * 60U;
                    uint32_t elap_s  = g_app.active_elapsed_ms / 1000U;
                    uint16_t rem_s   = (total_s > elap_s) ? (uint16_t)(total_s - elap_s) : 0U;
                    BSP_Screen_SetRemainTimeSec(rem_s);
                    BSP_Screen_SetGuideBPM(g_app.guide_bpm);
                    BSP_Screen_SetLungFill(0U);
                    BSP_Screen_SetBreathArrow(0U);
                    BSP_Audio_PlayVoice(AUDIO_VOICE_CLICK_START_PROMPT);
                }
                else
                {
                    App_Therapy_SelectBreathMode(BREATH_MODE_BIOFEEDBACK);
                }
            }
            else if (action == 0x01U) /* 启动 / 继续 */
            {
                if (App_Therapy_IsPausedInPage(SCREEN_PAGE_FANKUI))
                {
                    App_Therapy_Resume();
                }
                else if (frame->len >= 6U)
                {
                    uint8_t timer_min = frame->raw[3];
                    uint8_t volume    = frame->raw[4];
                    uint8_t ble_en    = frame->raw[5];
                    App_Therapy_StartBiofeedbackBreath(timer_min, volume, ble_en);
                }
                else
                {
                    App_Therapy_StartOrPause();
                }
            }
            else if (action == 0x00U)
            {
                App_Therapy_Pause();
            }
            else if (action == 0x02U || action == 0x0FU)
            {
                App_Therapy_StopToMenu();
            }
            break;

        case SCREEN_PAGE_FANKUIELEC: /* Page 9: fankuielec (生物反馈呼吸 + 电刺激联合测量运行) */
            if (action == 0x08U)
            {
                if (App_Therapy_IsPausedInPage(SCREEN_PAGE_FANKUIELEC))
                {
                    uint32_t total_s = (uint32_t)g_app.timer_min * 60U;
                    uint32_t elap_s  = g_app.active_elapsed_ms / 1000U;
                    uint16_t rem_s   = (total_s > elap_s) ? (uint16_t)(total_s - elap_s) : 0U;
                    BSP_Screen_SetRemainTimeSec(rem_s);
                    BSP_Screen_SetGuideBPM(g_app.guide_bpm);
                    BSP_Screen_SetLungFill(0U);
                    BSP_Screen_SetBreathArrow(0U);
                    BSP_Audio_PlayVoice(AUDIO_VOICE_CLICK_START_PROMPT);
                }
                else
                {
                    App_Therapy_SelectBreathMode(BREATH_MODE_BIOFEEDBACK);
                }
            }
            else if (action == 0x01U) /* 启动 / 继续 */
            {
                if (App_Therapy_IsPausedInPage(SCREEN_PAGE_FANKUIELEC))
                {
                    App_Therapy_Resume();
                }
                else if (frame->len >= 9U)
                {
                    uint8_t offset    = (frame->len >= 10U) ? 1U : 0U;
                    uint8_t mode_sel  = frame->raw[3U + offset];
                    uint8_t freq_hz   = frame->raw[4U + offset];
                    uint8_t ch1       = frame->raw[5U + offset];
                    uint8_t ch2       = frame->raw[6U + offset];
                    uint8_t ch3       = frame->raw[7U + offset];
                    uint8_t timer_min = frame->raw[8U + offset];
                    App_Therapy_StartCombo(1U /* 反馈呼吸 */, mode_sel, freq_hz, ch1, ch2, ch3, timer_min);
                }
                else
                {
                    App_Therapy_StartOrPause();
                }
            }
            else if (action == 0x00U)
            {
                App_Therapy_Pause();
            }
            else if (action == 0x02U || action == 0x0FU)
            {
                App_Therapy_StopToMenu();
            }
            break;

        case SCREEN_PAGE_ELEC: /* Page 11: elec (单纯电刺激治疗测量运行) */
            if (action == 0x08U)
            {
                if (App_Therapy_IsPausedInPage(SCREEN_PAGE_ELEC))
                {
                    uint32_t total_s = (uint32_t)g_app.timer_min * 60U;
                    uint32_t elap_s  = g_app.active_elapsed_ms / 1000U;
                    uint16_t rem_s   = (total_s > elap_s) ? (uint16_t)(total_s - elap_s) : 0U;
                    BSP_Screen_SetRemainTimeSec(rem_s);
                }
                BSP_Audio_PlayVoice(AUDIO_VOICE_CLICK_START_PROMPT); /* 0009 */
            }
            else if (action == 0x01U) /* 启动 / 继续 */
            {
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
            }
            else if (action == 0x02U || action == 0x0FU)
            {
                App_Therapy_StopToMenu();
            }
            break;

        /* ================= 3. 5 个参数设置页面 ================= */
        case SCREEN_PAGE_JIANYISET:  /* Page 5:  jianyiset (简易呼吸参数设置) */
        case SCREEN_PAGE_FANKUISET:  /* Page 8:  fankuiset (生物反馈呼吸参数设置) */
        case SCREEN_PAGE_ELECSET:    /* Page 12: elecset (单纯电刺激参数设置) */
        case SCREEN_PAGE_LIANHESET1: /* Page 13: lianheset1 (联合简易参数设置) */
        case SCREEN_PAGE_LIANHESET2: /* Page 14: lianheset2 (联合反馈参数设置) */
            if (action == 0x10U && frame->len >= 4U) /* 实时音量调节 */
            {
                App_Therapy_SetVolume(frame->raw[3]);
            }
            else if (action == 0x11U && frame->len >= 4U) /* 实时蓝牙开关 */
            {
                App_Therapy_SetBleEnable(frame->raw[3]);
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

        /* 3. 每 10 s（500 * 20 ms）刷新一次电量显示 */
        if ((tick_cnt % 500U) == 0U)
        {
            BSP_Screen_SetBattery(BSP_Power_GetBatteryPercent());
        }

        tick_cnt++;
        osDelay(20U);
    }
}
