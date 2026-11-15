#include "bsp_audio.h"
#include "usart.h"
#include <string.h>

/* Set to 1 for N9300 standard 0x7E..0xEF protocol, 0 for compact 0xA0 3-byte protocol */
#ifndef AUDIO_USE_N9300_STD_FRAME
#define AUDIO_USE_N9300_STD_FRAME   1
#endif

#define AUDIO_RX_RING_SIZE  64U
#define AUDIO_FRAME_MAX_LEN 32U

static uint8_t  s_rx_ring[AUDIO_RX_RING_SIZE];
static volatile uint16_t s_rx_head = 0U;
static volatile uint16_t s_rx_tail = 0U;
static uint8_t  s_rx_byte_it = 0U;

static uint8_t  s_rx_frame[AUDIO_FRAME_MAX_LEN];
static uint8_t  s_rx_len = 0U;
static uint8_t  s_last_ack_cmd = 0U;
static uint8_t  s_online = 0U;

#define AUDIO_QUEUE_MAX     8U
static uint16_t s_play_queue[AUDIO_QUEUE_MAX];
static uint8_t  s_queue_cnt = 0U;
static uint8_t  s_queue_tail = 0U;
static uint32_t s_track_play_tick = 0U;
static uint8_t  s_busy_prev = 0U;

static void audio_send_bytes(const uint8_t *buf, uint16_t len)
{
    HAL_UART_Transmit(&huart2, (uint8_t *)buf, len, 50U);
}

#if AUDIO_USE_N9300_STD_FRAME
/* N9300 frame: [0x7E] [LEN] [CMD] [P0] [P1] [CHK] [0xEF]
 * LEN = number of bytes from LEN through CHK minus 1 (or CMD..P1 + 2), CHK = XOR of LEN..Pn
 */
static void n9300_send_cmd(uint8_t cmd, uint8_t p0, uint8_t p1, uint8_t has_2_params)
{
    uint8_t frame[8];
    uint8_t idx = 0U;
    uint8_t xor_chk = 0U;
    uint8_t i;

    frame[idx++] = 0x7EU;
    frame[idx++] = has_2_params ? 4U : 3U; /* LEN: LEN_byte + CMD + params */
    frame[idx++] = cmd;
    if (has_2_params)
    {
        frame[idx++] = p0;
        frame[idx++] = p1;
    }
    else
    {
        frame[idx++] = p0;
    }

    for (i = 1U; i < idx; i++)
    {
        xor_chk ^= frame[i];
    }
    frame[idx++] = xor_chk;
    frame[idx++] = 0xEFU;

    audio_send_bytes(frame, idx);
}
#endif

void BSP_Audio_Init(void)
{
    s_rx_head = 0U;
    s_rx_tail = 0U;
    s_rx_len = 0U;
    HAL_UART_Receive_IT(&huart2, &s_rx_byte_it, 1U);
    BSP_Audio_SetVolume(5U);
}

void BSP_Audio_OnUartRxCplt(void)
{
    uint16_t next = (uint16_t)((s_rx_head + 1U) % AUDIO_RX_RING_SIZE);
    if (next != s_rx_tail)
    {
        s_rx_ring[s_rx_head] = s_rx_byte_it;
        s_rx_head = next;
    }
    HAL_UART_Receive_IT(&huart2, &s_rx_byte_it, 1U);
}

void BSP_Audio_Poll(void)
{
    while (s_rx_tail != s_rx_head)
    {
        uint8_t b = s_rx_ring[s_rx_tail];
        s_rx_tail = (uint16_t)((s_rx_tail + 1U) % AUDIO_RX_RING_SIZE);

        if (s_rx_len == 0U && b != 0x7EU)
        {
            continue;
        }
        if (s_rx_len < AUDIO_FRAME_MAX_LEN)
        {
            s_rx_frame[s_rx_len++] = b;
            if (b == 0xEFU && s_rx_len >= 5U)
            {
                s_last_ack_cmd = s_rx_frame[2];
                s_online = 1U;
                s_rx_len = 0U;
            }
        }
        else
        {
            s_rx_len = 0U;
        }
    }

    /* 链式多段语音连续播放调度 */
    if (s_queue_cnt > 0U)
    {
        uint32_t now = HAL_GetTick();
        uint8_t busy = BSP_Audio_IsBusy();

        /* 开始播放后至少经过 200ms 防抖才检测 busy 变为空闲，或超时 4000ms 兜底切下一首 */
        if ((now - s_track_play_tick) > 200U)
        {
            if ((s_busy_prev && !busy) || ((now - s_track_play_tick) > 4000U))
            {
                s_queue_tail = (uint8_t)((s_queue_tail + 1U) % AUDIO_QUEUE_MAX);
                s_queue_cnt--;
                if (s_queue_cnt > 0U)
                {
                    BSP_Audio_PlayTrack(s_play_queue[s_queue_tail]);
                    s_track_play_tick = now;
                    s_busy_prev = 1U;
                }
            }
        }
        s_busy_prev = busy;
    }
}

void BSP_Audio_SetVolume(uint8_t ui_vol_0_to_10)
{
    uint8_t hw_vol;
    if (ui_vol_0_to_10 > AUDIO_VOL_UI_MAX) ui_vol_0_to_10 = AUDIO_VOL_UI_MAX;
    hw_vol = (uint8_t)((uint16_t)ui_vol_0_to_10 * AUDIO_VOL_HW_MAX / AUDIO_VOL_UI_MAX);

#if AUDIO_USE_N9300_STD_FRAME
    n9300_send_cmd(0x31U, hw_vol, 0U, 0U); /* 0x31: Set volume (0~30) */
#else
    uint8_t c[3] = {0xA0U, 0x0EU, ui_vol_0_to_10};
    audio_send_bytes(c, 3U);
#endif
}

void BSP_Audio_PlayTrack(uint16_t track_id)
{
#if AUDIO_USE_N9300_STD_FRAME
    n9300_send_cmd(0x41U, (uint8_t)(track_id >> 8), (uint8_t)(track_id & 0xFFU), 1U); /* 0x41: Play track by index */
#else
    uint8_t c[3] = {0xA0U, 0x01U, (uint8_t)(track_id & 0xFFU)};
    audio_send_bytes(c, 3U);
#endif
}

void BSP_Audio_PlayVoice(AudioVoiceId_t id)
{
    s_queue_cnt = 0U;
    BSP_Audio_PlayTrack((uint16_t)id);
}

void BSP_Audio_PlayQueue(const uint16_t *tracks, uint8_t count)
{
    uint8_t i;
    if (tracks == NULL || count == 0U) return;
    if (count > AUDIO_QUEUE_MAX) count = AUDIO_QUEUE_MAX;

    s_queue_cnt  = 0U;
    s_queue_tail = 0U;

    for (i = 0U; i < count; i++)
    {
        s_play_queue[i] = tracks[i];
    }
    s_queue_cnt  = count;
    s_queue_tail = 0U;

    BSP_Audio_PlayTrack(s_play_queue[0]);
    s_track_play_tick = HAL_GetTick();
    s_busy_prev = 1U;
}

void BSP_Audio_PlayBioResult(uint8_t bpm)
{
    uint16_t seq[3];
    if (bpm < 5U) bpm = 5U;
    if (bpm > 10U) bpm = 10U;

    seq[0] = (uint16_t)AUDIO_VOICE_BIO_PREFIX;               /* 0008: "本次你的起始呼吸引导频率为" */
    seq[1] = AUDIO_TRACK_FREQ_BASE + (uint16_t)(bpm - 5U);  /* 0014 ~ 0019: "xx次每分钟" */
    seq[2] = (uint16_t)AUDIO_VOICE_CLICK_START_PROMPT;       /* 0009: "请点击启停键或右下角设置按钮设置好参数后开启治疗" */

    BSP_Audio_PlayQueue(seq, 3U);
}

void BSP_Audio_PlayTreatEnd(uint8_t timer_min)
{
    uint16_t seq[3];
    if (timer_min < 5U) timer_min = 5U;
    if (timer_min > 30U) timer_min = 30U;

    seq[0] = (uint16_t)AUDIO_VOICE_TREAT_END_PREFIX;            /* 0010: "本次治疗结束，有效时长为" */
    seq[1] = AUDIO_TRACK_TIME_BASE + (uint16_t)(timer_min - 5U); /* 0020 ~ 0045: "xx分钟" */
    seq[2] = (uint16_t)AUDIO_VOICE_POWER_OFF_PROMPT;            /* 0012: "本次治疗结束，请关闭仪器" */

    BSP_Audio_PlayQueue(seq, 3U);
}

void BSP_Audio_Stop(void)
{
    s_queue_cnt = 0U;
#if AUDIO_USE_N9300_STD_FRAME
    n9300_send_cmd(0x0EU, 0U, 0U, 0U); /* 0x0E: Stop playback */
#else
    uint8_t c[3] = {0xA0U, 0x03U, 0x00U};
    audio_send_bytes(c, 3U);
#endif
}

void BSP_Audio_Pause(void)
{
#if AUDIO_USE_N9300_STD_FRAME
    n9300_send_cmd(0x02U, 0U, 0U, 0U); /* 0x02: Pause */
#else
    BSP_Audio_Stop();
#endif
}

uint8_t BSP_Audio_IsBusy(void)
{
    /* N9300 Pin 16 BUSY: 播放时输出低电平(RESET)，空闲时输出高电平(SET) */
    return (HAL_GPIO_ReadPin(AUDIO_BUSY_GPIO_Port, AUDIO_BUSY_Pin) == GPIO_PIN_RESET) ? 1U : 0U;
}

uint8_t BSP_Audio_IsHeadphoneInserted(void)
{
    /* HP_DET (PA4) pulled up to 3.3V when inserted -> shuts down 8002A speaker amp */
    return (HAL_GPIO_ReadPin(HP_DET_GPIO_Port, HP_DET_Pin) == GPIO_PIN_SET) ? 1U : 0U;
}

void BSP_Audio_GetStatus(AudioStatus_t *out)
{
    if (out == NULL) return;
    out->busy = BSP_Audio_IsBusy();
    out->headphone_inserted = BSP_Audio_IsHeadphoneInserted();
    out->last_ack_cmd = s_last_ack_cmd;
    out->online = s_online;
}
