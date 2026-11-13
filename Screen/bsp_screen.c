/**
 * @file    bsp_screen.c
 * @brief   淘晶驰（TJC）串口屏底层驱动实现
 * @note    采用“中断单字节写 SPSC 环形缓冲区 + 任务层按 0xFF 0xFF 0xFF 提取完整帧”设计，
 *          并使用 FreeRTOS 互斥锁保证多任务并发下发串口指令时不发生字节穿插。
 */
#include "bsp_screen.h"
#include "usart.h"
#include "cmsis_os.h"
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

#define SCREEN_RX_RING_SIZE     256U    /**< 接收环形缓冲区容量（字节） */

/* SPSC（单生产者-单消费者）无锁环形缓冲区：
 * - 生产者：USART3 接收中断仅修改 s_rx_head
 * - 消费者：ScreenTask 任务仅修改 s_rx_tail
 * 因双方只各自更新自己的游标，无需关全局中断即可安全读写
 */
static uint8_t           s_rx_ring[SCREEN_RX_RING_SIZE];
static volatile uint16_t s_rx_head = 0U;            /**< 写游标（ISR 更新） */
static volatile uint16_t s_rx_tail = 0U;            /**< 读游标（Task 更新） */
static volatile uint32_t s_rx_overflow_cnt = 0U;    /**< 缓冲区满导致的丢字节计数 */
static uint8_t           s_rx_byte_it = 0U;         /**< HAL 单字节中断接收缓存 */

/* 任务层组帧缓冲区（仅在 ScreenTask 中访问） */
static uint8_t  s_frame_buf[SCREEN_FRAME_MAX_DATA];
static uint8_t  s_frame_len = 0U;                   /**< 当前已累计的字节长度 */
static uint8_t  s_ff_cnt    = 0U;                   /**< 连续收到的 0xFF 计数 */

static osMutexId s_tx_mutex = NULL;                 /**< 串口发送互斥锁（防止多任务指令混杂） */

/**
 * @brief  底层串口字节流发送函数（自动加锁保护多任务并发访问）
 * @param  data: 待发送数据缓冲区指针
 * @param  len: 发送字节数
 * @retval 无
 */
static void screen_uart_send_bytes(const uint8_t *data, uint16_t len)
{
    uint8_t locked = 0U;

    /* 仅在调度器已运行且不在中断上下文中时申请互斥锁 */
    if (s_tx_mutex != NULL && xTaskGetSchedulerState() == taskSCHEDULER_RUNNING && __get_IPSR() == 0U)
    {
        if (osMutexWait(s_tx_mutex, 50U) == osOK)
        {
            locked = 1U;
        }
    }

    HAL_UART_Transmit(&huart3, (uint8_t *)data, len, 50U);

    if (locked)
    {
        osMutexRelease(s_tx_mutex);
    }
}

/**
 * @brief  发送淘晶驰串口屏标准 3 字节结束符（0xFF 0xFF 0xFF）
 * @retval 无
 */
static void screen_send_end(void)
{
    static const uint8_t end[3] = {0xFFU, 0xFFU, 0xFFU};
    screen_uart_send_bytes(end, 3U);
}

/**
 * @brief  初始化串口屏驱动（创建发送互斥锁，清空缓冲区并启动 USART3 单字节接收中断）
 * @retval 无
 */
void BSP_Screen_Init(void)
{
    if (s_tx_mutex == NULL)
    {
        osMutexDef(ScreenTxMtx);
        s_tx_mutex = osMutexCreate(osMutex(ScreenTxMtx));
    }

    s_rx_head = 0U;
    s_rx_tail = 0U;
    s_rx_overflow_cnt = 0U;
    s_frame_len = 0U;
    s_ff_cnt = 0U;

    /* 清除上电初期可能残留的过载错误标志，并使能首次单字节中断接收 */
    __HAL_UART_CLEAR_OREFLAG(&huart3);
    HAL_UART_Receive_IT(&huart3, &s_rx_byte_it, 1U);
}

/**
 * @brief  USART3 接收完成中断处理函数（在 HAL_UART_RxCpltCallback 中调用）
 * @note   中断内仅执行“入队 1 字节 + 立即重装接收中断”，确保中断响应时间极短
 * @retval 无
 */
void BSP_Screen_OnUartRxCplt(void)
{
    uint16_t next = (uint16_t)((s_rx_head + 1U) % SCREEN_RX_RING_SIZE);
    if (next != s_rx_tail)
    {
        s_rx_ring[s_rx_head] = s_rx_byte_it;
        s_rx_head = next;
    }
    else
    {
        /* 环形缓冲区已满：记录溢出次数，丢弃当前字节以保护未读取数据 */
        s_rx_overflow_cnt++;
    }

    /* 必须立即重新开启下一次单字节中断接收 */
    HAL_UART_Receive_IT(&huart3, &s_rx_byte_it, 1U);
}

/**
 * @brief  USART3 异常错误中断恢复函数（在 HAL_UART_ErrorCallback 中调用）
 * @note   热插拔屏幕或总线噪声可能触发 ORE（过载错误）并导致 HAL 停止接收，
 *         此处清除错误标志后重新启动单字节接收中断
 * @retval 无
 */
void BSP_Screen_OnUartError(void)
{
    __HAL_UART_CLEAR_OREFLAG(&huart3);
    __HAL_UART_CLEAR_FEFLAG(&huart3);
    __HAL_UART_CLEAR_NEFLAG(&huart3);
    HAL_UART_Receive_IT(&huart3, &s_rx_byte_it, 1U);
}

/**
 * @brief  从环形缓冲区非阻塞提取一帧完整串口屏报文（以连续 0xFF 0xFF 0xFF 结尾）
 * @param  frame: 输出参数，存放剥离帧尾后的完整帧内容及关键字段
 * @retval 1: 成功读出完整一帧；0: 缓冲区暂无完整帧
 */
uint8_t BSP_Screen_ReadFrame(ScreenFrame_t *frame)
{
    if (frame == NULL) return 0U;

    while (s_rx_tail != s_rx_head)
    {
        uint8_t b = s_rx_ring[s_rx_tail];
        s_rx_tail = (uint16_t)((s_rx_tail + 1U) % SCREEN_RX_RING_SIZE);

        /* 先存入临时组帧缓存，若超出最大帧长则丢弃残帧重新同步 */
        if (s_frame_len < SCREEN_FRAME_MAX_DATA)
        {
            s_frame_buf[s_frame_len++] = b;
        }
        else
        {
            s_frame_len = 0U;
            s_ff_cnt = 0U;
            continue;
        }

        /* 状态机检测连续 3 个 0xFF 帧结束符 */
        if (b == 0xFFU)
        {
            s_ff_cnt++;
            if (s_ff_cnt >= 3U)
            {
                if (s_frame_len > 3U)
                {
                    /* 剥离末尾 3 字节 0xFF，输出完整有效载荷 */
                    uint8_t payload_len = (uint8_t)(s_frame_len - 3U);
                    memset(frame, 0, sizeof(*frame));
                    memcpy(frame->raw, s_frame_buf, payload_len);
                    frame->len     = payload_len;
                    frame->header  = frame->raw[0];
                    frame->page    = (payload_len > 1U) ? frame->raw[1] : 0U;
                    frame->control = (payload_len > 2U) ? frame->raw[2] : 0U;
                    frame->value   = (payload_len > 3U) ? frame->raw[3] : 0U;

                    s_frame_len = 0U;
                    s_ff_cnt = 0U;
                    return 1U;
                }
                /* 忽略串口屏上电空帧（如 0x00 FF FF FF 或单独的 FF FF FF） */
                s_frame_len = 0U;
                s_ff_cnt = 0U;
            }
        }
        else
        {
            s_ff_cnt = 0U;
        }
    }

    return 0U;
}

/**
 * @brief  获取接收环形缓冲区溢出次数
 * @retval 溢出次数统计值
 */
uint32_t BSP_Screen_GetRxOverflowCount(void)
{
    return s_rx_overflow_cnt;
}

/**
 * @brief  发送原始串口屏指令并自动附加结束符 0xFF 0xFF 0xFF
 * @param  cmd: ASCII 指令字符串，如 "page running"
 * @retval 无
 */
void BSP_Screen_SendRawCmd(const char *cmd)
{
    if (cmd == NULL) return;
    screen_uart_send_bytes((const uint8_t *)cmd, (uint16_t)strlen(cmd));
    screen_send_end();
}

/**
 * @brief  类似 printf 的变参格式化发送接口（自动格式化并附加 0xFF 0xFF 0xFF 结束符，受互斥锁保护）
 * @param  format: 格式化字符串，如 "page %s"、"result.t1.txt=\"%.4f\""
 * @param  ...: 可变参数列表
 * @retval 无
 */
void BSP_Screen_Printf(const char *format, ...)
{
    char buf[128];
    va_list args;
    if (format == NULL) return;

    va_start(args, format);
    vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);

    BSP_Screen_SendRawCmd(buf);
}

/**
 * @brief  发送控件数值赋值指令（如 "lung.val=50\xFF\xFF\xFF"）
 * @param  obj: 控件名称
 * @param  attr: 属性名（通常为 "val"）
 * @param  val: 整型数值
 * @retval 无
 */
void BSP_Screen_SetNum(const char *obj, const char *attr, int32_t val)
{
    char buf[48];
    snprintf(buf, sizeof(buf), "%s.%s=%ld", obj, attr, (long)val);
    BSP_Screen_SendRawCmd(buf);
}

/**
 * @brief  发送控件文本赋值指令（如 "t_time.txt=\"09:30\"\xFF\xFF\xFF"）
 * @param  obj: 控件名称
 * @param  attr: 属性名（通常为 "txt"）
 * @param  txt: 文本字符串
 * @retval 无
 */
void BSP_Screen_SetText(const char *obj, const char *attr, const char *txt)
{
    char buf[96];
    snprintf(buf, sizeof(buf), "%s.%s=\"%s\"", obj, attr, (txt != NULL) ? txt : "");
    BSP_Screen_SendRawCmd(buf);
}

/**
 * @brief  发送控件显隐控制指令（如 "vis w1,1\xFF\xFF\xFF"）
 * @param  obj: 控件名称
 * @param  visible: 1=显示，0=隐藏
 * @retval 无
 */
void BSP_Screen_SetVisible(const char *obj, uint8_t visible)
{
    char buf[32];
    snprintf(buf, sizeof(buf), "vis %s,%u", obj, visible ? 1U : 0U);
    BSP_Screen_SendRawCmd(buf);
}

/**
 * @brief  切换串口屏页面
 * @param  page_name: 页面名称字符串
 * @retval 无
 */
void BSP_Screen_SetPage(const char *page_name)
{
    char buf[32];
    snprintf(buf, sizeof(buf), "page %s", page_name);
    BSP_Screen_SendRawCmd(buf);
}

/**
 * @brief  更新肺部动画进度条控件（lung.val = 0~100）
 * @param  pct_0_to_100: 填充百分比（0=排空，100=充盈）
 * @retval 无
 */
void BSP_Screen_SetLungFill(uint8_t pct_0_to_100)
{
    if (pct_0_to_100 > 100U) pct_0_to_100 = 100U;
    BSP_Screen_SetNum("lung", "val", (int32_t)pct_0_to_100);
}

/**
 * @brief  控制呼吸箭头显隐（吸气显示 arr_in，呼气显示 arr_ex，其余全隐）
 * @param  phase: 0=隐藏，1=吸气，2=呼气
 * @retval 无
 */
void BSP_Screen_SetBreathArrow(uint8_t phase)
{
    if (phase == 1U)
    {
        BSP_Screen_SetVisible("arr_in", 1U);
        BSP_Screen_SetVisible("arr_ex", 0U);
    }
    else if (phase == 2U)
    {
        BSP_Screen_SetVisible("arr_in", 0U);
        BSP_Screen_SetVisible("arr_ex", 1U);
    }
    else
    {
        BSP_Screen_SetVisible("arr_in", 0U);
        BSP_Screen_SetVisible("arr_ex", 0U);
    }
}

/**
 * @brief  更新屏幕呼吸引导频率显示（gbpm.val）
 * @param  bpm: 当前引导频率（次/分钟）
 * @retval 无
 */
void BSP_Screen_SetGuideBPM(uint8_t bpm)
{
    BSP_Screen_SetNum("gbpm", "val", (int32_t)bpm);
}

/**
 * @brief  更新屏幕剩余治疗倒计时显示（t_time.txt = "MM:SS"）
 * @param  remain_sec: 剩余时间（秒）
 * @retval 无
 */
void BSP_Screen_SetRemainTimeSec(uint16_t remain_sec)
{
    char buf[16];
    uint16_t min = remain_sec / 60U;
    uint16_t sec = remain_sec % 60U;
    snprintf(buf, sizeof(buf), "%02u:%02u", min, sec);
    BSP_Screen_SetText("t_time", "txt", buf);
}

/**
 * @brief  更新电极脱落告警图标显隐（w1 / w2 / w3）
 * @param  ch: 通道索引（0~2）
 * @param  dropped: 1=电极脱落告警显示，0=恢复正常隐藏
 * @retval 无
 */
void BSP_Screen_SetElectrodeWarn(uint8_t ch, uint8_t dropped)
{
    char obj[8];
    if (ch > 2U) return;
    snprintf(obj, sizeof(obj), "w%u", (unsigned)(ch + 1U));
    BSP_Screen_SetVisible(obj, dropped ? 1U : 0U);
}

/**
 * @brief  更新右上角电池电量显示（batt.val = 0~100）
 * @param  pct: 电量百分比
 * @retval 无
 */
void BSP_Screen_SetBattery(uint8_t pct)
{
    if (pct > 100U) pct = 100U;
    BSP_Screen_SetNum("batt", "val", (int32_t)pct);
}

/**
 * @brief  同步电刺激频率及三路通道强度到屏幕控件（freq、ch1、ch2、ch3）
 * @param  freq_hz: 刺激频率（Hz）
 * @param  levels: 三路强度档位数组（0~10）
 * @retval 无
 */
void BSP_Screen_SetStimParams(uint8_t freq_hz, const uint8_t levels[3])
{
    BSP_Screen_SetNum("freq", "val", (int32_t)freq_hz);
    BSP_Screen_SetNum("ch1",  "val", (int32_t)levels[0]);
    BSP_Screen_SetNum("ch2",  "val", (int32_t)levels[1]);
    BSP_Screen_SetNum("ch3",  "val", (int32_t)levels[2]);
}

/**
 * @brief  同步设置页参数到屏幕控件（vol、timer、ble）
 * @param  vol: 音量（0~10）
 * @param  timer_min: 定时（5~30 min）
 * @param  ble_on: 蓝牙开关（0/1）
 * @retval 无
 */
void BSP_Screen_SetSettingsDisplay(uint8_t vol, uint8_t timer_min, uint8_t ble_on)
{
    BSP_Screen_SetNum("vol",   "val", (int32_t)vol);
    BSP_Screen_SetNum("timer", "val", (int32_t)timer_min);
    BSP_Screen_SetNum("ble",   "val", ble_on ? 1 : 0);
}

/**
 * @brief  切换至结算页面并显示本次有效治疗时长
 * @param  valid_min: 有效治疗分钟数
 * @retval 无
 */
void BSP_Screen_ShowFinished(uint16_t valid_min)
{
    char buf[64];
    snprintf(buf, sizeof(buf), "Treatment finished: %u min", valid_min);
    BSP_Screen_SetText("end", "txt", buf);
    BSP_Screen_SetPage("end");
}

/**
 * @brief  向屏幕 toast 控件发送提示文本
 * @param  msg: 提示内容字符串
 * @retval 无
 */
void BSP_Screen_ShowToast(const char *msg)
{
    BSP_Screen_SetText("toast", "txt", msg);
}
