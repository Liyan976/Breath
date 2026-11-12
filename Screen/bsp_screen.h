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

/**
 * @brief  初始化串口屏驱动（创建发送互斥锁，清空环形缓冲区并开启 USART3 单字节接收中断）
 * @retval 无
 */
void BSP_Screen_Init(void);

/**
 * @brief  USART3 单字节接收完成中断回调（在 HAL_UART_RxCpltCallback 中调用）
 * @note   仅将接收到的 1 字节写入无锁环形缓冲区并重装接收中断
 * @retval 无
 */
void BSP_Screen_OnUartRxCplt(void);

/**
 * @brief  USART3 错误中断恢复处理（在 HAL_UART_ErrorCallback 中调用）
 * @note   清除过载/帧/噪声错误标志并重新使能单字节接收中断，防止串口卡死
 * @retval 无
 */
void BSP_Screen_OnUartError(void);

/**
 * @brief  非阻塞读取并提取一帧完整数据（以连续3个 0xFF 作为帧结束标志）
 * @param  frame: 输出参数，存放提取并解析完成的帧结构体
 * @retval 1: 成功提取一帧完整数据；0: 暂无完整帧
 */
uint8_t BSP_Screen_ReadFrame(ScreenFrame_t *frame);

/**
 * @brief  获取串口接收环形缓冲区溢出累计次数（用于调试排查）
 * @retval 累计溢出字节数
 */
uint32_t BSP_Screen_GetRxOverflowCount(void);

/* ================= 底层串口屏指令发送接口（自动附加 0xFF 0xFF 0xFF） ================= */

/**
 * @brief  向串口屏发送一条原始指令字符串（自动在末尾附加 0xFF 0xFF 0xFF）
 * @param  cmd: 以 '\0' 结尾的 ASCII 指令字符串，例如 "page start"
 * @retval 无
 */
void BSP_Screen_SendRawCmd(const char *cmd);

/**
 * @brief  设置串口屏控件数值属性（格式化为 "obj.attr=val" 并附加帧尾）
 * @param  obj: 控件名称，例如 "lung"、"freq"
 * @param  attr: 属性名，例如 "val"
 * @param  val: 要写入的整型数值
 * @retval 无
 */
void BSP_Screen_SetNum(const char *obj, const char *attr, int32_t val);

/**
 * @brief  设置串口屏控件文本属性（格式化为 "obj.attr=\"txt\"" 并附加帧尾）
 * @param  obj: 控件名称，例如 "t_time"、"toast"
 * @param  attr: 属性名，例如 "txt"
 * @param  txt: 文本内容字符串
 * @retval 无
 */
void BSP_Screen_SetText(const char *obj, const char *attr, const char *txt);

/**
 * @brief  控制串口屏控件显示或隐藏（发送 "vis obj,0/1"）
 * @param  obj: 控件名称，例如 "arr_in"、"w1"
 * @param  visible: 1=显示，0=隐藏
 * @retval 无
 */
void BSP_Screen_SetVisible(const char *obj, uint8_t visible);

/* ================= 高层业务语义界面刷新接口 ================= */

/**
 * @brief  切换串口屏当前显示页面
 * @param  page_name: 目标页面名称，例如 "start"、"running"、"paused"、"end"
 * @retval 无
 */
void BSP_Screen_SetPage(const char *page_name);

/**
 * @brief  刷新肺部填充动画进度（0~100）
 * @param  pct_0_to_100: 填充百分比（吸气过程 0->100，呼气过程 100->0，超出100自动限幅）
 * @retval 无
 */
void BSP_Screen_SetLungFill(uint8_t pct_0_to_100);

/**
 * @brief  切换呼吸引导方向箭头显示状态
 * @param  phase: 0=全部隐藏（空闲/暂停），1=吸气（显示左箭头），2=呼气（显示右箭头）
 * @retval 无
 */
void BSP_Screen_SetBreathArrow(uint8_t phase);

/**
 * @brief  刷新屏幕显示的当前呼吸引导频率（BPM）
 * @param  bpm: 引导呼吸频率（次/分钟）
 * @retval 无
 */
void BSP_Screen_SetGuideBPM(uint8_t bpm);

/**
 * @brief  刷新屏幕显示的剩余治疗倒计时（格式 "MM:SS"）
 * @param  remain_sec: 剩余秒数
 * @retval 无
 */
void BSP_Screen_SetRemainTimeSec(uint16_t remain_sec);

/**
 * @brief  更新指定通道电极片脱落告警图标显示状态
 * @param  ch: 刺激通道号（0=通道1，1=通道2，2=通道3）
 * @param  dropped: 1=显示脱落告警，0=隐藏告警
 * @retval 无
 */
void BSP_Screen_SetElectrodeWarn(uint8_t ch, uint8_t dropped);

/**
 * @brief  刷新屏幕右上角电池电量显示百分比
 * @param  pct: 电量百分比（0~100）
 * @retval 无
 */
void BSP_Screen_SetBattery(uint8_t pct);

/**
 * @brief  同步电刺激频率与三路强度档位到串口屏显示控件
 * @param  freq_hz: 当前电刺激频率（Hz）
 * @param  levels: 三路强度数组（levels[0..2]，每路 0~10 档）
 * @retval 无
 */
void BSP_Screen_SetStimParams(uint8_t freq_hz, const uint8_t levels[3]);

/**
 * @brief  同步系统设置参数（音量、定时时长、蓝牙开关）到串口屏显示控件
 * @param  vol: 音量档位（0~10）
 * @param  timer_min: 定时时长（5~30 分钟）
 * @param  ble_on: 蓝牙状态（0=关闭，1=开启）
 * @retval 无
 */
void BSP_Screen_SetSettingsDisplay(uint8_t vol, uint8_t timer_min, uint8_t ble_on);

/**
 * @brief  跳转至治疗结束结算页面并显示本次有效治疗时长
 * @param  valid_min: 有效治疗分钟数
 * @retval 无
 */
void BSP_Screen_ShowFinished(uint16_t valid_min);

/**
 * @brief  在屏幕提示框中弹出短提示信息
 * @param  msg: 提示文本字符串
 * @retval 无
 */
void BSP_Screen_ShowToast(const char *msg);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_SCREEN_H */
