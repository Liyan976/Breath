#include "therapy_ctrl.h"
#include "bsp_audio.h"
#include "bsp_breath_sensor.h"
#include "bsp_power.h"
#include "bsp_screen.h"
#include "bsp_stimulator.h"
#include "bsp_storage.h"
#include <string.h>

AppContext_t g_app;

static uint8_t  s_bio_calib_repeat = 0U;
static uint8_t  s_bio_busy_prev    = 0U;
static uint8_t  s_bio_wait_5s      = 0U;
static uint32_t s_bio_calib_tick   = 0U;
static uint8_t  s_paused_therapy_page = 0xFFU; /**< 0xFF 表示当前无暂停保存的治疗模式 */
static uint8_t  s_calib_signal_detected_prev = 0U; /**< 生物反馈校准期传感器有效气流状态，用于节流更新 */

uint8_t App_Therapy_GetPausedPage(void)
{
    return s_paused_therapy_page;
}

uint8_t App_Therapy_IsPausedInPage(uint8_t page)
{
    return (s_paused_therapy_page == page && (g_app.paused || g_app.sys_state == APP_STATE_PAUSED));
}

void App_Therapy_ClearPausedSession(void)
{
    s_paused_therapy_page = 0xFFU;
}

static uint8_t therapy_can_start(void)
{
    if (g_app.Breath_Mode == BREATH_MODE_OFF && g_app.Stim_Mode == STIM_MODE_OFF)
    {
        return 0U;
    }

    if (g_app.Breath_Mode == BREATH_MODE_BIOFEEDBACK && !BSP_BreathSensor_IsInserted())
    {
        BSP_Screen_ShowToast("Insert breath sensor");
        return 0U;
    }

    if (g_app.Stim_Mode != STIM_MODE_OFF)
    {
        if (g_app.stim_freq_hz < 1U ||
            (g_app.stim_level[0] == 0U && g_app.stim_level[1] == 0U && g_app.stim_level[2] == 0U))
        {
            BSP_Screen_ShowToast("Set stim level & freq");
            return 0U;
        }
    }

    return 1U;
}

static void sync_stimulator_params(void)
{
    StimWaveType_t w = STIM_WAVE_CONT;
    if (g_app.Stim_Mode == STIM_MODE_INTERMITTENT)     w = STIM_WAVE_INTERMITTENT;
    else if (g_app.Stim_Mode == STIM_MODE_DENSE_DISPERSE) w = STIM_WAVE_DENSE_SPARSE;

    BSP_Stim_SetWaveType(w);
    BSP_Stim_SetBaseFreqHz(g_app.stim_freq_hz ? g_app.stim_freq_hz : 1U);
    BSP_Stim_SetAllLevels(g_app.stim_level);
}

static void apply_combined_phase_to_stim(BreathPhase_t phase)
{
    uint16_t dense_hz;
    if (g_app.Breath_Mode == BREATH_MODE_OFF || g_app.Stim_Mode == STIM_MODE_OFF)
    {
        return;
    }

    switch (g_app.Stim_Mode)
    {
        case STIM_MODE_CONTINUOUS:
            BSP_Stim_SetComboOverride(1U, 1U, g_app.stim_freq_hz ? g_app.stim_freq_hz : 1U);
            break;

        case STIM_MODE_INTERMITTENT:
            /* Sparse wave during Inhale (e.g. 4s), Dense wave (5x) during Exhale (e.g. 6s) */
            if (phase == BREATH_PHASE_INHALE)
            {
                BSP_Stim_SetComboOverride(1U, 1U, g_app.stim_freq_hz ? g_app.stim_freq_hz : 1U);
            }
            else if (phase == BREATH_PHASE_EXHALE)
            {
                dense_hz = (uint16_t)g_app.stim_freq_hz * 5U;
                if (dense_hz < 5U) dense_hz = 5U;
                if (dense_hz > STIM_FREQ_MAX_HZ) dense_hz = STIM_FREQ_MAX_HZ;
                BSP_Stim_SetComboOverride(1U, 1U, (uint8_t)dense_hz);
            }
            break;

        case STIM_MODE_DENSE_DISPERSE:
            /* Active during Inhale, paused during Exhale */
            BSP_Stim_SetComboOverride(1U,
                                      (phase == BREATH_PHASE_INHALE) ? 1U : 0U,
                                      g_app.stim_freq_hz ? g_app.stim_freq_hz : 1U);
            break;

        default:
            break;
    }
}

void App_Therapy_Init(void)
{
    memset(&g_app, 0, sizeof(g_app));
    g_app.Breath_Mode  = BREATH_MODE_OFF;
    g_app.Stim_Mode    = STIM_MODE_OFF;
    g_app.sys_state    = APP_STATE_MENU;
    g_app.stim_freq_hz = 20U;
    g_app.timer_min    = APP_TIMER_MIN_DEFAULT;
    g_app.volume       = APP_VOL_DEFAULT;
    g_app.guide_bpm    = 10U;

    BSP_Power_Init(App_Therapy_PowerOffRequest);
    BSP_BreathSensor_Init();
    BSP_Stim_Init();
    BSP_Screen_Init();
    BSP_Audio_Init();
    BSP_BLE_Init(App_Therapy_OnBleCommand);
    BSP_Storage_Init();
    Algo_Breath_Init();

    /* 明确关闭所有休眠指令与自动休眠定时器，确保屏幕常亮不熄屏 */
    BSP_Screen_SendRawCmd("sleep=0");
    BSP_Screen_SendRawCmd("usleep=0");

    BSP_Audio_SetVolume(g_app.volume);
    BSP_Audio_PlayVoice(AUDIO_VOICE_WELCOME);
    BSP_Screen_SetBattery(BSP_Power_GetBatteryPercent());
    BSP_Screen_SetPage("start");
}

void App_Therapy_EnterBreathMenu(uint8_t is_combo)
{
    g_app.Breath_Mode = BREATH_MODE_SIMPLE;
    g_app.Stim_Mode = is_combo ? STIM_MODE_CONTINUOUS : STIM_MODE_OFF;
    g_app.sys_state = APP_STATE_SELECT_BREATH;
    BSP_Audio_PlayVoice(AUDIO_VOICE_SEL_BREATH_MODE);
}

void App_Therapy_EnterStimMenu(void)
{
    g_app.Breath_Mode = BREATH_MODE_OFF;
    g_app.Stim_Mode = STIM_MODE_CONTINUOUS;
    g_app.sys_state = APP_STATE_SELECT_STIM;
    BSP_Audio_PlayVoice(AUDIO_VOICE_SEL_STIM_MODE);
}

void App_Therapy_SelectBreathMode(BreathMode_t mode)
{
    g_app.Breath_Mode = mode;
    if (mode == BREATH_MODE_BIOFEEDBACK)
    {
        Algo_Breath_ResetBioCalib();
        g_app.sys_state = APP_STATE_BIO_CALIB;
        s_bio_calib_repeat = 1U; /* 第一次播报 0007 */
        s_bio_calib_tick = HAL_GetTick();
        s_bio_busy_prev = 1U;
        s_bio_wait_5s = 0U;

        /* 校准引导期间按键保持“开始/播放”图标 (pic = 11)，禁止自动变为暂停图标 */
        BSP_Screen_SetControlBtnPic(0U);
        /* 初始状态肺部与箭头保持静止排空，待检测到传感器呼吸信号后再动起来 */
        s_calib_signal_detected_prev = 0U;
        BSP_Screen_SetLungFill(0U);
        BSP_Screen_SetBreathArrow(0U);
        Algo_Breath_StartGuide(10U);

        BSP_Audio_PlayVoice(AUDIO_VOICE_BIO_CALIB);
    }
    else if (mode == BREATH_MODE_SIMPLE)
    {
        g_app.guide_bpm = 10U;
        g_app.sys_state = (g_app.Stim_Mode == STIM_MODE_OFF) ? APP_STATE_READY : APP_STATE_SELECT_STIM;
    }
}

void App_Therapy_SelectStimMode(StimMode_t mode)
{
    g_app.Stim_Mode = mode;
    if (mode == STIM_MODE_DENSE_DISPERSE && g_app.stim_freq_hz > 20U)
    {
        g_app.stim_freq_hz = 20U; /* Sparse 0~20 Hz -> dense 5~100 Hz */
    }
    g_app.sys_state = APP_STATE_READY;
}

void App_Therapy_EnterSettings(void)
{
    if (g_app.sys_state == APP_STATE_RUNNING)
    {
        BSP_Screen_ShowToast("Pause before settings");
        return;
    }
    g_app.sys_state = APP_STATE_SETTINGS;
    BSP_Screen_SetSettingsDisplay(g_app.volume, g_app.timer_min, g_app.ble_enabled);
}

void App_Therapy_ReturnFromSettings(void)
{
    g_app.sys_state = g_app.paused ? APP_STATE_PAUSED : APP_STATE_READY;
    BSP_Screen_SetSettingsDisplay(g_app.volume, g_app.timer_min, g_app.ble_enabled);
    BSP_Screen_SetStimParams(g_app.stim_freq_hz, g_app.stim_level);
}

void App_Therapy_SetVolume(uint8_t vol_0_to_10)
{
    g_app.volume = (vol_0_to_10 > APP_VOL_MAX) ? APP_VOL_MAX : vol_0_to_10;
    BSP_Audio_SetVolume(g_app.volume);
}

void App_Therapy_SetTimerMin(uint8_t timer_min)
{
    if (timer_min < APP_TIMER_MIN_MIN) timer_min = APP_TIMER_MIN_MIN;
    if (timer_min > APP_TIMER_MIN_MAX) timer_min = APP_TIMER_MIN_MAX;
    g_app.timer_min = timer_min;
}

void App_Therapy_SetBleEnable(uint8_t enable)
{
    g_app.ble_enabled = enable ? 1U : 0U;
    BSP_BLE_SetEnabled(g_app.ble_enabled);
}

void App_Therapy_SetStimFreq(uint8_t freq_hz)
{
    g_app.stim_freq_hz = (freq_hz > STIM_FREQ_MAX_HZ) ? STIM_FREQ_MAX_HZ : freq_hz;
    if (g_app.Stim_Mode == STIM_MODE_DENSE_DISPERSE && g_app.stim_freq_hz > 20U)
    {
        g_app.stim_freq_hz = 20U;
    }
    BSP_Stim_SetBaseFreqHz(g_app.stim_freq_hz ? g_app.stim_freq_hz : 1U);
}

void App_Therapy_SetStimLevel(uint8_t ch, uint8_t level_0_to_10)
{
    if (ch >= STIM_CH_COUNT) return;
    if (level_0_to_10 > STIM_LEVEL_MAX) level_0_to_10 = STIM_LEVEL_MAX;
    g_app.stim_level[ch] = level_0_to_10;
    BSP_Stim_SetChannelLevel(ch, level_0_to_10);
}

void App_Therapy_Pause(void)
{
    if (g_app.sys_state == APP_STATE_RUNNING || g_app.sys_state == APP_STATE_BIO_CALIB)
    {
        g_app.paused = 1U;
        g_app.sys_state = APP_STATE_PAUSED;
        s_paused_therapy_page = BSP_Screen_GetCurrentPage();

        BSP_Stim_Enable(0U);
        BSP_Audio_Stop();

        /* 暂停后先将肺部图像清空，并隐藏吸呼指示箭头 */
        g_app.lung_pct = 0U;
        BSP_Screen_SetLungFill(0U);
        BSP_Screen_SetBreathArrow(0U);

        /* 暂停后按钮切换为开始/播放图标 (pic = 11) */
        BSP_Screen_SetControlBtnPic(0U);

        BSP_Audio_PlayVoice(AUDIO_VOICE_PAUSE_TREAT);
    }
}

void App_Therapy_Resume(void)
{
    if (g_app.sys_state == APP_STATE_PAUSED || g_app.paused)
    {
        g_app.paused = 0U;

        /* 恢复运行后按钮切换为暂停图标 (pic = 10) */
        BSP_Screen_SetControlBtnPic(1U);

        /* 若是在生物反馈引导未完成时被暂停的，恢复为引导阶段继续检测 */
        if (!Algo_Breath_IsBioCalibReady() &&
            (s_paused_therapy_page == SCREEN_PAGE_FANKUI || s_paused_therapy_page == SCREEN_PAGE_FANKUIELEC))
        {
            g_app.sys_state = APP_STATE_BIO_CALIB;
            s_bio_busy_prev = 1U;
            s_bio_wait_5s   = 0U;
            s_bio_calib_tick = HAL_GetTick();
            Algo_Breath_StartGuide(10U);
            BSP_Audio_PlayVoice(AUDIO_VOICE_BIO_CALIB);
            return;
        }

        g_app.running = 1U;
        g_app.sys_state = APP_STATE_RUNNING;

        if (g_app.Breath_Mode != BREATH_MODE_OFF)
        {
            /* 继续时重新从吸气阶段 0% 开始引导 */
            Algo_Breath_StartGuide(g_app.guide_bpm);
            g_app.breath_phase = BREATH_PHASE_INHALE;
            g_app.lung_pct     = 0U;
            BSP_Screen_SetLungFill(0U);
            BSP_Screen_SetBreathArrow(1U); /* 吸气箭头 */
            BSP_Audio_PlayVoice(AUDIO_VOICE_INHALE);
        }

        if (g_app.Stim_Mode != STIM_MODE_OFF)
        {
            sync_stimulator_params();
            BSP_Stim_SetComboOverride((g_app.Breath_Mode != BREATH_MODE_OFF) ? 1U : 0U,
                                      1U, g_app.stim_freq_hz ? g_app.stim_freq_hz : 1U);
            BSP_Stim_Enable(1U);
        }

        /* 立即同步刷新当前保留的剩余倒计时和频率 */
        uint32_t total_s = (uint32_t)g_app.timer_min * 60U;
        uint32_t elap_s  = g_app.active_elapsed_ms / 1000U;
        uint16_t rem_s   = (total_s > elap_s) ? (uint16_t)(total_s - elap_s) : 0U;
        BSP_Screen_SetRemainTimeSec(rem_s);
        BSP_Screen_SetGuideBPM(g_app.guide_bpm);
    }
}

void App_Therapy_StartOrPause(void)
{
    if (g_app.sys_state == APP_STATE_RUNNING)
    {
        App_Therapy_Pause();
        BSP_Screen_SetPage("paused");
        return;
    }

    if (g_app.sys_state == APP_STATE_PAUSED)
    {
        App_Therapy_Resume();
        BSP_Screen_SetPage("running");
        return;
    }

    if (!therapy_can_start())
    {
        return;
    }

    g_app.active_elapsed_ms = 0U;
    g_app.running = 1U;
    g_app.paused  = 0U;
    g_app.sys_state = APP_STATE_RUNNING;

    if (g_app.Breath_Mode != BREATH_MODE_OFF)
    {
        g_app.guide_bpm = (g_app.Breath_Mode == BREATH_MODE_BIOFEEDBACK)
                          ? Algo_Breath_GetBioGBPM()
                          : 10U;
        Algo_Breath_StartGuide(g_app.guide_bpm);
    }
    else
    {
        Algo_Breath_StopGuide();
        BSP_Screen_SetLungFill(0U);
        BSP_Screen_SetBreathArrow(0U);
    }

    if (g_app.Stim_Mode != STIM_MODE_OFF)
    {
        sync_stimulator_params();
        BSP_Stim_SetComboOverride((g_app.Breath_Mode != BREATH_MODE_OFF) ? 1U : 0U,
                                  1U, g_app.stim_freq_hz ? g_app.stim_freq_hz : 1U);
        BSP_Stim_Enable(1U);
    }
    else
    {
        BSP_Stim_SetComboOverride(0U, 0U, 1U);
        BSP_Stim_Enable(0U);
    }

    BSP_Audio_PlayVoice(AUDIO_VOICE_START_TREAT);
    BSP_Screen_SetPage("running");
}

void App_Therapy_StartSimpleBreath(uint8_t timer_min, uint8_t volume, uint8_t ble_en)
{
    App_Therapy_SetTimerMin(timer_min);
    App_Therapy_SetVolume(volume);
    App_Therapy_SetBleEnable(ble_en);

    g_app.Breath_Mode = BREATH_MODE_SIMPLE;
    g_app.Stim_Mode   = STIM_MODE_OFF;
    g_app.guide_bpm   = 10U;

    s_paused_therapy_page   = SCREEN_PAGE_JIANYI;
    g_app.active_elapsed_ms = 0U;
    g_app.running = 1U;
    g_app.paused  = 0U;
    g_app.sys_state = APP_STATE_RUNNING;

    Algo_Breath_StartGuide(g_app.guide_bpm);
    BSP_Stim_SetComboOverride(0U, 0U, 1U);
    BSP_Stim_Enable(0U);

    /* 启动瞬间立即强制刷新屏幕频率与初始倒计时，杜绝显示残留值 */
    BSP_Screen_SetControlBtnPic(1U);
    BSP_Screen_SetGuideBPM(g_app.guide_bpm);
    BSP_Screen_SetRemainTimeSec((uint16_t)timer_min * 60U);

    BSP_Audio_PlayVoice(AUDIO_VOICE_START_TREAT);
}

void App_Therapy_StartBiofeedbackBreath(uint8_t timer_min, uint8_t volume, uint8_t ble_en)
{
    App_Therapy_SetTimerMin(timer_min);
    App_Therapy_SetVolume(volume);
    App_Therapy_SetBleEnable(ble_en);

    g_app.Breath_Mode = BREATH_MODE_BIOFEEDBACK;
    g_app.Stim_Mode   = STIM_MODE_OFF;

    if (!Algo_Breath_IsBioCalibReady())
    {
        Algo_Breath_ResetBioCalib();
        g_app.guide_bpm = 10U;
    }
    else
    {
        g_app.guide_bpm = Algo_Breath_GetBioGBPM();
    }

    s_paused_therapy_page   = SCREEN_PAGE_FANKUI;
    g_app.active_elapsed_ms = 0U;
    g_app.running = 1U;
    g_app.paused  = 0U;
    g_app.sys_state = APP_STATE_RUNNING;

    Algo_Breath_StartGuide(g_app.guide_bpm);
    BSP_Stim_SetComboOverride(0U, 0U, 1U);
    BSP_Stim_Enable(0U);

    /* 启动瞬间立即强制刷新屏幕频率与初始倒计时，杜绝显示残留值 */
    BSP_Screen_SetControlBtnPic(1U);
    BSP_Screen_SetGuideBPM(g_app.guide_bpm);
    BSP_Screen_SetRemainTimeSec((uint16_t)timer_min * 60U);

    BSP_Audio_PlayVoice(AUDIO_VOICE_START_TREAT);
}

void App_Therapy_StartPureStim(uint8_t mode_sel, uint8_t freq_hz, uint8_t ch1, uint8_t ch2, uint8_t ch3, uint8_t timer_min)
{
    g_app.Breath_Mode = BREATH_MODE_OFF;
    switch (mode_sel)
    {
        case 0U: g_app.Stim_Mode = STIM_MODE_CONTINUOUS; break;
        case 1U: g_app.Stim_Mode = STIM_MODE_INTERMITTENT; break;
        case 2U: g_app.Stim_Mode = STIM_MODE_DENSE_DISPERSE; break;
        default: g_app.Stim_Mode = STIM_MODE_CONTINUOUS; break;
    }

    App_Therapy_SetStimFreq(freq_hz);
    App_Therapy_SetStimLevel(0U, ch1);
    App_Therapy_SetStimLevel(1U, ch2);
    App_Therapy_SetStimLevel(2U, ch3);
    App_Therapy_SetTimerMin(timer_min);

    if (!therapy_can_start())
    {
        return;
    }

    s_paused_therapy_page   = SCREEN_PAGE_ELEC;
    g_app.active_elapsed_ms = 0U;
    g_app.running = 1U;
    g_app.paused  = 0U;
    g_app.sys_state = APP_STATE_RUNNING;

    Algo_Breath_StopGuide();
    BSP_Screen_SetLungFill(0U);
    BSP_Screen_SetBreathArrow(0U);

    sync_stimulator_params();
    BSP_Stim_SetComboOverride(0U, 1U, g_app.stim_freq_hz ? g_app.stim_freq_hz : 1U);
    BSP_Stim_Enable(1U);

    /* 启动瞬间立即刷新初始倒计时与按键状态 */
    BSP_Screen_SetControlBtnPic(1U);
    BSP_Screen_SetRemainTimeSec((uint16_t)timer_min * 60U);

    BSP_Audio_PlayVoice(AUDIO_VOICE_START_TREAT);
}

void App_Therapy_StartCombo(uint8_t breath_mode, uint8_t mode_sel, uint8_t freq_hz, uint8_t ch1, uint8_t ch2, uint8_t ch3, uint8_t timer_min)
{
    g_app.Breath_Mode = (breath_mode == 1U) ? BREATH_MODE_BIOFEEDBACK : BREATH_MODE_SIMPLE;
    switch (mode_sel)
    {
        case 0U: g_app.Stim_Mode = STIM_MODE_CONTINUOUS; break;
        case 1U: g_app.Stim_Mode = STIM_MODE_INTERMITTENT; break;
        case 2U: g_app.Stim_Mode = STIM_MODE_DENSE_DISPERSE; break;
        default: g_app.Stim_Mode = STIM_MODE_CONTINUOUS; break;
    }

    App_Therapy_SetStimFreq(freq_hz);
    App_Therapy_SetStimLevel(0U, ch1);
    App_Therapy_SetStimLevel(1U, ch2);
    App_Therapy_SetStimLevel(2U, ch3);
    App_Therapy_SetTimerMin(timer_min);

    if (!therapy_can_start())
    {
        return;
    }

    s_paused_therapy_page   = (breath_mode == 1U) ? SCREEN_PAGE_FANKUIELEC : SCREEN_PAGE_JIANYIELEC;
    g_app.active_elapsed_ms = 0U;
    g_app.running = 1U;
    g_app.paused  = 0U;
    g_app.sys_state = APP_STATE_RUNNING;

    if (g_app.Breath_Mode == BREATH_MODE_BIOFEEDBACK)
    {
        g_app.guide_bpm = Algo_Breath_IsBioCalibReady() ? Algo_Breath_GetBioGBPM() : 10U;
    }
    else
    {
        g_app.guide_bpm = 10U;
    }
    Algo_Breath_StartGuide(g_app.guide_bpm);

    sync_stimulator_params();
    BSP_Stim_SetComboOverride(1U, 1U, g_app.stim_freq_hz ? g_app.stim_freq_hz : 1U);
    BSP_Stim_Enable(1U);

    /* 启动瞬间立即强制刷新屏幕频率与初始倒计时 */
    BSP_Screen_SetControlBtnPic(1U);
    BSP_Screen_SetGuideBPM(g_app.guide_bpm);
    BSP_Screen_SetRemainTimeSec((uint16_t)timer_min * 60U);

    BSP_Audio_PlayVoice(AUDIO_VOICE_START_TREAT);
}

void App_Therapy_StopToMenu(void)
{
    /* 彻底重置治疗运行状态与暂停保存数据，确保退出到菜单后重新进入时从初始设定重新开始 */
    g_app.running = 0U;
    g_app.paused  = 0U;
    g_app.active_elapsed_ms = 0U;
    s_paused_therapy_page   = 0xFFU;
    g_app.Breath_Mode = BREATH_MODE_OFF;
    g_app.Stim_Mode   = STIM_MODE_OFF;
    g_app.sys_state   = APP_STATE_MENU;

    Algo_Breath_StopGuide();
    BSP_Stim_SetComboOverride(0U, 0U, 1U);
    BSP_Stim_Enable(0U);
    BSP_Audio_Stop();
    BSP_Screen_SetLungFill(0U);
    BSP_Screen_SetBreathArrow(0U);
}

void App_Therapy_PowerOffRequest(void)
{
    /* 用户明确要求：关闭所有 sleep 指令，不向串口屏发送任何熄屏休眠指令 */
    BSP_Stim_EmergencyStop();
    BSP_Audio_Stop();
    if (g_app.active_elapsed_ms > 0U && g_app.sys_state != APP_STATE_FINISHED)
    {
        BSP_Storage_LogSession((uint8_t)g_app.Breath_Mode,
                               (uint8_t)g_app.Stim_Mode,
                               g_app.stim_freq_hz,
                               g_app.stim_level,
                               (uint16_t)(g_app.active_elapsed_ms / 1000U),
                               g_app.guide_bpm);
    }
    HAL_Delay(5U);
    BSP_Power_ShutdownNow();
}

void App_Therapy_UnifiedTimeAxisStep20ms(uint32_t now_ms)
{
    uint16_t raw_breath;
    BreathGuideOut_t guide_out;
    uint8_t target_bpm;

    /* Always sample NTC breath sensor at 50 Hz when biofeedback is active or calibrating */
    if (g_app.Breath_Mode == BREATH_MODE_BIOFEEDBACK)
    {
        raw_breath = BSP_BreathSensor_ReadRaw();
        Algo_Breath_FeedSample(raw_breath, now_ms);
        g_app.real_cbpm = Algo_Breath_GetRealCBPM();

        if (g_app.sys_state == APP_STATE_BIO_CALIB)
        {
            if (Algo_Breath_IsBioCalibReady())
            {
                Algo_Breath_StopGuide();
                BSP_Screen_SetLungFill(0U);
                BSP_Screen_SetBreathArrow(0U);

                g_app.guide_bpm = Algo_Breath_GetBioGBPM();
                g_app.sys_state = APP_STATE_READY;
                BSP_Screen_SetGuideBPM(g_app.guide_bpm);
                BSP_Audio_PlayBioResult(g_app.guide_bpm);

                /* 3次呼吸引导检测结束：清空动画并将按键切换为“开始”图标(pic=11)，等待患者开启治疗 */
                BSP_Screen_SetControlBtnPic(0U);
            }
            else
            {
                /* 检查传感器是否有呼吸气流输出 */
                uint8_t detected = Algo_Breath_IsSignalDetected();
                if (detected)
                {
                    s_calib_signal_detected_prev = 1U;
                    /* 检测到传感器呼吸输出：肺部填充与箭头跟随 10次/分 引导律动 */
                    BreathGuideOut_t calib_guide;
                    Algo_Breath_StepGuide(20U, 10U, &calib_guide);
                    BSP_Screen_SetLungFill(calib_guide.lung_pct);
                    BSP_Screen_SetBreathArrow((uint8_t)calib_guide.phase);
                }
                else
                {
                    if (s_calib_signal_detected_prev)
                    {
                        s_calib_signal_detected_prev = 0U;
                        /* 未检测到有效气流：肺部清空、箭头全隐，保持静止等待 */
                        BSP_Screen_SetLungFill(0U);
                        BSP_Screen_SetBreathArrow(0U);
                    }
                }

                /* 检查 0007 播完后 5 秒无呼吸信号重试机制（最多重复播 3 次） */
                uint8_t busy = BSP_Audio_IsBusy();
                if (s_bio_busy_prev && !busy)
                {
                    s_bio_wait_5s = 1U;
                    s_bio_calib_tick = now_ms;
                }
                s_bio_busy_prev = busy;

                if (s_bio_wait_5s && ((now_ms - s_bio_calib_tick) >= 5000U))
                {
                    s_bio_wait_5s = 0U;
                    if (s_bio_calib_repeat < 3U)
                    {
                        s_bio_calib_repeat++;
                        BSP_Audio_PlayVoice(AUDIO_VOICE_BIO_CALIB);
                        s_bio_busy_prev = 1U;
                    }
                    else
                    {
                        /* 3次引导后仍未检测到有效呼吸信号，回退默认 5次/分 并播报 0013 */
                        Algo_Breath_StopGuide();
                        BSP_Screen_SetLungFill(0U);
                        BSP_Screen_SetBreathArrow(0U);

                        g_app.guide_bpm = 5U;
                        g_app.sys_state = APP_STATE_READY;
                        BSP_Screen_SetGuideBPM(5U);
                        BSP_Audio_PlayVoice(AUDIO_VOICE_BIO_FALLBACK);

                        /* 超时结束同样切换按键为“开始”图标(pic=11) */
                        BSP_Screen_SetControlBtnPic(0U);
                    }
                }
            }
        }
    }

    if (!g_app.running || g_app.paused || g_app.sys_state != APP_STATE_RUNNING)
    {
        return;
    }

    g_app.active_elapsed_ms += 20U;

    if (g_app.Breath_Mode != BREATH_MODE_OFF)
    {
        target_bpm = (g_app.Breath_Mode == BREATH_MODE_BIOFEEDBACK)
                     ? Algo_Breath_GetBioGBPM()
                     : Algo_Breath_GetSimpleBPM(g_app.active_elapsed_ms);

        Algo_Breath_StepGuide(20U, target_bpm, &guide_out);
        g_app.breath_phase = guide_out.phase;
        g_app.lung_pct     = guide_out.lung_pct;
        g_app.guide_bpm    = guide_out.guide_bpm;

        if (guide_out.phase_changed)
        {
            /* Unified lockstep sync across Screen, Audio, and Stimulator */
            BSP_Screen_SetBreathArrow((uint8_t)guide_out.phase);
            BSP_Screen_SetGuideBPM(guide_out.guide_bpm);
            BSP_Audio_PlayVoice((guide_out.phase == BREATH_PHASE_INHALE)
                                ? AUDIO_VOICE_INHALE
                                : AUDIO_VOICE_EXHALE);
            apply_combined_phase_to_stim(guide_out.phase);
        }
    }
}

void App_Therapy_MainTick10ms(uint32_t now_ms)
{
    uint32_t total_ms;
    uint8_t ch;
    uint8_t drop_mask = 0U;

    BSP_Power_Poll10ms(now_ms);

    if (g_app.sys_state == APP_STATE_RUNNING)
    {
        total_ms = (uint32_t)g_app.timer_min * 60000UL;
        if (g_app.active_elapsed_ms >= total_ms)
        {
            g_app.running = 0U;
            g_app.paused  = 0U;
            s_paused_therapy_page = 0xFFU;
            g_app.finish_tick_ms = now_ms;
            g_app.sys_state = APP_STATE_FINISHED;

            Algo_Breath_StopGuide();
            BSP_Stim_SetComboOverride(0U, 0U, 1U);
            BSP_Stim_Enable(0U);
            BSP_Screen_SetLungFill(0U);
            BSP_Screen_SetBreathArrow(0U);
            BSP_Screen_SetControlBtnPic(0U);
            BSP_Screen_ShowFinished(g_app.timer_min);
            BSP_Audio_PlayTreatEnd(g_app.timer_min);

            {
                StorageSessionRecord_t rec;
                rec.breath_mode  = (uint8_t)g_app.Breath_Mode;
                rec.stim_mode    = (uint8_t)g_app.Stim_Mode;
                rec.stim_freq_hz = g_app.stim_freq_hz;
                rec.levels[0]    = g_app.stim_level[0];
                rec.levels[1]    = g_app.stim_level[1];
                rec.levels[2]    = g_app.stim_level[2];
                rec.valid_sec    = (uint16_t)(g_app.active_elapsed_ms / 1000U);
                rec.final_gbpm   = g_app.guide_bpm;
                BSP_Storage_RequestLog(&rec);
            }
            return;
        }

        if (g_app.Stim_Mode != STIM_MODE_OFF)
        {
            for (ch = 0U; ch < STIM_CH_COUNT; ch++)
            {
                if (BSP_Stim_IsElectrodeDropped(ch))
                {
                    drop_mask |= (uint8_t)(1U << ch);
                }
            }
            if (drop_mask != g_app.elec_drop_mask)
            {
                for (ch = 0U; ch < STIM_CH_COUNT; ch++)
                {
                    BSP_Screen_SetElectrodeWarn(ch, (drop_mask >> ch) & 0x01U);
                }
                if (drop_mask != 0U && g_app.elec_drop_mask == 0U)
                {
                    BSP_Audio_PlayVoice(AUDIO_VOICE_ELECTRODE_LOST);
                }
                g_app.elec_drop_mask = drop_mask;
            }
        }
    }
    else if (g_app.sys_state == APP_STATE_FINISHED)
    {
        /* 语音播报期间（0010有效时长+0020~0045时间+0012关机提示）保持刷新计时，确保提示播报完毕后再等待 10s */
        if (BSP_Audio_IsBusy())
        {
            g_app.finish_tick_ms = now_ms;
        }
        else if ((now_ms - g_app.finish_tick_ms) >= APP_FINISH_AUTO_OFF_MS)
        {
            App_Therapy_PowerOffRequest();
        }
    }
}

void App_Therapy_OnBleCommand(const BleRxCommand_t *cmd)
{
    if (cmd == NULL) return;

    if (cmd->cmd_id == BLE_CMD_SET_PARAMS && cmd->payload_len >= 6U)
    {
        uint8_t freq = (cmd->payload[0] > STIM_FREQ_MAX_HZ) ? STIM_FREQ_MAX_HZ : cmd->payload[0];
        uint8_t c1   = (cmd->payload[1] > STIM_LEVEL_MAX) ? STIM_LEVEL_MAX : cmd->payload[1];
        uint8_t c2   = (cmd->payload[2] > STIM_LEVEL_MAX) ? STIM_LEVEL_MAX : cmd->payload[2];
        uint8_t c3   = (cmd->payload[3] > STIM_LEVEL_MAX) ? STIM_LEVEL_MAX : cmd->payload[3];
        uint8_t tmin = cmd->payload[4];
        uint8_t vol  = (cmd->payload[5] > APP_VOL_MAX) ? APP_VOL_MAX : cmd->payload[5];

        if (tmin < APP_TIMER_MIN_MIN) tmin = APP_TIMER_MIN_MIN;
        if (tmin > APP_TIMER_MIN_MAX) tmin = APP_TIMER_MIN_MAX;

        g_app.stim_freq_hz = freq;
        g_app.stim_level[0] = c1;
        g_app.stim_level[1] = c2;
        g_app.stim_level[2] = c3;
        g_app.timer_min = tmin;
        g_app.volume = vol;

        BSP_Stim_SetBaseFreqHz(freq ? freq : 1U);
        BSP_Stim_SetAllLevels(g_app.stim_level);
        BSP_Audio_SetVolume(vol);
        BSP_Screen_SetStimParams(freq, g_app.stim_level);
        BSP_Screen_SetSettingsDisplay(g_app.volume, g_app.timer_min, g_app.ble_enabled);
    }
    else if (cmd->cmd_id == BLE_CMD_CTRL_STATE && cmd->payload_len >= 1U)
    {
        if (cmd->payload[0] == 1U) App_Therapy_StartOrPause();
        else if (cmd->payload[0] == 2U) App_Therapy_StopToMenu();
    }
}
