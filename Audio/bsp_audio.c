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
    BSP_Audio_PlayTrack((uint16_t)id);
}

void BSP_Audio_Stop(void)
{
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
    return (HAL_GPIO_ReadPin(AUDIO_BUSY_GPIO_Port, AUDIO_BUSY_Pin) == GPIO_PIN_SET) ? 1U : 0U;
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
