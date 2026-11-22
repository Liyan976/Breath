#include "algo_breath.h"
#include <math.h>
#include <string.h>

#define BREATH_BUF_LEN          64U
#define VALLEY_MIN_INTERVAL_MS  2000U  /* <= 30 BPM (真实人体呼吸在6~20次/分，滤除微小高频抖动) */
#define VALLEY_MAX_INTERVAL_MS  12000U /* >= 5 BPM (最长间隔12秒) */
#define VALLEY_AMP_MIN_THRESH   8.0f   /* 关键修复（问题8）：波谷识别阈值从25.0f恢复为8.0f(~6.4mV)，真实人体呼吸可灵敏识别 */
#define GBPM_MIN                5U
#define GBPM_MAX                10U

static float    s_buf[BREATH_BUF_LEN];
static uint32_t s_sample_cnt = 0U;
static float    s_baseline = 0.0f;
static float    s_lpf = 0.0f;
static float    s_amp_env = 0.0f;
static uint32_t s_last_valley_ms = 0U;
static uint32_t s_now_ms = 0U;
static uint32_t s_last_signal_ms = 0U;

static float    s_cbpm_win[3] = {0.0f, 0.0f, 0.0f};
static uint8_t  s_cbpm_win_idx = 0U;
static uint32_t s_cbpm_total = 0U;
static uint8_t  s_bio_gbpm = 10U;
static uint8_t  s_real_cbpm = 0U;
static uint8_t  s_calib_ready = 0U;

/* Unified 4:6 phase time-axis */
static BreathPhase_t s_phase = BREATH_PHASE_IDLE;
static uint32_t      s_phase_elapsed_ms = 0U;
static uint8_t       s_active_bpm = 10U;
static uint8_t       s_guide_just_started = 0U; /* 仅在启动引导时触发一次初始相位变更 */

static void on_valid_valley(uint32_t now_ms)
{
    uint32_t dt;
    float cbpm;
    float avg3;

    if (s_last_valley_ms == 0U)
    {
        s_last_valley_ms = now_ms;
        return;
    }

    dt = now_ms - s_last_valley_ms;
    if (dt < VALLEY_MIN_INTERVAL_MS || dt > VALLEY_MAX_INTERVAL_MS)
    {
        if (dt > VALLEY_MAX_INTERVAL_MS) s_last_valley_ms = now_ms;
        return;
    }

    s_last_valley_ms = now_ms;
    cbpm = 60000.0f / (float)dt;
    s_real_cbpm = (uint8_t)floorf(cbpm + 0.5f);

    if (s_cbpm_win_idx < 3U)
    {
        s_cbpm_win[s_cbpm_win_idx++] = cbpm;
    }
    else
    {
        s_cbpm_win[0] = s_cbpm_win[1];
        s_cbpm_win[1] = s_cbpm_win[2];
        s_cbpm_win[2] = cbpm;
    }
    s_cbpm_total++;

    if (!s_calib_ready)
    {
        if (s_cbpm_total >= 2U)
        {
            float avg = 0.0f;
            uint8_t k;
            for (k = 0U; k < s_cbpm_win_idx; k++) avg += s_cbpm_win[k];
            avg /= (float)s_cbpm_win_idx;

            int32_t g0 = (int32_t)floorf(avg + 0.5f);
            if (g0 < (int32_t)GBPM_MIN) g0 = GBPM_MIN;
            if (g0 > (int32_t)GBPM_MAX) g0 = GBPM_MAX;
            s_bio_gbpm = (uint8_t)g0;
            s_calib_ready = 1U;
        }
    }
    else
    {
        avg3 = (s_cbpm_win[0] + s_cbpm_win[1] + s_cbpm_win[2]) / 3.0f;
        if (avg3 <= (float)s_bio_gbpm)
        {
            uint8_t step = (s_bio_gbpm > 10U) ? 2U : 1U;
            if (s_bio_gbpm > (GBPM_MIN + step))
            {
                s_bio_gbpm -= step;
            }
            else
            {
                s_bio_gbpm = GBPM_MIN;
            }
        }
    }
}

void Algo_Breath_Init(void)
{
    memset(s_buf, 0, sizeof(s_buf));
    s_sample_cnt = 0U;
    s_baseline = 0.0f;
    s_lpf = 0.0f;
    s_amp_env = 0.0f;
    s_now_ms = 0U;
    s_last_signal_ms = 0U;
    s_last_valley_ms = 0U;
    s_bio_gbpm = 10U;
    s_real_cbpm = 0U;
    s_calib_ready = 0U;
    s_cbpm_win_idx = 0U;
    s_cbpm_total = 0U;
    s_phase = BREATH_PHASE_IDLE;
    s_phase_elapsed_ms = 0U;
    s_active_bpm = 10U;
}

void Algo_Breath_ResetBioCalib(void)
{
    s_last_signal_ms = 0U;
    s_last_valley_ms = 0U;
    s_cbpm_win_idx = 0U;
    s_cbpm_total = 0U;
    s_calib_ready = 0U;
    s_real_cbpm = 0U;
    s_bio_gbpm = 10U;
    s_amp_env = 0.0f;
}

void Algo_Breath_FeedSample(uint16_t raw_adc, uint32_t now_ms)
{
    float raw = (float)raw_adc;
    float ac;
    float k_recent; /* Rising slope after valley */
    float k_prev;   /* Falling slope into valley */
    float valley_val;
    float th;
    uint32_t n;

    s_now_ms = now_ms;
    if (s_sample_cnt == 0U)
    {
        s_baseline = raw;
    }
    /* 关键修复（问题2）：基线跟踪时间常数放缓至约10秒（50Hz下系数 0.002），防止将真实人体的慢呼吸信号作为漂移滤除 */
    s_baseline = 0.998f * s_baseline + 0.002f * raw;
    ac = raw - s_baseline;
    s_lpf = 0.85f * s_lpf + 0.15f * ac;

    /* 当检测到有效呼吸扰动幅度 (>= 25.0f) 或已成功检测到波谷时，刷新信号活跃时间戳 */
    if (s_sample_cnt >= 25U && (fabsf(s_lpf) >= VALLEY_AMP_MIN_THRESH || s_cbpm_total > 0U))
    {
        s_last_signal_ms = now_ms;
    }

    s_buf[s_sample_cnt % BREATH_BUF_LEN] = s_lpf;
    s_sample_cnt++;
    if (s_sample_cnt < 9U) return;

    n = s_sample_cnt;
    /* PDF eq: averaged 1st-order differences around candidate valley fmin at (n-5) */
    k_recent = (s_buf[(n - 1U) % BREATH_BUF_LEN] + s_buf[(n - 2U) % BREATH_BUF_LEN]
              - s_buf[(n - 3U) % BREATH_BUF_LEN] - s_buf[(n - 4U) % BREATH_BUF_LEN]) * 0.5f;
    k_prev   = (s_buf[(n - 5U) % BREATH_BUF_LEN] + s_buf[(n - 6U) % BREATH_BUF_LEN]
              - s_buf[(n - 7U) % BREATH_BUF_LEN] - s_buf[(n - 8U) % BREATH_BUF_LEN]) * 0.5f;

    valley_val = s_buf[(n - 5U) % BREATH_BUF_LEN];
    s_amp_env = 0.98f * s_amp_env + 0.02f * fabsf(s_lpf);
    th = s_amp_env * 0.25f;
    if (th < VALLEY_AMP_MIN_THRESH) th = VALLEY_AMP_MIN_THRESH;

    /* Valid valley: falling before (k_prev < 0), rising after (k_recent > 0), and below negative threshold */
    if (k_prev < 0.0f && k_recent > 0.0f && valley_val < -th)
    {
        on_valid_valley(now_ms);
    }
}

uint8_t Algo_Breath_GetSimpleBPM(uint32_t active_elapsed_ms)
{
    static const uint16_t seg_sec[5] = {100U, 125U, 150U, 175U, 200U};
    uint32_t elapsed_s = active_elapsed_ms / 1000U;
    uint32_t acc = 0U;
    uint8_t i;

    for (i = 0U; i < 5U; i++)
    {
        acc += seg_sec[i];
        if (elapsed_s < acc)
        {
            return (uint8_t)(10U - i);
        }
    }
    return GBPM_MIN;
}

void Algo_Breath_StartGuide(uint8_t initial_bpm)
{
    if (initial_bpm < GBPM_MIN) initial_bpm = GBPM_MIN;
    if (initial_bpm > GBPM_MAX) initial_bpm = GBPM_MAX;
    s_active_bpm = initial_bpm;
    s_phase = BREATH_PHASE_INHALE;
    s_phase_elapsed_ms = 0U;
    s_guide_just_started = 1U;
}

void Algo_Breath_StopGuide(void)
{
    s_phase = BREATH_PHASE_IDLE;
    s_phase_elapsed_ms = 0U;
    s_guide_just_started = 0U;
}

void Algo_Breath_StepGuide(uint32_t dt_ms, uint8_t target_bpm, BreathGuideOut_t *out)
{
    uint32_t cycle_ms;
    uint32_t inhale_ms;
    uint32_t exhale_ms;

    if (out == NULL) return;
    memset(out, 0, sizeof(*out));

    if (s_phase == BREATH_PHASE_IDLE)
    {
        out->phase = BREATH_PHASE_IDLE;
        out->lung_pct = 0U;
        out->guide_bpm = target_bpm;
        out->real_cbpm = s_real_cbpm;
        out->calib_ready = s_calib_ready;
        return;
    }

    if (target_bpm < GBPM_MIN) target_bpm = GBPM_MIN;
    if (target_bpm > GBPM_MAX) target_bpm = GBPM_MAX;

    if (s_guide_just_started != 0U)
    {
        /* 关键修复（问题2）：仅在刚启动引导的首个周期置位一次 phase_changed，
         * 杜绝后续呼气结束回到吸气时因 s_phase_elapsed_ms==0 连发两个 phase_changed 导致“吸、吸”双重播报 */
        s_guide_just_started = 0U;
        s_active_bpm = target_bpm;
        out->phase_changed = 1U;
    }

    s_phase_elapsed_ms += dt_ms;
    cycle_ms  = 60000UL / (uint32_t)s_active_bpm;
    inhale_ms = (cycle_ms * 4U) / 10U; /* Strict 4:6 inhale/exhale ratio */
    exhale_ms = cycle_ms - inhale_ms;

    if (s_phase == BREATH_PHASE_INHALE)
    {
        if (s_phase_elapsed_ms >= inhale_ms)
        {
            s_phase = BREATH_PHASE_EXHALE;
            s_phase_elapsed_ms = 0U;
            out->phase_changed = 1U;
            out->lung_pct = 100U;
        }
        else
        {
            out->lung_pct = (uint8_t)((s_phase_elapsed_ms * 100UL) / inhale_ms);
        }
    }
    else /* BREATH_PHASE_EXHALE */
    {
        if (s_phase_elapsed_ms >= exhale_ms)
        {
            s_phase = BREATH_PHASE_INHALE;
            s_phase_elapsed_ms = 0U;
            s_active_bpm = target_bpm;
            out->phase_changed = 1U;
            out->lung_pct = 0U;
        }
        else
        {
            uint32_t down = (s_phase_elapsed_ms * 100UL) / exhale_ms;
            out->lung_pct = (down <= 100U) ? (uint8_t)(100U - down) : 0U;
        }
    }

    out->phase = s_phase;
    out->guide_bpm = s_active_bpm;
    out->real_cbpm = s_real_cbpm;
    out->calib_ready = s_calib_ready;
}

uint8_t Algo_Breath_GetBioGBPM(void)
{
    return s_bio_gbpm;
}

void Algo_Breath_SetBioGBPM(uint8_t bpm)
{
    if (bpm < GBPM_MIN) bpm = GBPM_MIN;
    if (bpm > GBPM_MAX) bpm = GBPM_MAX;
    s_bio_gbpm = bpm;
    s_calib_ready = 1U;
}

uint8_t Algo_Breath_GetRealCBPM(void)
{
    return s_real_cbpm;
}

uint8_t Algo_Breath_IsBioCalibReady(void)
{
    return s_calib_ready;
}

uint8_t Algo_Breath_IsSignalDetected(void)
{
    if (s_sample_cnt >= 25U && s_last_signal_ms != 0U)
    {
        if ((s_now_ms - s_last_signal_ms) <= 6500U)
        {
            return 1U;
        }
    }
    return 0U;
}

uint8_t Algo_Breath_FinalizeBioCalib(void)
{
    if (s_calib_ready)
    {
        return s_bio_gbpm;
    }

    if (s_cbpm_total >= 1U && s_cbpm_win_idx > 0U)
    {
        float sum = 0.0f;
        uint8_t i;
        for (i = 0U; i < s_cbpm_win_idx; i++)
        {
            sum += s_cbpm_win[i];
        }
        sum /= (float)s_cbpm_win_idx;
        int32_t g0 = (int32_t)floorf(sum + 0.5f);
        if (g0 < (int32_t)GBPM_MIN) g0 = GBPM_MIN;
        if (g0 > (int32_t)GBPM_MAX) g0 = GBPM_MAX;
        s_bio_gbpm = (uint8_t)g0;
        s_calib_ready = 1U;
        return s_bio_gbpm;
    }

    return GBPM_MIN; /* Fallback to 5 BPM */
}
