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
#define SCREEN_FRAME_HDR_CTRL   0x05U   /**< 自定义三级控制帧帧头：[0x05][page][control][value] */
#define SCREEN_FRAME_HDR_TOUCH  0x65U   /**< 原生触摸事件帧头：[0x65][page][comp_id][event] */
#define SCREEN_FRAME_HDR_PAGE   0x66U   /**< 原生当前页码返回帧头：[0x66][page] */
#define SCREEN_FRAME_HDR_STR    0x70U   /**< 原生字符串返回帧头：[0x70][ASCII...] */
#define SCREEN_FRAME_HDR_NUM    0x71U   /**< 原生32位整数返回帧头：[0x71][b0..b3] */

#define SCREEN_FRAME_MAX_DATA   48U     /**< 单帧有效载荷最大字节数（不含3字节0xFF帧尾） */

/**
 * @brief 串口屏页面编号枚举（对应 0x05 [page] [control] [value] 协议中的 page 字段）
 */
typedef enum {
    SCREEN_PAGE_START       = 0x00U, /**< 主菜单页：选择呼吸训练 / 单纯电刺激 / 联合模式 */
    SCREEN_PAGE_BREATH_SEL  = 0x01U, /**< 呼吸子模式选择页：简易治疗 / 生物反馈 */
    SCREEN_PAGE_STIM_SEL    = 0x02U, /**< 电刺激波形选择页：连续波 / 断续波 / 疏密波 */
    SCREEN_PAGE_TREAT_RUN   = 0x03U, /**< 治疗就绪/运行/暂停页：启停、参数显示与强度调节 */
    SCREEN_PAGE_SETTINGS    = 0x04U, /**< 系统设置页：音量、定时时长、蓝牙开关 */
    SCREEN_PAGE_END         = 0x05U  /**< 治疗结束结算页：显示本次有效治疗时长 */
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
void     BSP_Screen_Printf(const char *format, ...);
void     BSP_Screen_SetNum(const char *obj, const char *attr, int32_t val);
void     BSP_Screen_SetText(const char *obj, const char *attr, const char *txt);
void     BSP_Screen_SetVisible(const char *obj, uint8_t visible);

/* ================= 业务语义界面刷新接口 ================= */
void     BSP_Screen_SetPage(const char *page_name);
void     BSP_Screen_SetLungFill(uint8_t pct_0_to_100);
void     BSP_Screen_SetBreathArrow(uint8_t phase);
void     BSP_Screen_SetGuideBPM(uint8_t bpm);
void     BSP_Screen_SetRemainTimeSec(uint16_t remain_sec);
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
