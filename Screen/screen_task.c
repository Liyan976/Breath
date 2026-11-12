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

/**
 * @brief  处理自定义 0x05 控制帧（三级 switch-case 分发：页面 -> 控件 -> 参数/动作值）
 * @param  page: 页面编号（ScreenPageId_t，如 0x00 主菜单、0x01 呼吸选择等）
 * @param  control: 当前页面内的控件或功能编号
 * @param  value: 按键动作码或滑块/步进器携带的数值
 * @retval 无
 */
static void Screen_HandleCtrlFrame(uint8_t page, uint8_t control, uint8_t value)
{
    /* 第一级 switch：按页面编号分发 */
    switch (page)
    {
        case SCREEN_PAGE_START: /* 0x00: 开机主菜单界面 */
        {
            /* 第二级 switch：按主菜单控件编号分发 */
            switch (control)
            {
                case 0x01U: /* 按键1：呼吸训练模式 */
                {
                    /* 第三级 switch：按动作值分发 */
                    switch (value)
                    {
                        case 0x01U:
                            App_Therapy_EnterBreathMenu(0U); /* 仅呼吸训练，关闭电刺激 */
                            break;
                        default:
                            break;
                    }
                    break;
                }
                case 0x02U: /* 按键2：单纯电刺激模式 */
                {
                    switch (value)
                    {
                        case 0x01U:
                            App_Therapy_EnterStimMenu();     /* 仅电刺激，关闭呼吸引导 */
                            break;
                        default:
                            break;
                    }
                    break;
                }
                case 0x03U: /* 按键3：呼吸+电刺激联合模式 */
                {
                    switch (value)
                    {
                        case 0x01U:
                            App_Therapy_EnterBreathMenu(1U); /* 联合模式：先选呼吸子模式再选电刺激波形 */
                            break;
                        default:
                            break;
                    }
                    break;
                }
                default:
                    break;
            }
            break;
        }

        case SCREEN_PAGE_BREATH_SEL: /* 0x01: 呼吸引导子模式选择界面 */
        {
            switch (control)
            {
                case 0x01U: /* 简易治疗模式（10->5 次/分固定阶梯递减） */
                {
                    switch (value)
                    {
                        case 0x01U: /* 仅高亮选中（预留，可在屏幕端完成视觉高亮） */
                            break;
                        case 0x02U: /* 点击“下一页”确认进入 */
                            App_Therapy_SelectBreathMode(BREATH_MODE_SIMPLE);
                            if (g_app.Stim_Mode != STIM_MODE_OFF)
                            {
                                /* 联合模式下，下一步进入电刺激波形选择提示 */
                                BSP_Audio_PlayVoice(AUDIO_VOICE_SEL_STIM_MODE);
                            }
                            else
                            {
                                /* 单纯呼吸模式直接刷新右侧参数展示 */
                                BSP_Screen_SetGuideBPM(10U);
                                BSP_Screen_SetSettingsDisplay(g_app.volume, g_app.timer_min, g_app.ble_enabled);
                            }
                            break;
                        default:
                            break;
                    }
                    break;
                }
                case 0x02U: /* 生物反馈模式（实时检测呼吸波谷自动调整 GBPM） */
                {
                    switch (value)
                    {
                        case 0x01U: /* 仅高亮选中 */
                            break;
                        case 0x02U: /* 点击“下一页”确认并开始 3 次呼吸初始校准 */
                            App_Therapy_SelectBreathMode(BREATH_MODE_BIOFEEDBACK);
                            break;
                        default:
                            break;
                    }
                    break;
                }
                case 0x0FU: /* 返回主菜单 */
                {
                    if (value == 0x01U)
                    {
                        App_Therapy_StopToMenu();
                    }
                    break;
                }
                default:
                    break;
            }
            break;
        }

        case SCREEN_PAGE_STIM_SEL: /* 0x02: 电刺激波形选择界面 */
        {
            switch (control)
            {
                case 0x01U: /* 连续波 */
                    if (value == 0x01U) App_Therapy_SelectStimMode(STIM_MODE_CONTINUOUS);
                    break;
                case 0x02U: /* 断续波（联合模式下吸气疏波、呼气密波） */
                    if (value == 0x01U) App_Therapy_SelectStimMode(STIM_MODE_INTERMITTENT);
                    break;
                case 0x03U: /* 疏密波（联合模式下吸气刺激、呼气暂停） */
                    if (value == 0x01U) App_Therapy_SelectStimMode(STIM_MODE_DENSE_DISPERSE);
                    break;
                case 0x0FU: /* 返回主菜单 */
                    if (value == 0x01U) App_Therapy_StopToMenu();
                    break;
                default:
                    break;
            }
            /* 选定波形后立即同步回显当前频率、三路强度及系统设置 */
            if (control >= 0x01U && control <= 0x03U && value == 0x01U)
            {
                BSP_Screen_SetStimParams(g_app.stim_freq_hz, g_app.stim_level);
                BSP_Screen_SetSettingsDisplay(g_app.volume, g_app.timer_min, g_app.ble_enabled);
            }
            break;
        }

        case SCREEN_PAGE_TREAT_RUN: /* 0x03: 治疗就绪 / 运行 / 暂停界面 */
        {
            switch (control)
            {
                case 0x01U: /* 启停按键（运行 <-> 暂停/启动） */
                    if (value == 0x01U) App_Therapy_StartOrPause();
                    break;
                case 0x02U: /* 返回按键（治疗运行中将被状态机拦截提示） */
                    if (value == 0x01U) App_Therapy_StopToMenu();
                    break;
                case 0x03U: /* 设置按键（仅暂停或未开始时允许进入设置页） */
                    if (value == 0x01U) App_Therapy_EnterSettings();
                    break;
                case 0x10U: /* 电刺激频率调节：value = 0~100 Hz */
                    App_Therapy_SetStimFreq(value);
                    break;
                case 0x11U: /* 通道1强度调节：value = 0~10 档 */
                    App_Therapy_SetStimLevel(0U, value);
                    break;
                case 0x12U: /* 通道2强度调节：value = 0~10 档 */
                    App_Therapy_SetStimLevel(1U, value);
                    break;
                case 0x13U: /* 通道3强度调节：value = 0~10 档 */
                    App_Therapy_SetStimLevel(2U, value);
                    break;
                default:
                    break;
            }
            break;
        }

        case SCREEN_PAGE_SETTINGS: /* 0x04: 参数设置界面 */
        {
            switch (control)
            {
                case 0x01U: /* 音量调节：value = 0~10（0 为静音） */
                    App_Therapy_SetVolume(value);
                    break;
                case 0x02U: /* 定时时长设置：value = 5~30 分钟 */
                    App_Therapy_SetTimerMin(value);
                    break;
                case 0x03U: /* 蓝牙开关：value = 0 关闭 / 1 开启 */
                    App_Therapy_SetBleEnable(value);
                    break;
                case 0x0FU: /* 返回治疗界面，同步回显设置值 */
                    if (value == 0x01U) App_Therapy_ReturnFromSettings();
                    break;
                default:
                    break;
            }
            break;
        }

        case SCREEN_PAGE_END: /* 0x05: 治疗结束结算界面 */
        {
            switch (control)
            {
                case 0x01U: /* 确认并返回主菜单 */
                    if (value == 0x01U) App_Therapy_StopToMenu();
                    break;
                default:
                    break;
            }
            break;
        }

        default:
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
    /* 仅响应弹起事件（0x00），避免按下期间重复触发 */
    if (event != 0x00U) return;
    Screen_HandleCtrlFrame(page, comp_id, 0x01U);
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

    for (;;)
    {
        /* 1. 排空并解析所有以 0xFF 0xFF 0xFF 结尾的完整上行数据帧 */
        while (BSP_Screen_ReadFrame(&frame))
        {
            switch (frame.header)
            {
                case SCREEN_FRAME_HDR_CTRL: /* 自定义协议帧：0x05 [page] [control] [value] */
                    if (frame.len >= 4U)
                    {
                        Screen_HandleCtrlFrame(frame.page, frame.control, frame.value);
                    }
                    break;

                case SCREEN_FRAME_HDR_TOUCH: /* 淘晶驰原生触摸帧：0x65 [page] [comp] [event] */
                    if (frame.len >= 4U)
                    {
                        Screen_HandleTouchFrame(frame.page, frame.control, frame.value);
                    }
                    break;

                default:
                    break;
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
