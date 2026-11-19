/**
 * @file    bsp_screen.h
 * @brief   淘晶驰（TJC）串口屏驱动及通信协议头文件
 * @note    硬件连接：USART3（PB10/TX -> 屏RX，PB11/RX -> 屏TX，波特率 115200，8N1）
 */
#ifndef __BSP_SCREEN_H
#define __BSP_SCREEN_H

#include "main.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * 淘晶驰串口屏通信协议说明（USART3: PB10/TX, PB11/RX, 115200 8N1）
 *
 * 1. 主控 MCU -> 串口屏：ASCII 文本指令 + 3字节固定结束符 (0xFF 0xFF 0xFF)
 *    示例："lung.val=65\xFF\xFF\xFF"、"page running\xFF\xFF\xFF"
 *
 * 2. 串口屏 -> 主控 MCU：二进制数据帧 + 3字节固定结束符 (0xFF 0xFF 0xFF)
 *    - 自定义控制帧（兼容 FPM 模板协议）：
 *      [0x05] [page] [control] [value] [可选扩展字节...] [0xFF 0xFF 0xFF]
 *    - 淘晶驰原生触摸事件帧：
 *      [0x65] [page] [comp_id] [event] [0xFF 0xFF 0xFF]
 *    - 淘晶驰原生数值返回帧：
 *      [0x71] [b0] [b1] [b2] [b3] [0xFF 0xFF 0xFF]
 * ========================================================================== */

/** @defgroup 串口屏帧头定义 */
#define SCREEN_FRAME_HDR_CTRL   0x05U   /**< 自定义控制帧帧头：[0x05][page][control][value] */
#define SCREEN_FRAME_HDR_CUSTOM 0xA5U   /**< 紧凑自定义帧帧头：[0xA5][page][action/cmd][params...][0x5A] */
#define SCREEN_FRAME_TAIL_CUSTOM 0x5AU  /**< 紧凑自定义帧帧尾 */
#define SCREEN_FRAME_HDR_TOUCH  0x65U   /**< 原生触摸事件帧头：[0x65][page][comp_id][event] */
#define SCREEN_FRAME_HDR_PAGE   0x66U   /**< 原生当前页码返回帧头：[0x66][page] */
#define SCREEN_FRAME_HDR_STR    0x70U   /**< 原生字符串返回帧头：[0x70][ASCII...] */
#define SCREEN_FRAME_HDR_NUM    0x71U   /**< 原生32位整数返回帧头：[0x71][b0..b3] */

#define SCREEN_FRAME_MAX_DATA   48U     /**< 单帧有效载荷最大字节数（不含3字节0xFF帧尾） */

/** @defgroup 倒计时显示格式配置 */
#define SCREEN_TIMER_FORMAT_MMSS 0      /**< 1: 秒级精准倒计时 "09:59"; 0: 传统整分钟 "10分钟"（需求规定格式） */

/** @defgroup 串口屏启停控制按键图标ID（严格对应 NAFLD.HMI 工程各治疗运行页面 b1.pic 图标） */
#define SCREEN_PIC_BTN_RUNNING   10U   /**< 运行中：显示“暂停”图标 (|| 双竖杠，点击可暂停) */
#define SCREEN_PIC_BTN_STOPPED   11U   /**< 停止/暂停中：显示“开始/播放”图标 (▶ 三角，点击可开始) */

/**
 * @brief 串口屏页面编号定义（严格对应上位机 15 个完整界面：Page 0 ~ Page 14）
 */
typedef enum {
    /* 1. 模式选择与主菜单页面 */
    SCREEN_PAGE_START         = 0x00U, /**< page 0:  start (主界面/欢迎页) */
    SCREEN_PAGE_ELECMODE      = 0x01U, /**< page 1:  elecmode (电刺激模式选择：连续/断续/疏密) */
    SCREEN_PAGE_BREATHMODE    = 0x02U, /**< page 2:  breathmode (呼吸引导模式选择：简易/生物反馈) */
    SCREEN_PAGE_LIANHEMODE1   = 0x03U, /**< page 3:  lianhemode1 (联合模式第1步：选呼吸模式) */
    SCREEN_PAGE_LIANHEMODE2   = 0x0AU, /**< page 10: lianhemode2 (联合模式第2步：选电刺激波形) */

    /* 2. 5 个测量/运行页面（带开始/暂停 b1 与返回 b2） */
    SCREEN_PAGE_JIANYI        = 0x04U, /**< page 4:  jianyi (单纯简易呼吸测量运行) */
    SCREEN_PAGE_JIANYIELEC    = 0x06U, /**< page 6:  jianyielec (简易呼吸 + 电刺激联合测量运行) */
    SCREEN_PAGE_FANKUI        = 0x07U, /**< page 7:  fankui (单纯生物反馈呼吸测量运行) */
    SCREEN_PAGE_FANKUIELEC    = 0x09U, /**< page 9:  fankuielec (生物反馈 + 电刺激联合测量运行) */
    SCREEN_PAGE_ELEC          = 0x0BU, /**< page 11: elec (单独电刺激治疗测量运行) */

    /* 3. 5 个参数设置页面 */
    SCREEN_PAGE_JIANYISET     = 0x05U, /**< page 5:  jianyiset (简易呼吸参数设置) */
    SCREEN_PAGE_FANKUISET     = 0x08U, /**< page 8:  fankuiset (生物反馈呼吸参数设置) */
    SCREEN_PAGE_ELECSET       = 0x0CU, /**< page 12: elecset (单纯电刺激参数设置) */
    SCREEN_PAGE_LIANHESET1    = 0x0DU, /**< page 13: lianheset1 (联合简易参数设置) */
    SCREEN_PAGE_LIANHESET2    = 0x0EU  /**< page 14: lianheset2 (联合反馈参数设置) */
} ScreenPageId_t;

/**
 * @brief 解析完成的一帧串口屏上行数据结构体
 */
typedef struct {
    uint8_t header;                         /**< 帧头字节（如 0x05、0x65） */
    uint8_t page;                           /**< 页面编号（raw[1]） */
    uint8_t control;                        /**< 控件/功能编号（raw[2]） */
    uint8_t value;                          /**< 控件值或动作码（raw[3]） */
    uint8_t raw[SCREEN_FRAME_MAX_DATA];     /**< 完整有效载荷原始字节（已剥离3字节0xFF帧尾） */
    uint8_t len;                            /**< raw 中有效字节长度 */
} ScreenFrame_t;

/* ================= 驱动与数据流处理接口 ================= */
void     BSP_Screen_Init(void);
void     BSP_Screen_OnUartRxCplt(void);
void     BSP_Screen_OnUartError(void);
uint8_t  BSP_Screen_ReadFrame(ScreenFrame_t *frame);
uint32_t BSP_Screen_GetRxOverflowCount(void);

/* ================= 底层串口屏指令发送接口 ================= */
void     BSP_Screen_SendRawCmd(const char *cmd);
void     BSP_Screen_SendRawBytes(const uint8_t *data, uint16_t len);
void     BSP_Screen_Printf(const char *format, ...);
void     BSP_Screen_SetNum(const char *obj, const char *attr, int32_t val);
void     BSP_Screen_SetText(const char *obj, const char *attr, const char *txt);
void     BSP_Screen_SetVisible(const char *obj, uint8_t visible);

/* ================= 业务语义界面刷新接口 ================= */
uint8_t  BSP_Screen_IsTherapyPage(uint8_t page);
void     BSP_Screen_SetControlBtnPic(uint8_t is_running);
void     BSP_Screen_SetCurrentPage(uint8_t page_id);
uint8_t  BSP_Screen_GetCurrentPage(void);
void     BSP_Screen_SetPage(const char *page_name);
void     BSP_Screen_SetLungFill(uint8_t pct_0_to_100);
void     BSP_Screen_SetBreathArrow(uint8_t phase);
void     BSP_Screen_SetGuideBPM(uint8_t bpm);
void     BSP_Screen_SetRemainTimeSec(uint16_t remain_sec);
void     BSP_Screen_SetBleStatus(uint8_t connected);
void     BSP_Screen_SetElectrodeWarn(uint8_t ch, uint8_t dropped);
void     BSP_Screen_SetBattery(uint8_t pct);
void     BSP_Screen_SetStimParams(uint8_t freq_hz, const uint8_t levels[3]);
void     BSP_Screen_SetSettingsDisplay(uint8_t vol, uint8_t timer_min, uint8_t ble_on);
void     BSP_Screen_ShowFinished(uint16_t valid_min);
void     BSP_Screen_ShowToast(const char *msg);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_SCREEN_H */
