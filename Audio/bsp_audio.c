/**
  ******************************************************************************
  * @file    bsp_audio.c
  * @brief   N930X（N9300）MP3 语音芯片驱动  —— 协议修正与状态机增强版
  *
  * 【协议规范】
  * N930X 数据手册 4.3 节规定 UART 帧格式为：
  *     [CMD] [~CMD] [LEN] [Data1] ... [DataN] [SUM]
  *      命令码  命令码反码  数据个数    数据       所有字节之和的低 8 位
  *
  * 例：指定路径播放 /0001.mp3 —— 04 FB 0A 07 2F 30 30 30 31 2E 6D 70 33 3E
  * 例：设置音量为 20 (0~30)    —— 06 F9 02 01 14 16
  *
  * 【时序要求】
  * 1) 上电后约 5s 内芯片在做初始化，此期间发的指令一律无效 —— 等待 5s 就绪门闸；
  * 2) 任意两条指令之间间隔 500ms 以上；
  * 3) 切盘符后等待 200ms 以上才能发指定曲目。
  *
  * 【稳定性保证】
  * 1) 发送采用轮询 USART_SR_TXE/TC 寄存器直接写 DR，不占 huart2 Lock，避免与
  *    RX 中断里的 HAL_UART_Receive_IT(&huart2) 产生锁竞争死锁；
  * 2) 播放队列与单首播放互斥防抖，正在播报或排队中的相同曲目不重复下发；
  * 3) 超时兜底由 8s 加大为 30s，确保 0007.mp3 等长语音完整播报。
  ******************************************************************************
  */

#include "bsp_audio.h"
#include "usart.h"
#include "FreeRTOS.h"
#include "task.h"
#include <string.h>

/* ======================= 可调参数 ======================= */
#define AUDIO_POWER_ON_WAIT_MS   5000U  /* N9300 上电初始化静默期，5s 内不发指令 */
#define AUDIO_CMD_GAP_MS          500U  /* 两条指令最小间隔（数据手册要求） */
#define AUDIO_SEG_MIN_MS          300U  /* 播放指令后至少多久才允许判定“播完” */
#define AUDIO_SEG_TIMEOUT_MS    30000U  /* 单段语音超时兜底（加大至 30s，支持 0007 长语音） */
#define AUDIO_HALT_HOLD_MS        300U  /* Stop 的合并窗口：窗口内来了播放请求就作废 */

#define AUDIO_RX_RING_SIZE         64U
#define AUDIO_FRAME_MAX_LEN        32U
#define AUDIO_FIFO_MAX              8U
#define AUDIO_QUEUE_MAX             8U

/* ---------- 曲目定位方式 ---------- */
#define AUDIO_USE_PATH_PLAY         1U  /* 1=按文件名播放 "/0001.mp3"（推荐，与文件名绑定）
                                           0=按 FAT 顺序号播放（06 命令） */

/* ---------- 开机循环播放测试（默认关闭） ---------- */
#define AUDIO_BOOT_LOOP_TEST        0U  /* 1=开机自动循环播放；0=正常按事件播放 */
#define AUDIO_BOOT_LOOP_FIRST       1U
#define AUDIO_BOOT_LOOP_LAST        45U
#define AUDIO_BOOT_LOOP_INTERVAL_MS 2000U

/* ======================= N930X 指令码 ======================= */
#define N9_CMD_PLAY         0x04U   /* 播放控制族，反码 0xFB */
#define N9_ACT_QUERY_STATE  0x00U   /* 查询播放状态 */
#define N9_ACT_PLAY         0x01U   /* 播放 */
#define N9_ACT_PAUSE        0x02U   /* 暂停 */
#define N9_ACT_STOP         0x03U   /* 停止 */
#define N9_ACT_SET_TRACK    0x06U   /* 指定曲目播放（曲目号 2 字节：高、低） */
#define N9_ACT_PLAY_PATH    0x07U   /* 当前盘符指定路径播放（路径 + 1 个 LEN） */
#define N9_ACT_SW_FLASH     0x0CU   /* 切换到 SPI-FLASH 盘符 */

#define N9_CMD_VOLUME       0x06U   /* 音量控制族，反码 0xF9 */
#define N9_ACT_SET_VOL      0x01U   /* 设置音量（0~30） */

/* 内部待发指令类型 */
#define AOP_NONE   0x00U
#define AOP_PLAY   0x01U
#define AOP_VOL    0x02U
#define AOP_STOP   0x03U
#define AOP_PAUSE  0x04U
#define AOP_FLASH  0x05U

typedef struct {
    uint8_t  type;
    uint16_t arg;
} audio_req_t;

/* ======================= 内部变量 ======================= */
static uint8_t  s_rx_ring[AUDIO_RX_RING_SIZE];
static volatile uint16_t s_rx_head = 0U;
static volatile uint16_t s_rx_tail = 0U;
static uint8_t  s_rx_byte_it = 0U;

static uint8_t  s_rx_frame[AUDIO_FRAME_MAX_LEN];
static uint8_t  s_rx_len = 0U;

static volatile uint8_t s_last_ack_cmd = 0U;   /* 最近应答的 CMD */
static volatile uint8_t s_online       = 0U;   /* 收到过合法应答即认为在线 */
static volatile uint8_t s_last_error   = 0U;   /* AA 55 02 FF xx 中的 xx */

static uint32_t s_power_on_tick = 0U;
static uint8_t  s_ready         = 0U;          /* 上电初始化 5s 门闸 */
static uint32_t s_last_tx_tick  = 0U;

/* 指令 FIFO */
static audio_req_t s_req[AUDIO_FIFO_MAX];
static uint8_t     s_req_head = 0U;
static uint8_t     s_req_tail = 0U;
static uint8_t     s_req_cnt  = 0U;

/* 多段排队队列 */
static uint16_t s_queue[AUDIO_QUEUE_MAX];
static uint8_t  s_q_count       = 0U;
static uint8_t  s_q_idx         = 0U;

/* 当前段播放状态 */
static uint8_t  s_seg_active     = 0U;
static uint8_t  s_seg_busy_seen  = 0U;         /* 本段是否已经看到 BUSY 拉低 */
static uint32_t s_seg_tick       = 0U;

/* 停止命令合并窗口 */
static uint8_t  s_halt_pending = 0U;
static uint32_t s_halt_tick    = 0U;

/* 开机循环播放测试状态 */
static uint8_t  s_loop_on       = 0U;
static uint16_t s_loop_first    = AUDIO_BOOT_LOOP_FIRST;
static uint16_t s_loop_last     = AUDIO_BOOT_LOOP_LAST;
static uint16_t s_loop_cur      = AUDIO_BOOT_LOOP_FIRST;
static uint32_t s_loop_interval = AUDIO_BOOT_LOOP_INTERVAL_MS;
static uint32_t s_loop_next_tick = 0U;
static uint16_t s_last_track    = 0U;           /* 最近一次下发的曲目号 */
static uint8_t  s_current_hw_vol = 0xFFU;
static uint8_t  s_pending_deferred_hw_vol = 0xFFU;

/* ======================= 底层：串口发送 ======================= */
static void uart2_tx_raw(const uint8_t *buf, uint16_t len)
{
    USART_TypeDef *u = huart2.Instance;
    uint16_t i;
    uint32_t guard;

    for (i = 0U; i < len; i++)
    {
        guard = 0U;
        while ((u->SR & USART_SR_TXE) == 0U)
        {
            if (++guard > 500000U) return;
        }
        u->DR = buf[i];
    }

    guard = 0U;
    while ((u->SR & USART_SR_TC) == 0U)
    {
        if (++guard > 500000U) return;
    }
}

static void n930x_send(uint8_t cmd, const uint8_t *data, uint8_t len)
{
    uint8_t frame[24];
    uint8_t i;
    uint8_t sum;

    if (len > 20U) return;

    frame[0] = cmd;
    frame[1] = (uint8_t)(~cmd);            /* 命令码反码 */
    frame[2] = len;                        /* 数据个数 */
    sum      = (uint8_t)(frame[0] + frame[1] + frame[2]);

    for (i = 0U; i < len; i++)
    {
        frame[3U + i] = data[i];
        sum = (uint8_t)(sum + data[i]);
    }
    frame[3U + len] = sum;                 /* 校验和 */

    uart2_tx_raw(frame, (uint16_t)(len + 4U));
}

static void n930x_play(uint16_t track_id)
{
#if (AUDIO_USE_PATH_PLAY != 0U)
    uint8_t d[13];
    uint8_t n = 0U;
    uint16_t t = track_id;

    d[n++] = N9_ACT_PLAY_PATH;
    d[n++] = '/';
    d[n++] = (uint8_t)('0' + ((t / 1000U) % 10U));
    d[n++] = (uint8_t)('0' + ((t / 100U) % 10U));
    d[n++] = (uint8_t)('0' + ((t / 10U) % 10U));
    d[n++] = (uint8_t)('0' + (t % 10U));
    d[n++] = '.';
    d[n++] = 'm';
    d[n++] = 'p';
    d[n++] = '3';

    n930x_send(N9_CMD_PLAY, d, n);
#else
    uint8_t d[3];
    d[0] = N9_ACT_SET_TRACK;
    d[1] = (uint8_t)(track_id >> 8);
    d[2] = (uint8_t)(track_id & 0xFFU);
    n930x_send(N9_CMD_PLAY, d, 3U);
#endif
}

static void n930x_volume(uint8_t vol_0_to_30)
{
    uint8_t d[2];
    d[0] = N9_ACT_SET_VOL;
    d[1] = vol_0_to_30;
    n930x_send(N9_CMD_VOLUME, d, 2U);
}

static void n930x_stop(void)
{
    uint8_t d[1] = { N9_ACT_STOP };
    n930x_send(N9_CMD_PLAY, d, 1U);
}

static void n930x_pause(void)
{
    uint8_t d[1] = { N9_ACT_PAUSE };
    n930x_send(N9_CMD_PLAY, d, 1U);
}

static void n930x_switch_flash(void)
{
    uint8_t d[1] = { N9_ACT_SW_FLASH };
    n930x_send(N9_CMD_PLAY, d, 1U);
}

/* ======================= 指令 FIFO ======================= */
static void audio_req_push(uint8_t type, uint16_t arg)
{
    taskENTER_CRITICAL();
    if (s_req_cnt == 0U)
    {
        s_req[s_req_tail].type = type;
        s_req[s_req_tail].arg  = arg;
        s_req_tail = (uint8_t)((s_req_tail + 1U) % AUDIO_FIFO_MAX);
        s_req_cnt  = 1U;
    }
    else if (s_req_cnt < AUDIO_FIFO_MAX)
    {
        s_req[s_req_tail].type = type;
        s_req[s_req_tail].arg  = arg;
        s_req_tail = (uint8_t)((s_req_tail + 1U) % AUDIO_FIFO_MAX);
        s_req_cnt++;
    }
    else
    {
        /* 队列满时覆盖最旧的一条，保证最新指令发出 */
        s_req[s_req_head].type = type;
        s_req[s_req_head].arg  = arg;
        s_req_head = (uint8_t)((s_req_head + 1U) % AUDIO_FIFO_MAX);
        s_req_tail = s_req_head;
    }
    taskEXIT_CRITICAL();
}

static void audio_req_replace_or_push(uint8_t type, uint16_t arg)
{
    uint8_t idx;
    uint8_t i;
    uint8_t found = 0U;

    taskENTER_CRITICAL();
    idx = s_req_head;
    for (i = 0U; i < s_req_cnt; i++)
    {
        if (s_req[idx].type == type)
        {
            s_req[idx].arg = arg;
            found = 1U;
            break;
        }
        idx = (uint8_t)((idx + 1U) % AUDIO_FIFO_MAX);
    }
    taskEXIT_CRITICAL();

    if (found == 0U) audio_req_push(type, arg);
}

static uint8_t audio_req_pop(audio_req_t *out)
{
    uint8_t ok = 0U;
    taskENTER_CRITICAL();
    if (s_req_cnt > 0U)
    {
        *out = s_req[s_req_head];
        s_req_head = (uint8_t)((s_req_head + 1U) % AUDIO_FIFO_MAX);
        s_req_cnt--;
        ok = 1U;
    }
    taskEXIT_CRITICAL();
    return ok;
}

static void audio_req_purge(uint8_t type)
{
    uint8_t rd;
    uint8_t wr;
    uint8_t cnt;
    uint8_t kept = 0U;
    uint8_t i;

    taskENTER_CRITICAL();
    cnt = s_req_cnt;
    rd  = s_req_head;
    wr  = s_req_head;
    for (i = 0U; i < cnt; i++)
    {
        audio_req_t r = s_req[rd];
        rd = (uint8_t)((rd + 1U) % AUDIO_FIFO_MAX);
        if (r.type == type) continue;
        s_req[wr] = r;
        wr = (uint8_t)((wr + 1U) % AUDIO_FIFO_MAX);
        kept++;
    }
    s_req_cnt  = kept;
    s_req_tail = wr;
    taskEXIT_CRITICAL();
}

static uint8_t audio_req_has(uint8_t type)
{
    uint8_t i;
    uint8_t idx;
    uint8_t found = 0U;
    taskENTER_CRITICAL();
    idx = s_req_head;
    for (i = 0U; i < s_req_cnt; i++)
    {
        if (s_req[idx].type == type) { found = 1U; break; }
        idx = (uint8_t)((idx + 1U) % AUDIO_FIFO_MAX);
    }
    taskEXIT_CRITICAL();
    return found;
}

/* ======================= 接收解析 ======================= */
static void audio_rx_parse(uint8_t b)
{
    uint8_t need;
    uint8_t len;
    uint8_t i;
    uint8_t sum = 0U;

    if (s_rx_len == 0U)
    {
        s_rx_frame[s_rx_len++] = b;
        return;
    }

    if (s_rx_len < AUDIO_FRAME_MAX_LEN)
    {
        s_rx_frame[s_rx_len++] = b;
    }
    else
    {
        s_rx_len = 0U;
        return;
    }

    if (s_rx_len < 3U) return;

    len = s_rx_frame[2];
    if (len > 20U)
    {
        s_rx_len = 0U;
        return;
    }
    need = (uint8_t)(3U + len + 1U);
    if (s_rx_len < need) return;

    /* 命令码 + 反码 校验 */
    if ((uint8_t)(s_rx_frame[0] ^ 0xFFU) != s_rx_frame[1])
    {
        s_rx_len = 0U;
        return;
    }
    /* 校验和 */
    for (i = 0U; i < need - 1U; i++) sum = (uint8_t)(sum + s_rx_frame[i]);
    if (sum != s_rx_frame[need - 1U])
    {
        s_rx_len = 0U;
        return;
    }

    s_last_ack_cmd = s_rx_frame[0];
    s_online = 1U;

    if (s_rx_frame[0] == 0xAAU && len >= 2U && s_rx_frame[3] == 0xFFU)
    {
        s_last_error = s_rx_frame[4];
    }
    else if (s_rx_frame[0] == 0x04U && len >= 1U)
    {
        s_last_error = 0U;
    }

    s_rx_len = 0U;
}

/* ======================= 对外接口 ======================= */
void BSP_Audio_Init(void)
{
    s_rx_head     = 0U;
    s_rx_tail     = 0U;
    s_rx_len      = 0U;
    s_req_head    = 0U;
    s_req_tail    = 0U;
    s_req_cnt     = 0U;
    s_q_count     = 0U;
    s_q_idx       = 0U;
    s_seg_active  = 0U;
    s_seg_busy_seen = 0U;
    s_ready       = 0U;
    s_power_on_tick = HAL_GetTick();
    s_last_tx_tick  = 0U;
    s_last_ack_cmd  = 0U;
    s_online        = 0U;
    s_last_error    = 0U;
    s_last_track    = 0U;
    s_halt_pending  = 0U;
    s_halt_tick     = 0U;
    s_pending_deferred_hw_vol = 0xFFU;

    s_loop_first     = AUDIO_BOOT_LOOP_FIRST;
    s_loop_last      = AUDIO_BOOT_LOOP_LAST;
    s_loop_cur       = AUDIO_BOOT_LOOP_FIRST;
    s_loop_interval  = AUDIO_BOOT_LOOP_INTERVAL_MS;
    s_loop_next_tick = s_power_on_tick + AUDIO_POWER_ON_WAIT_MS;
#if (AUDIO_BOOT_LOOP_TEST != 0U)
    s_loop_on        = 1U;
#else
    s_loop_on        = 0U;
#endif

    HAL_UART_Receive_IT(&huart2, &s_rx_byte_it, 1U);
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
    uint32_t now = HAL_GetTick();

    /* 1. 收取应答字节 */
    while (s_rx_tail != s_rx_head)
    {
        uint8_t b = s_rx_ring[s_rx_tail];
        s_rx_tail = (uint16_t)((s_rx_tail + 1U) % AUDIO_RX_RING_SIZE);
        audio_rx_parse(b);
    }

    /* 2. 上电 5s 静默期：期间只收不发 */
    if (!s_ready)
    {
        if ((now - s_power_on_tick) >= AUDIO_POWER_ON_WAIT_MS)
        {
            s_ready        = 1U;
            /* 关键修复（问题1）：5秒上电初始化门闸到期，直接下发切到 SPI-FLASH 盘符，绝不能压入队列尾部！
             * 否则若压入队列，会在欢迎语音(track 1)刚开始播放500ms时弹出执行切盘符，导致欢迎语瞬间被掐断 */
            n930x_switch_flash();
            s_last_tx_tick = now;                      /* 下一条指令将在 500ms 后发出 */
        }
        return;
    }

    /* 3. 停止合并窗口到期：确实没有新的播放请求，才真正下发 STOP */
    if ((s_halt_pending != 0U) && ((now - s_halt_tick) >= AUDIO_HALT_HOLD_MS))
    {
        s_halt_pending = 0U;
        if (audio_req_has(AOP_PLAY) == 0U)
        {
            s_q_count = 0U;
            s_q_idx   = 0U;
            audio_req_push(AOP_STOP, 0U);
        }
    }

    /* 4. 每 500ms 最多发一条待发指令 */
    if ((s_req_cnt > 0U) && ((now - s_last_tx_tick) >= AUDIO_CMD_GAP_MS))
    {
        audio_req_t r;
        if (audio_req_pop(&r))
        {
            switch (r.type)
            {
                case AOP_FLASH:
                    n930x_switch_flash();
                    break;

                case AOP_VOL:
                    n930x_volume((uint8_t)r.arg);
                    break;

                case AOP_PLAY:
                    n930x_play(r.arg);
                    s_last_track    = r.arg;
                    s_seg_active    = 1U;
                    s_seg_busy_seen = 0U;
                    s_seg_tick      = now;
                    break;

                case AOP_STOP:
                    n930x_stop();
                    s_q_count    = 0U;
                    s_q_idx      = 0U;
                    s_seg_active = 0U;
                    break;

                case AOP_PAUSE:
                    n930x_pause();
                    break;

                default:
                    break;
            }
            s_last_tx_tick = now;
        }
    }

    /* 5. 段落播放结束检测：BUSY 先拉低再抬高 -> 本段播完；超时兜底 */
    if (s_seg_active)
    {
        uint8_t busy;

        /* 关键修复（问题3）：仅当实际硬件引脚检测到低电平（0=BUSY播放中）时，才置位 s_seg_busy_seen！
         * 切勿直接调用 BSP_Audio_IsBusy()，因其内部在 s_seg_active!=0 时恒为 1，会导致误判立即结束 */
        if (HAL_GPIO_ReadPin(AUDIO_BUSY_GPIO_Port, AUDIO_BUSY_Pin) == GPIO_PIN_RESET)
        {
            s_seg_busy_seen = 1U;
        }

        /* 有 PLAY 还在队列里没发出去时不作判定，避免重复入队 */
        if (!audio_req_has(AOP_PLAY) && !audio_req_has(AOP_STOP))
        {
            busy = (HAL_GPIO_ReadPin(AUDIO_BUSY_GPIO_Port, AUDIO_BUSY_Pin) == GPIO_PIN_RESET) ? 1U : 0U;
            if (((now - s_seg_tick) > AUDIO_SEG_MIN_MS) &&
                ((s_seg_busy_seen && !busy) ||
                 ((now - s_seg_tick) > AUDIO_SEG_TIMEOUT_MS)))
            {
                s_seg_active = 0U;
                s_q_idx++;
                if (s_q_idx < s_q_count)
                {
                    audio_req_push(AOP_PLAY, s_queue[s_q_idx]);
                }
                else
                {
                    s_q_count = 0U;
                    s_q_idx   = 0U;
                    /* 关键修复（问题4）：播报队列播放完毕后，自动向 N9300 下发停止指令使 DAC 进入低功耗静音模式，
                     * 彻底解决 8002A 功放悬空偏置引发的喇叭持续“突突突”电机船/振荡杂音 */
                    audio_req_push(AOP_STOP, 0U);
                }
            }
        }
    }

    /* 5.5 延期音量更新：语音播放完毕且处于空闲状态时，自动补发之前被推迟的音量设置 */
    if ((s_pending_deferred_hw_vol != 0xFFU) && (s_seg_active == 0U) && !audio_req_has(AOP_PLAY))
    {
        uint8_t busy = (HAL_GPIO_ReadPin(AUDIO_BUSY_GPIO_Port, AUDIO_BUSY_Pin) == GPIO_PIN_RESET) ? 1U : 0U;
        if (!busy)
        {
            audio_req_replace_or_push(AOP_VOL, (uint16_t)s_pending_deferred_hw_vol);
            s_pending_deferred_hw_vol = 0xFFU;
        }
    }

    /* 6. 开机循环播放测试 */
    if ((s_loop_on != 0U) && (s_req_cnt == 0U) && (s_seg_active == 0U))
    {
        if ((int32_t)(now - s_loop_next_tick) >= 0)
        {
            audio_req_push(AOP_PLAY, s_loop_cur);
            s_loop_next_tick = now + s_loop_interval;
            s_loop_cur = (s_loop_cur >= s_loop_last) ? s_loop_first : (uint16_t)(s_loop_cur + 1U);
        }
    }
}

void BSP_Audio_SetVolume(uint8_t ui_vol_0_to_10)
{
    uint8_t hw_vol;

    if (ui_vol_0_to_10 > AUDIO_VOL_UI_MAX) ui_vol_0_to_10 = AUDIO_VOL_UI_MAX;
    hw_vol = (uint8_t)((uint16_t)ui_vol_0_to_10 * AUDIO_VOL_HW_MAX / AUDIO_VOL_UI_MAX);

    /* 关键修复（问题1）：音量未变时绝不下发，严防冗余音量指令掐断正在播放的语音 */
    if (hw_vol == s_current_hw_vol)
    {
        return;
    }
    s_current_hw_vol = hw_vol;

    /* 关键修复（问题1）：若芯片正在播放提示音（如0007生物反馈长语音或呼吸引导语音），
     * 绝不下发串口音量指令，避免 N9300 收到串口指令后重置/截断当前音频！
     * 保存至 s_pending_deferred_hw_vol，在段落播放完毕空闲时自动补发 */
    if (s_seg_active != 0U || audio_req_has(AOP_PLAY) != 0U || BSP_Audio_IsBusy())
    {
        s_pending_deferred_hw_vol = hw_vol;
        return;
    }

    s_pending_deferred_hw_vol = 0xFFU;
    audio_req_replace_or_push(AOP_VOL, hw_vol);   /* 音量只保留最新一条，不占队、不滞后 */
}

static uint32_t s_last_play_req_tick = 0U;
static uint16_t s_last_play_req_track = 0U;

void BSP_Audio_PlayTrack(uint16_t track_id)
{
    uint32_t now = HAL_GetTick();

    /* 关键修复（问题2）：800ms 内对同一曲目的重复调用直接过滤防抖，杜绝双“吸”音 */
    if ((track_id == s_last_play_req_track) && ((now - s_last_play_req_tick) < 800U))
    {
        return;
    }

    /* 1) 新的播放请求要作废“待发的停止”和已排队的停止 */
    s_halt_pending = 0U;
    audio_req_purge(AOP_STOP);

    /* 2) 同一首正在播、且硬件 BUSY 引脚确实拉低处于播放状态 -> 当作重复触发，直接忽略，避免掐断或重头再来 */
    if ((s_seg_active != 0U || audio_req_has(AOP_PLAY) != 0U) && (s_last_track == track_id))
    {
        if (HAL_GPIO_ReadPin(AUDIO_BUSY_GPIO_Port, AUDIO_BUSY_Pin) == GPIO_PIN_RESET || audio_req_has(AOP_PLAY) != 0U)
        {
            return;
        }
    }

    s_last_play_req_track = track_id;
    s_last_play_req_tick  = now;

    /* 3) 单曲：清掉尚未发出的旧曲目，清空多段队列 */
    audio_req_purge(AOP_PLAY);
    s_q_count       = 0U;
    s_q_idx         = 0U;
    s_seg_busy_seen = 0U;

    audio_req_push(AOP_PLAY, track_id);
}

void BSP_Audio_PlayVoice(AudioVoiceId_t id)
{
    BSP_Audio_PlayTrack((uint16_t)id);
}

void BSP_Audio_PlayQueue(const uint16_t *tracks, uint8_t count)
{
    uint8_t i;

    if ((tracks == NULL) || (count == 0U)) return;
    if (count > AUDIO_QUEUE_MAX) count = AUDIO_QUEUE_MAX;

    for (i = 0U; i < count; i++) s_queue[i] = tracks[i];

    s_halt_pending = 0U;
    audio_req_purge(AOP_STOP);
    audio_req_purge(AOP_PLAY);
    s_q_count       = count;
    s_q_idx         = 0U;
    s_seg_busy_seen = 0U;

    audio_req_push(AOP_PLAY, s_queue[0]);
}

void BSP_Audio_PlayBioResult(uint8_t bpm)
{
    uint16_t seq[3];
    if (bpm < 5U)  bpm = 5U;
    if (bpm > 10U) bpm = 10U;

    seq[0] = (uint16_t)AUDIO_VOICE_BIO_PREFIX;               /* 0008 */
    seq[1] = AUDIO_TRACK_FREQ_BASE + (uint16_t)(bpm - 5U);   /* 0014 ~ 0019 */
    seq[2] = (uint16_t)AUDIO_VOICE_CLICK_START_PROMPT;       /* 0009 */

    BSP_Audio_PlayQueue(seq, 3U);
}

void BSP_Audio_PlayTreatEnd(uint8_t timer_min)
{
    uint16_t seq[3];
    if (timer_min < 5U)  timer_min = 5U;
    if (timer_min > 30U) timer_min = 30U;

    seq[0] = (uint16_t)AUDIO_VOICE_TREAT_END_PREFIX;             /* 0010 */
    seq[1] = AUDIO_TRACK_TIME_BASE + (uint16_t)(timer_min - 5U);  /* 0020 ~ 0045 */
    seq[2] = (uint16_t)AUDIO_VOICE_POWER_OFF_PROMPT;              /* 0012 */

    BSP_Audio_PlayQueue(seq, 3U);
}

void BSP_Audio_Stop(void)
{
    audio_req_purge(AOP_PLAY);
    audio_req_purge(AOP_STOP);
    s_q_count       = 0U;
    s_q_idx         = 0U;
    s_seg_active    = 0U;
    s_halt_pending  = 0U;
    s_pending_deferred_hw_vol = 0xFFU;
    /* 关键修复（问题7）：直接下发硬件停止指令并清空队列，确保切页面时残存语音立即终止 */
    n930x_stop();
    s_last_tx_tick  = HAL_GetTick();
}

void BSP_Audio_Pause(void)
{
    audio_req_push(AOP_PAUSE, 0U);
}

uint8_t BSP_Audio_IsBusy(void)
{
    /* 若段落处于播放状态或 FIFO 仍有指令待发，视为 busy */
    if (s_seg_active != 0U || s_req_cnt > 0U)
    {
        return 1U;
    }
    /* BUSY 引脚：播放中为低电平(RESET)，空闲为高电平(SET) */
    return (HAL_GPIO_ReadPin(AUDIO_BUSY_GPIO_Port, AUDIO_BUSY_Pin) == GPIO_PIN_RESET) ? 1U : 0U;
}

uint8_t BSP_Audio_IsHeadphoneInserted(void)
{
    /* 插入耳机时 PJ-342A 触点断开，HP_DET 被 R77(100k) 拉高到 3.3V */
    return (HAL_GPIO_ReadPin(HP_DET_GPIO_Port, HP_DET_Pin) == GPIO_PIN_SET) ? 1U : 0U;
}

void BSP_Audio_GetStatus(AudioStatus_t *out)
{
    if (out == NULL) return;
    out->busy               = (uint8_t)((s_seg_active || (s_req_cnt > 0U) || BSP_Audio_IsBusy()) ? 1U : 0U);
    out->headphone_inserted = BSP_Audio_IsHeadphoneInserted();
    out->last_ack_cmd       = s_last_ack_cmd;
    out->online             = s_online;
}

/* ======================= 循环播放测试接口 ======================= */
void BSP_Audio_LoopTestStart(uint16_t first_track, uint16_t last_track, uint32_t interval_ms)
{
    if (first_track == 0U) first_track = 1U;
    if (last_track < first_track) last_track = first_track;
    if (interval_ms < AUDIO_CMD_GAP_MS) interval_ms = AUDIO_CMD_GAP_MS;

    s_loop_first     = first_track;
    s_loop_last      = last_track;
    s_loop_cur       = first_track;
    s_loop_interval  = interval_ms;
    s_loop_next_tick = HAL_GetTick();
    s_loop_on        = 1U;
}

void BSP_Audio_LoopTestStop(void)
{
    s_loop_on = 0U;
}

uint16_t BSP_Audio_GetCurrentTrack(void)
{
    return s_last_track;
}

/* ======================= 调试接口 ======================= */
uint8_t BSP_Audio_IsReady(void)
{
    return s_ready;
}

uint8_t BSP_Audio_GetLastReply(void)
{
    return s_last_ack_cmd;
}

uint8_t BSP_Audio_GetLastError(void)
{
    return s_last_error;
}
