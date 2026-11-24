#include "bsp_stimulator.h"
#include "adc.h"
#include "tim.h"
#include <string.h>

/* 50 us TIM2 tick constants: 400 us pulse width = 8 ticks (倍增电荷量，对比原 250us 显著增强肌肉收缩感), 50 us dead-time = 1 tick */
#define PULSE_WIDTH_TICKS       8U
#define PULSE_DEADTIME_TICKS    1U
#define TICK_RATE_HZ            20000U

/* Schematic Boost divider: Rtop=100k, Rbot=4.7k -> Vout_mV = raw * 3300 * 1047 / (4095 * 47) */
#define BOOST_ADC_TO_MV(raw)    ((uint32_t)(raw) * 1795U / 100U)

/* Constant current DAC duty for 2x stimulus intensity across 10 ohm shunt:
 * 原基准：30 mA 对应 CCR = 906，单相脉宽 250 us，单相电荷 Q_old = 30mA * 250us = 7500 nC
 * 2倍强度调校：单相脉宽提升至 400 us (8 ticks)，限流上限提升至 37.5 mA (CCR = 1133)
 * 单相电荷 Q_new = 37.5mA * 400us = 15000 nC (严格达成 2.0 倍理论刺激强度)
 */
#define CC_DAC_CCR_MAX          1133U

/* Electrode drop threshold on LM324 output:
 * 悬空开路时 LM324 采样底噪为 0~10 counts (~10mV)
 * 贴附人体时探测脉冲产生的 LM324 采样为 150~400 counts (~120~320mV)
 * 取 20 counts 作为脱落/贴附判决门限，既高于0~10底噪，又保证1档微弱电流稳定识别 */
#define ELEC_VALID_ADC_THRESH   20U

/* 脱落探测占空比（~3.5mA 安全微弱探测脉冲），确保电极重新贴附时能产生明显的 ADC 响应 */
#define STIM_ELEC_PROBE_CCR     120U

typedef enum {
    HB_STATE_IDLE = 0,
    HB_STATE_POS_PULSE,
    HB_STATE_DEADTIME,
    HB_STATE_NEG_PULSE
} HBridgeState_t;

typedef struct {
    GPIO_TypeDef *port_a;
    uint16_t      pin_a;
    GPIO_TypeDef *port_b;
    uint16_t      pin_b;
} HBridgePins_t;

static const HBridgePins_t s_hb_pins[STIM_CH_COUNT] = {
    { S11_GPIO_Port, S11_Pin, S12_GPIO_Port, S12_Pin },
    { S21_GPIO_Port, S21_Pin, S22_GPIO_Port, S22_Pin },
    { S31_GPIO_Port, S31_Pin, S32_GPIO_Port, S32_Pin }
};

static volatile uint8_t  s_enabled = 0U;
static volatile uint8_t  s_gate_on = 1U;
static volatile uint8_t  s_combo_override = 0U;
static volatile uint8_t  s_runtime_freq_hz = 20U;
static uint8_t           s_base_freq_hz = 20U;
static StimWaveType_t    s_wave_type = STIM_WAVE_CONT;

static uint8_t  s_target_level[STIM_CH_COUNT] = {0};
static float    s_current_level_f[STIM_CH_COUNT] = {0.0f};
static uint8_t  s_elec_dropped[STIM_CH_COUNT] = {0};
static uint32_t s_elec_recover_start_ms[STIM_CH_COUNT] = {0};
static volatile uint32_t s_last_valid_pulse_ms[STIM_CH_COUNT] = {0};
static uint16_t s_elec_peak_adc[STIM_CH_COUNT] = {0};

static float    s_boost_integral[STIM_CH_COUNT] = {0.0f};
static uint16_t s_boost_mv[STIM_CH_COUNT] = {0};

/* TIM2 ISR state */
static volatile HBridgeState_t s_hb_state = HB_STATE_IDLE;
static volatile uint32_t       s_state_ticks = 0U;
static volatile uint32_t       s_period_ticks = 1000U;
static volatile uint32_t       s_cycle_tick_acc = 0U;

static void hb_write_ch(uint8_t ch, GPIO_PinState a, GPIO_PinState b)
{
    HAL_GPIO_WritePin(s_hb_pins[ch].port_a, s_hb_pins[ch].pin_a, a);
    HAL_GPIO_WritePin(s_hb_pins[ch].port_b, s_hb_pins[ch].pin_b, b);
}

static void hb_all_off(void)
{
    uint8_t ch;
    for (ch = 0U; ch < STIM_CH_COUNT; ch++)
    {
        hb_write_ch(ch, GPIO_PIN_RESET, GPIO_PIN_RESET);
    }
}

static void set_boost_ccr(uint8_t ch, uint32_t ccr)
{
    if (ccr > (BOOST_PWM_PERIOD * 85U / 100U)) /* Cap duty at 85% for Boost safety */
    {
        ccr = BOOST_PWM_PERIOD * 85U / 100U;
    }
    if (ch == 0U)      __HAL_TIM_SET_COMPARE(&htim5,  TIM_CHANNEL_1, ccr);
    else if (ch == 1U) __HAL_TIM_SET_COMPARE(&htim14, TIM_CHANNEL_1, ccr);
    else if (ch == 2U) __HAL_TIM_SET_COMPARE(&htim4,  TIM_CHANNEL_1, ccr);
}

static void set_cc_dac_ccr(uint8_t ch, uint32_t ccr)
{
    if (ccr > CC_DAC_CCR_MAX) ccr = CC_DAC_CCR_MAX;
    if (ch == 0U)      __HAL_TIM_SET_COMPARE(&htim3,  TIM_CHANNEL_1, ccr);
    else if (ch == 1U) __HAL_TIM_SET_COMPARE(&htim3,  TIM_CHANNEL_2, ccr);
    else if (ch == 2U) __HAL_TIM_SET_COMPARE(&htim12, TIM_CHANNEL_2, ccr);
}

void BSP_Stim_Init(void)
{
    uint8_t ch;
    hb_all_off();

    for (ch = 0U; ch < STIM_CH_COUNT; ch++)
    {
        s_target_level[ch] = 0U;
        s_current_level_f[ch] = 0.0f;
        s_elec_dropped[ch] = 0U;
        s_elec_recover_start_ms[ch] = 0U;
        s_last_valid_pulse_ms[ch] = 0U;
        s_boost_integral[ch] = 0.0f;
        set_boost_ccr(ch, 0U);
        set_cc_dac_ccr(ch, 0U);
    }

    HAL_TIM_PWM_Start(&htim5,  TIM_CHANNEL_1);
    HAL_TIM_PWM_Start(&htim14, TIM_CHANNEL_1);
    HAL_TIM_PWM_Start(&htim4,  TIM_CHANNEL_1);
    HAL_TIM_PWM_Start(&htim3,  TIM_CHANNEL_1);
    HAL_TIM_PWM_Start(&htim3,  TIM_CHANNEL_2);
    HAL_TIM_PWM_Start(&htim12, TIM_CHANNEL_2);
    HAL_TIM_Base_Start_IT(&htim2);
}

void BSP_Stim_Enable(uint8_t enable)
{
    uint8_t ch;
    s_enabled = enable ? 1U : 0U;
    if (s_enabled)
    {
        uint32_t now = HAL_GetTick();
        s_hb_state = HB_STATE_IDLE;
        s_cycle_tick_acc = s_period_ticks; /* 启动即刻激发首拍脉冲，消除1Hz下首脉冲1秒迟滞 */
        for (ch = 0U; ch < STIM_CH_COUNT; ch++)
        {
            s_elec_dropped[ch] = 0U;
            s_elec_recover_start_ms[ch] = 0U;
            s_last_valid_pulse_ms[ch] = now;
        }
    }
    else
    {
        BSP_Stim_EmergencyStop();
    }
}

void BSP_Stim_EmergencyStop(void)
{
    uint8_t ch;
    s_enabled = 0U;
    s_hb_state = HB_STATE_IDLE;
    hb_all_off();
    for (ch = 0U; ch < STIM_CH_COUNT; ch++)
    {
        s_current_level_f[ch] = 0.0f;
        s_boost_integral[ch] = 0.0f;
        set_boost_ccr(ch, 0U);
        set_cc_dac_ccr(ch, 0U);
    }
}

void BSP_Stim_SetChannelLevel(uint8_t ch, uint8_t level)
{
    if (ch >= STIM_CH_COUNT) return;
    if (level > STIM_LEVEL_MAX) level = STIM_LEVEL_MAX;
    s_target_level[ch] = level;
    if (level == 0U)
    {
        s_current_level_f[ch] = 0.0f;
        s_elec_dropped[ch] = 0U;
        s_elec_recover_start_ms[ch] = 0U;
        s_last_valid_pulse_ms[ch] = HAL_GetTick();
        set_cc_dac_ccr(ch, 0U);
        set_boost_ccr(ch, 0U);
    }
}

void BSP_Stim_SetAllLevels(const uint8_t levels[STIM_CH_COUNT])
{
    uint8_t ch;
    for (ch = 0U; ch < STIM_CH_COUNT; ch++)
    {
        BSP_Stim_SetChannelLevel(ch, levels[ch]);
    }
}

void BSP_Stim_SetWaveType(StimWaveType_t wave)
{
    s_wave_type = wave;
}

void BSP_Stim_SetBaseFreqHz(uint8_t freq_hz)
{
    if (freq_hz < STIM_FREQ_MIN_HZ) freq_hz = STIM_FREQ_MIN_HZ;
    if (freq_hz > STIM_FREQ_MAX_HZ) freq_hz = STIM_FREQ_MAX_HZ;
    s_base_freq_hz = freq_hz;
    BSP_Stim_SetRuntimeFreqHz(freq_hz);
}

void BSP_Stim_SetRuntimeFreqHz(uint8_t freq_hz)
{
    uint32_t period;
    if (freq_hz < STIM_FREQ_MIN_HZ) freq_hz = STIM_FREQ_MIN_HZ;
    if (freq_hz > STIM_FREQ_MAX_HZ) freq_hz = STIM_FREQ_MAX_HZ;
    if (freq_hz != s_runtime_freq_hz)
    {
        uint8_t c;
        uint32_t now = HAL_GetTick();
        for (c = 0U; c < STIM_CH_COUNT; c++)
        {
            if (!s_elec_dropped[c])
            {
                s_last_valid_pulse_ms[c] = now;
            }
        }
    }
    s_runtime_freq_hz = freq_hz;
    period = TICK_RATE_HZ / (uint32_t)freq_hz;
    if (period < (PULSE_WIDTH_TICKS * 2U + PULSE_DEADTIME_TICKS + 4U))
    {
        period = PULSE_WIDTH_TICKS * 2U + PULSE_DEADTIME_TICKS + 4U;
    }
    s_period_ticks = period;
}

void BSP_Stim_SetComboOverride(uint8_t enable_override, uint8_t gate_on, uint8_t runtime_freq_hz)
{
    uint8_t old_gate = s_gate_on;
    s_combo_override = enable_override ? 1U : 0U;
    if (s_combo_override)
    {
        uint8_t new_gate = gate_on ? 1U : 0U;
        if (!old_gate && new_gate)
        {
            /* 关键修复（问题3）：联合模式由呼气(静息/无刺激)转为吸气(开始刺激)瞬间，
             * 立即重置所有通道的最后有效脉冲时间为当前时间，杜绝将静息期误算入脱落防抖导致误播“开始治疗” */
            uint32_t now = HAL_GetTick();
            uint8_t c;
            for (c = 0U; c < STIM_CH_COUNT; c++)
            {
                if (!s_elec_dropped[c])
                {
                    s_last_valid_pulse_ms[c] = now;
                }
                s_elec_recover_start_ms[c] = 0U;
            }
        }
        s_gate_on = new_gate;
        BSP_Stim_SetRuntimeFreqHz(runtime_freq_hz);
    }
}

void BSP_Stim_UpdateLoop10ms(uint32_t now_ms)
{
    static uint32_t wave_phase_ms = 0U;
    uint8_t ch;

    if (!s_enabled)
    {
        wave_phase_ms = 0U;
        return;
    }

    /* Standalone wave modulation (skipped when Combined Mode controls phase sync) */
    if (!s_combo_override)
    {
        uint8_t old_gate = s_gate_on;
        wave_phase_ms += 10U;
        if (s_wave_type == STIM_WAVE_INTERMITTENT)
        {
            /* 断续波：工作 15s，停 5s（周期 20000ms） */
            uint32_t t = wave_phase_ms % 20000U;
            s_gate_on = (t < 15000U) ? 1U : 0U;
            if (!old_gate && s_gate_on)
            {
                /* 关键修复（问题2）：纯电刺激断续波由停歇5s转为工作15s的上升沿，
                 * 立即重置脉冲时间戳，杜绝工作首刻误报脱落引发播报“开始治疗” */
                uint8_t c;
                for (c = 0U; c < STIM_CH_COUNT; c++)
                {
                    if (!s_elec_dropped[c])
                    {
                        s_last_valid_pulse_ms[c] = now_ms;
                    }
                    s_elec_recover_start_ms[c] = 0U;
                }
            }
            BSP_Stim_SetRuntimeFreqHz(s_base_freq_hz);
        }
        else if (s_wave_type == STIM_WAVE_DENSE_SPARSE)
        {
            /* 疏密波：严格遵照开发手册要求《工作时间比值(疏波:密波=2:1)，疏密波频率比值(疏波:密波=1:5)》
             * 周期 15000ms：前 10s 为疏波（基频 F），后 5s 为密波（5*F）
             */
            uint32_t t = wave_phase_ms % 15000U;
            s_gate_on = 1U;
            if (t < 10000U)
            {
                BSP_Stim_SetRuntimeFreqHz(s_base_freq_hz);
            }
            else
            {
                uint16_t dense = (uint16_t)s_base_freq_hz * 5U;
                if (dense > STIM_FREQ_MAX_HZ) dense = STIM_FREQ_MAX_HZ;
                BSP_Stim_SetRuntimeFreqHz((uint8_t)dense);
            }
        }
        else
        {
            s_gate_on = 1U;
        }
    }

    for (ch = 0U; ch < STIM_CH_COUNT; ch++)
    {
        uint16_t fb_raw = ADC1_GetChannelAvg((AdcChannelIndex_t)(ADC_IDX_BOOST1_FB + ch));
        uint16_t elec_raw = ADC1_GetChannelRaw((AdcChannelIndex_t)(ADC_IDX_ELEC1_IN + ch));
        uint32_t vout_mv = BOOST_ADC_TO_MV(fb_raw);
        s_boost_mv[ch] = (uint16_t)vout_mv;

        if (elec_raw > s_elec_peak_adc[ch])
        {
            s_elec_peak_adc[ch] = elec_raw;
        }

        if (s_target_level[ch] == 0U)
        {
            s_current_level_f[ch] = 0.0f;
            s_boost_integral[ch] = 0.0f;
            s_elec_dropped[ch] = 0U;
            s_elec_recover_start_ms[ch] = 0U;
            s_last_valid_pulse_ms[ch] = now_ms;
            set_boost_ccr(ch, 0U);
            set_cc_dac_ccr(ch, 0U);
            continue;
        }

        /* 捕获到的峰值若满足有效脉冲门限，即刻刷新有效脉冲时间戳（双重保障） */
        if (s_elec_peak_adc[ch] >= ELEC_VALID_ADC_THRESH)
        {
            s_last_valid_pulse_ms[ch] = now_ms;
        }

        /* Electrode drop & recovery detection based on pulse validity */
        if (s_enabled && s_gate_on)
        {
            uint32_t last_valid = s_last_valid_pulse_ms[ch];
            uint32_t elapsed_no_pulse = (now_ms >= last_valid) ? (now_ms - last_valid) : 0U;

            /* 根据当前运行时频率动态自适应脱落判定门限与恢复探测窗口（消除1Hz时周期为1000ms引发的误判） */
            uint32_t active_hz = (s_runtime_freq_hz > 0U) ? (uint32_t)s_runtime_freq_hz : 1U;
            uint32_t pulse_gap_ms = 1000U / active_hz;
            uint32_t drop_timeout_ms = pulse_gap_ms * 2U + 300U;
            if (drop_timeout_ms < 1500U) drop_timeout_ms = 1500U;
            uint32_t recover_gap_ms = pulse_gap_ms + 300U;

            if (!s_elec_dropped[ch])
            {
                /* 正常刺激中：超过防抖时间未检测到有效脉冲电流，判定电极脱落（容许至少2个脉冲周期无信号） */
                if (elapsed_no_pulse >= drop_timeout_ms)
                {
                    s_elec_dropped[ch] = 1U;
                    s_elec_recover_start_ms[ch] = 0U;
                    s_current_level_f[ch] = 0.0f;
                }
            }
            else
            {
                /* 脱落探测期：安全微弱探测脉冲在电极重新贴附后会重新产生有效采样 */
                if (elapsed_no_pulse < recover_gap_ms)
                {
                    if (s_elec_recover_start_ms[ch] == 0U)
                    {
                        s_elec_recover_start_ms[ch] = now_ms;
                    }
                    else if ((now_ms - s_elec_recover_start_ms[ch]) >= STIM_ELEC_RECOVER_CONFIRM_MS)
                    {
                        s_elec_dropped[ch] = 0U;
                        s_elec_recover_start_ms[ch] = 0U;
                        s_current_level_f[ch] = 0.0f; /* 恢复时触发软启动平滑爬升 */
                    }
                }
                else
                {
                    /* 探测脉冲未能检测到持续接触，重置恢复确认计时 */
                    s_elec_recover_start_ms[ch] = 0U;
                }
            }
        }
        else
        {
            /* 关键修复（问题2 & 问题3）：处于断续波停歇期或呼气暂停期间（!s_gate_on），
             * 未脱落通道持续刷新脉冲时间戳为当前时间，防止门闸重新打开首个tick误判脱落；
             * 同时暂停恢复确认计时器，严防在无刺激输出期间误确认恢复 */
            if (!s_elec_dropped[ch])
            {
                s_last_valid_pulse_ms[ch] = now_ms;
            }
            s_elec_recover_start_ms[ch] = 0U;
        }
        s_elec_peak_adc[ch] = 0U;

        /* 关键修复（问题5）：掉一个片三个全停！若任一已启用通道脱落，所有通道切断常规刺激 */
        {
            uint8_t any_dropped = 0U;
            uint8_t k;
            for (k = 0U; k < STIM_CH_COUNT; k++)
            {
                if (s_target_level[k] > 0U && s_elec_dropped[k])
                {
                    any_dropped = 1U;
                    break;
                }
            }

            if (any_dropped)
            {
                s_current_level_f[ch] = 0.0f;
                /* 脱落时恒流 DAC 保持微弱探测占空比（~3.5mA安全探针），保证电极重新贴附时能够被采样识别 */
                set_cc_dac_ccr(ch, STIM_ELEC_PROBE_CCR);
            }
            else
            {
                /* Soft-start ramp over STIM_ELEC_RECOVER_RAMP_MS (10ms step) */
                float target_f = (float)s_target_level[ch];
                float step = target_f * (10.0f / (float)STIM_ELEC_RECOVER_RAMP_MS);
                if (step < 0.02f) step = 0.02f;
                if (s_current_level_f[ch] < target_f)
                {
                    s_current_level_f[ch] += step;
                    if (s_current_level_f[ch] > target_f) s_current_level_f[ch] = target_f;
                }
                else
                {
                    s_current_level_f[ch] = target_f;
                }

                /* Update Constant-Current DAC PWM */
                uint32_t dac_ccr = (uint32_t)((s_current_level_f[ch] / (float)STIM_LEVEL_MAX) * (float)CC_DAC_CCR_MAX);
                set_cc_dac_ccr(ch, dac_ccr);
            }
        }

        /* Closed-loop PI Boost regulator: 根据设定目标档位稳定升压 (7.5V ~ 30V)
         * 关键修复（问题7）：无论电极是否脱落，升压电路均维持设定档位目标电压稳定输出，
         * 确保 PA0 (TIM5_CH1) 始终产生 50kHz PWM，VOUT1 稳定升压而非死锁在 4.68V 二极管压降 */
        {
            float eff_level = (float)s_target_level[ch];
            float target_mv = (float)STIM_BOOST_MIN_MV +
                              (eff_level / (float)STIM_LEVEL_MAX) *
                              (float)(STIM_BOOST_MAX_MV - STIM_BOOST_MIN_MV);
            float err_v = (target_mv - (float)vout_mv) * 0.001f;
            float duty;
            s_boost_integral[ch] += err_v * 0.01f;
            if (s_boost_integral[ch] > 0.6f)  s_boost_integral[ch] = 0.6f;
            if (s_boost_integral[ch] < -0.2f) s_boost_integral[ch] = -0.2f;

            duty = 0.25f + err_v * 0.03f + s_boost_integral[ch] * 0.4f;
            if (duty < 0.0f)  duty = 0.0f;
            if (duty > 0.85f) duty = 0.85f;
            set_boost_ccr(ch, (uint32_t)(duty * (float)BOOST_PWM_PERIOD));
        }
    }
}

void BSP_Stim_PulseTickISR(void)
{
    uint8_t ch;
    uint16_t raw;

    if (!s_enabled || !s_gate_on)
    {
        s_hb_state = HB_STATE_IDLE;
        s_cycle_tick_acc = 0U;
        hb_all_off();
        return;
    }

    /* Sample electrode sense ADC during active pulse for reliable contact detection */
    if (s_hb_state == HB_STATE_POS_PULSE || s_hb_state == HB_STATE_NEG_PULSE)
    {
        for (ch = 0U; ch < STIM_CH_COUNT; ch++)
        {
            raw = ADC1_GetChannelRaw((AdcChannelIndex_t)(ADC_IDX_ELEC1_IN + ch));
            if (raw > s_elec_peak_adc[ch]) s_elec_peak_adc[ch] = raw;
            if (raw >= ELEC_VALID_ADC_THRESH)
            {
                s_last_valid_pulse_ms[ch] = HAL_GetTick();
            }
        }
    }

    s_cycle_tick_acc++;

    switch (s_hb_state)
    {
        case HB_STATE_IDLE:
            if (s_cycle_tick_acc >= s_period_ticks)
            {
                s_cycle_tick_acc = 0U;
                s_state_ticks = 0U;
                s_hb_state = HB_STATE_POS_PULSE;
                for (ch = 0U; ch < STIM_CH_COUNT; ch++)
                {
                    if (s_target_level[ch] > 0U)
                    {
                        hb_write_ch(ch, GPIO_PIN_SET, GPIO_PIN_RESET);
                    }
                    else
                    {
                        hb_write_ch(ch, GPIO_PIN_RESET, GPIO_PIN_RESET);
                    }
                }
            }
            break;

        case HB_STATE_POS_PULSE:
            s_state_ticks++;
            if (s_state_ticks >= PULSE_WIDTH_TICKS)
            {
                s_state_ticks = 0U;
                s_hb_state = HB_STATE_DEADTIME;
                hb_all_off();
            }
            break;

        case HB_STATE_DEADTIME:
            s_state_ticks++;
            if (s_state_ticks >= PULSE_DEADTIME_TICKS)
            {
                s_state_ticks = 0U;
                s_hb_state = HB_STATE_NEG_PULSE;
                for (ch = 0U; ch < STIM_CH_COUNT; ch++)
                {
                    if (s_target_level[ch] > 0U)
                    {
                        hb_write_ch(ch, GPIO_PIN_RESET, GPIO_PIN_SET);
                    }
                    else
                    {
                        hb_write_ch(ch, GPIO_PIN_RESET, GPIO_PIN_RESET);
                    }
                }
            }
            break;

        case HB_STATE_NEG_PULSE:
            s_state_ticks++;
            if (s_state_ticks >= PULSE_WIDTH_TICKS)
            {
                s_state_ticks = 0U;
                s_hb_state = HB_STATE_IDLE;
                hb_all_off();
            }
            break;

        default:
            s_hb_state = HB_STATE_IDLE;
            hb_all_off();
            break;
    }
}

void BSP_Stim_GetStatus(StimStatus_t *out)
{
    uint8_t ch;
    if (out == NULL) return;
    memset(out, 0, sizeof(*out));
    out->output_enabled = s_enabled;
    out->active_freq_hz = s_runtime_freq_hz;
    for (ch = 0U; ch < STIM_CH_COUNT; ch++)
    {
        out->target_level[ch]      = s_target_level[ch];
        out->effective_level[ch]   = (uint8_t)(s_current_level_f[ch] + 0.5f);
        out->electrode_dropped[ch] = s_elec_dropped[ch];
        out->boost_mv[ch]          = s_boost_mv[ch];
        out->elec_adc_mv[ch]       = (uint16_t)((uint32_t)ADC1_GetChannelRaw((AdcChannelIndex_t)(ADC_IDX_ELEC1_IN + ch)) * 3300U / 4095U);
    }
}

uint8_t BSP_Stim_IsElectrodeDropped(uint8_t ch)
{
    if (ch >= STIM_CH_COUNT) return 0U;
    return s_elec_dropped[ch];
}
