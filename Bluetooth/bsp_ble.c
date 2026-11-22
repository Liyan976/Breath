#include "bsp_ble.h"
#include "usart.h"
#include <string.h>

#define BLE_RX_RING_SIZE    128U
#define BLE_FRAME_MAX_LEN   40U

static uint8_t  s_rx_ring[BLE_RX_RING_SIZE];
static volatile uint16_t s_rx_head = 0U;
static volatile uint16_t s_rx_tail = 0U;
static uint8_t  s_rx_byte_it = 0U;
static volatile uint8_t s_data_ready_flag = 0U;

static uint8_t  s_rx_frame[BLE_FRAME_MAX_LEN];
static uint8_t  s_rx_pos = 0U;
static uint8_t  s_expected_len = 0U;
static uint8_t  s_ble_enabled = 0U;
static BleRxCallback_t s_rx_cb = NULL;

static uint8_t ble_crc8(const uint8_t *data, uint8_t len)
{
    uint8_t crc = 0x00U;
    uint8_t i, b;
    for (i = 0U; i < len; i++)
    {
        crc ^= data[i];
        for (b = 0U; b < 8U; b++)
        {
            crc = (crc & 0x80U) ? (uint8_t)((crc << 1) ^ 0x07U) : (uint8_t)(crc << 1);
        }
    }
    return crc;
}

void BSP_BLE_Init(BleRxCallback_t cb)
{
    s_rx_cb = cb;
    s_rx_head = 0U;
    s_rx_tail = 0U;
    s_rx_pos = 0U;
    s_expected_len = 0U;
    s_data_ready_flag = 0U;

    HAL_GPIO_WritePin(BLE_MOD_GPIO_Port, BLE_MOD_Pin, GPIO_PIN_SET); /* Transparent mode */
    BSP_BLE_SetEnabled(0U); /* 默认关闭蓝牙：拉低 BLE_RST 强行硬件复位，禁止广播与搜索 */
    HAL_UART_Receive_IT(&huart1, &s_rx_byte_it, 1U);
}

void BSP_BLE_Reset(void)
{
    HAL_GPIO_WritePin(BLE_RST_GPIO_Port, BLE_RST_Pin, GPIO_PIN_RESET);
    HAL_Delay(10U);
    if (s_ble_enabled)
    {
        HAL_GPIO_WritePin(BLE_RST_GPIO_Port, BLE_RST_Pin, GPIO_PIN_SET);
    }
}

void BSP_BLE_SetEnabled(uint8_t enable)
{
    s_ble_enabled = enable ? 1U : 0U;
    if (s_ble_enabled)
    {
        /* 开启蓝牙：拉高 BLE_RST 解除硬件复位，拉低 BLE_WKP 唤醒串口通信，开始广播供 App 搜索连接 */
        HAL_GPIO_WritePin(BLE_WKP_GPIO_Port, BLE_WKP_Pin, GPIO_PIN_RESET);
        HAL_GPIO_WritePin(BLE_RST_GPIO_Port, BLE_RST_Pin, GPIO_PIN_SET);
    }
    else
    {
        /* 关闭蓝牙：拉低 BLE_RST 强行将 E104-BT5005A 锁定在复位态，射频彻底关断，App 无法搜索或连接 */
        HAL_GPIO_WritePin(BLE_RST_GPIO_Port, BLE_RST_Pin, GPIO_PIN_RESET);
        HAL_GPIO_WritePin(BLE_WKP_GPIO_Port, BLE_WKP_Pin, GPIO_PIN_SET);
    }
}

uint8_t BSP_BLE_IsConnected(void)
{
    /* E104-BT5005A LINK pin outputs LOW when BLE connection is active */
    return (HAL_GPIO_ReadPin(BLE_LINK_GPIO_Port, BLE_LINK_Pin) == GPIO_PIN_RESET) ? 1U : 0U;
}

void BSP_BLE_OnUartRxCplt(void)
{
    uint16_t next = (uint16_t)((s_rx_head + 1U) % BLE_RX_RING_SIZE);
    if (next != s_rx_tail)
    {
        s_rx_ring[s_rx_head] = s_rx_byte_it;
        s_rx_head = next;
    }
    HAL_UART_Receive_IT(&huart1, &s_rx_byte_it, 1U);
}

void BSP_BLE_OnDataReadyExti(void)
{
    s_data_ready_flag = 1U;
}

void BSP_BLE_Poll(void)
{
    (void)s_data_ready_flag;
    s_data_ready_flag = 0U;

    while (s_rx_tail != s_rx_head)
    {
        uint8_t b = s_rx_ring[s_rx_tail];
        s_rx_tail = (uint16_t)((s_rx_tail + 1U) % BLE_RX_RING_SIZE);

        if (s_rx_pos == 0U)
        {
            if (b == BLE_FRAME_SOF0) s_rx_frame[s_rx_pos++] = b;
            continue;
        }
        if (s_rx_pos == 1U)
        {
            if (b == BLE_FRAME_SOF1) s_rx_frame[s_rx_pos++] = b;
            else s_rx_pos = (b == BLE_FRAME_SOF0) ? 1U : 0U;
            continue;
        }
        if (s_rx_pos == 2U)
        {
            /* LEN = cmd + payload bytes (1..25) */
            if (b >= 1U && b <= 25U)
            {
                s_expected_len = (uint8_t)(b + 5U); /* SOF0,SOF1,LEN + (LEN) + CRC + EOF */
                s_rx_frame[s_rx_pos++] = b;
            }
            else
            {
                s_rx_pos = 0U;
            }
            continue;
        }

        if (s_rx_pos < BLE_FRAME_MAX_LEN)
        {
            s_rx_frame[s_rx_pos++] = b;
        }
        else
        {
            s_rx_pos = 0U;
            continue;
        }

        if (s_rx_pos >= s_expected_len)
        {
            uint8_t len_field = s_rx_frame[2];
            uint8_t calc_crc = ble_crc8(&s_rx_frame[2], (uint8_t)(1U + len_field));
            uint8_t rx_crc   = s_rx_frame[3U + len_field];
            uint8_t rx_eof   = s_rx_frame[4U + len_field];

            if (rx_eof == BLE_FRAME_EOF && calc_crc == rx_crc && s_rx_cb != NULL)
            {
                BleRxCommand_t cmd;
                memset(&cmd, 0, sizeof(cmd));
                cmd.cmd_id = s_rx_frame[3];
                cmd.payload_len = (len_field > 1U) ? (uint8_t)(len_field - 1U) : 0U;
                if (cmd.payload_len > 0U)
                {
                    memcpy(cmd.payload, &s_rx_frame[4], cmd.payload_len);
                }
                s_rx_cb(&cmd);
            }
            s_rx_pos = 0U;
        }
    }
}

void BSP_BLE_SendTelemetry(const BleTelemetry_t *t)
{
    uint8_t frame[24];
    uint8_t idx = 0U;

    if (t == NULL || !s_ble_enabled) return;

    frame[idx++] = BLE_FRAME_SOF0;
    frame[idx++] = BLE_FRAME_SOF1;
    frame[idx++] = 16U; /* LEN: 1(cmd) + 15(payload) */
    frame[idx++] = BLE_CMD_TELEMETRY;
    frame[idx++] = t->breath_mode;
    frame[idx++] = t->stim_mode;
    frame[idx++] = t->stim_freq_hz;
    frame[idx++] = t->stim_level[0];
    frame[idx++] = t->stim_level[1];
    frame[idx++] = t->stim_level[2];
    frame[idx++] = t->timer_min;
    frame[idx++] = (uint8_t)(t->elapsed_sec >> 8);
    frame[idx++] = (uint8_t)(t->elapsed_sec & 0xFFU);
    frame[idx++] = (uint8_t)(t->remain_sec >> 8);
    frame[idx++] = (uint8_t)(t->remain_sec & 0xFFU);
    frame[idx++] = t->guide_bpm;
    frame[idx++] = t->real_cbpm;
    frame[idx++] = t->elec_drop_mask;
    frame[idx++] = (uint8_t)((t->running ? 1U : 0U) | (t->paused ? 2U : 0U));
    frame[idx]   = ble_crc8(&frame[2], 17U);
    idx++;
    frame[idx++] = BLE_FRAME_EOF;

    HAL_UART_Transmit(&huart1, frame, idx, 50U);
}
