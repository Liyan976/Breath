# 蓝牙 APP 对接与主控协议开发指南

本文档面向后续开发蓝牙手机 APP（iOS / Android / 微信小程序）或扩展 STM32 主控端蓝牙通信功能的工程师，系统阐述了脂肪肝调控仪的蓝牙硬件架构、透传协议帧格式、下位机遥测数据上报修改方法以及 APP 指令下发解析实现。

---

## 一、 硬件架构与蓝牙模块工作模式

### 1.1 模块型号与硬件引脚
本设备板载 **亿佰特 E104-BT5005A**（基于 BLE 5.0 协议栈的低功耗串口透传模块）。主控单片机（STM32F405RGT6）通过 USART1 与其连接。

| 模块引脚 | STM32 引脚 | 引脚类型 | 默认电平/状态 | 功能定义 |
|---|---|---|---|---|
| **TXD** | `PA10 (USART1_RX)` | 串口输入 | 115200 8N1 | 接收来自 BLE 模块透传的 APP 下发数据 |
| **RXD** | `PA9  (USART1_TX)` | 串口输出 | 115200 8N1 | 向 BLE 模块发送待透传给 APP 的状态数据 |
| **LINK** | `PA6  (BLE_LINK)`  | GPIO 输入 | 低电平有效 | 蓝牙连接状态指示（APP 连接成功时拉低） |
| **INT** | `PB2  (BLE_INT)`   | EXTI2 中断 | 双边沿触发 | 数据到达/就绪中断输出 |
| **RST** | `PB7  (BLE_RST)`   | GPIO 输出 | 高电平正常 | 蓝牙硬件复位引脚（低脉冲复位） |
| **WKP** | `PB3  (BLE_WKP)`   | GPIO 输出 | 低电平唤醒 | 模块低功耗唤醒/休眠控制引脚 |
| **MOD** | `PB4  (BLE_MOD)`   | GPIO 输出 | 高电平固定 | 模式选择（拉高为串口透传模式） |

### 1.2 BLE GATT 服务与特性 UUID
E104-BT5005A 默认出厂服务结构如下（可在 BLE 调试助手如 nRF Connect 中查看到）：
- **Primary Service UUID**: `0000fff0-0000-1000-8000-00805f9b34fb`
  - **Write Characteristic**: `0000fff2-0000-1000-8000-00805f9b34fb`（APP 发送数据到单片机，支持 Write / Write Without Response）
  - **Notify Characteristic**: `0000fff1-0000-1000-8000-00805f9b34fb`（单片机上报数据到 APP，APP 需开启此特性的 Notify 监听）

### 1.3 屏幕蓝牙开关与 APP 控制权限逻辑
- **蓝牙广播持续开启**：无论屏幕上蓝牙设置为“开启”还是“关闭”，硬件蓝牙模块始终正常供电并广播，手机 APP 均可搜索并连接到设备。
- **软件控制门控**：
  - 当屏幕上蓝牙设置为 **开启 (`g_app.ble_enabled = 1`)** 时：主控开启周期性遥测上报，并正常执行 APP 下发的启动、暂停、参数设置指令。
  - 当屏幕上蓝牙设置为 **关闭 (`g_app.ble_enabled = 0`)** 时：主控停止主动遥测上报，且在 `App_Therapy_OnBleCommand()` 中拒绝执行控制指令，保障现场设备控制的安全性。

---

## 二、 通信帧结构与校验规范

所有单片机与 APP 之间的交互采用二进制定长/变长协议帧封装，具备帧头、长度、命令字、数据载荷、CRC-8 校验和帧尾。

### 2.1 基础帧格式
```
+-------+-------+-----+--------+-------------------+------+-----+
| SOF0  | SOF1  | LEN | CMD_ID |      Payload      | CRC8 | EOF |
| 1 Byte| 1 Byte|1Byte| 1 Byte | (LEN - 1) Bytes   |1 Byte|1Byte|
+-------+-------+-----+--------+-------------------+------+-----+
|  0xAA |  0x55 |  N  |  0x??  | [D0, D1, ..., Dn] | 0x?? | 0x0D|
+-------+-------+-----+--------+-------------------+------+-----+
```
- **SOF0, SOF1**：起始引导码，固定为 `0xAA 0x55`。
- **LEN**：长度字段，表示 `[CMD_ID] + [Payload...]` 的总字节数（$LEN = 1 + Payload\_Length$）。
- **CMD_ID**：命令功能码（见 2.3 节）。
- **Payload**：指令具体数据，若无参数则为空。
- **CRC8**：校验和，计算范围为从 `LEN` 开始至 `Payload` 最后一个字节。
  - 多项式：$X^8 + X^2 + X + 1$（多项式简记为 `0x07`，初始值 `0x00`）。
- **EOF**：结束符，固定为 `0x0D`（ASCII 回车符 `\r`）。

### 2.2 CRC8 校验算法（C语言实现）
```c
uint8_t ble_crc8(const uint8_t *data, uint8_t len)
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
```

### 2.3 命令字定义
| CMD_ID | 宏名称 | 方向 | 作用与含义 |
|---|---|---|---|
| `0x01` | `BLE_CMD_TELEMETRY` | 下位机 $\to$ APP | 周期性上报运行状态与全部实时参数 |
| `0x10` | `BLE_CMD_SET_PARAMS` | APP $\to$ 下位机 | APP 下发设置参数（频率/档位/时长/音量） |
| `0x11` | `BLE_CMD_CTRL_STATE` | APP $\to$ 下位机 | APP 控制治疗启动、暂停、停止 |
| `0x12` | `BLE_CMD_QUERY_STATUS`| APP $\to$ 下位机 | APP 主动请求单片机立即应答一帧遥测 |

---

## 三、 主控数据上报开发（下位机 $\to$ APP）

### 3.1 上报调度与执行位置
遥测数据由 FreeRTOS 任务 `Bluetooth/ble_task.c` 中的 `BleTask` 驱动，默认每 **500 ms** 触发一次：
```c
/* 文件路径：Bluetooth/ble_task.c */
void BleTask(void const *argument)
{
    // ...
    for (;;)
    {
        BSP_BLE_Poll();

        /* 每 500 ms 发送一次实时遥测数据 */
        if ((tick_cnt % 10U) == 0U && g_app.ble_enabled)
        {
            BleTelemetry_t t;
            // 组装结构体数据...
            BSP_BLE_SendTelemetry(&t);
        }

        tick_cnt++;
        osDelay(50U);
    }
}
```

### 3.2 遥测数据包格式 (`0x01`)
数据包载荷共 17 字节，各字节含义如下：

| 字节偏移 | 变量名 | 数据类型 | 取值范围 | 语义说明 |
|---|---|---|---|---|
| Byte 0 | `breath_mode` | uint8_t | 0~2 | 0: 关闭, 1: 简易模式, 2: 生物反馈模式 |
| Byte 1 | `stim_mode` | uint8_t | 0~3 | 0: 关闭, 1: 连续波, 2: 断续波, 3: 疏密波 |
| Byte 2 | `stim_freq_hz` | uint8_t | 1~100 | 电刺激工作频率 (Hz) |
| Byte 3 | `stim_level[0]`| uint8_t | 0~10 | 通道 1 输出强度档位 |
| Byte 4 | `stim_level[1]`| uint8_t | 0~10 | 通道 2 输出强度档位 |
| Byte 5 | `stim_level[2]`| uint8_t | 0~10 | 通道 3 输出强度档位 |
| Byte 6 | `timer_min` | uint8_t | 5~30 | 本次治疗设定总时长 (分钟) |
| Byte 7~8 | `elapsed_sec` | uint16_t (大端) | 0~1800 | 已治疗有效时长 (秒)，Byte 7 高字节 |
| Byte 9~10 | `remain_sec` | uint16_t (大端) | 0~1800 | 剩余治疗时长 (秒)，Byte 9 高字节 |
| Byte 11 | `guide_bpm` | uint8_t | 5~10 | 当前呼吸引导节奏 (次/分钟) |
| Byte 12 | `real_cbpm` | uint8_t | 0~50 | 传感器实时检测到的实际呼吸率 (次/分) |
| Byte 13 | `elec_drop_mask`| uint8_t | Bit位掩码 | bit0=CH1, bit1=CH2, bit2=CH3 (1代表脱落) |
| Byte 14 | `running` | uint8_t | 0 / 1 | 0: 未在治疗中, 1: 治疗正在运行 |
| Byte 15 | `paused` | uint8_t | 0 / 1 | 0: 正常运行, 1: 当前处于暂停状态 |

### 3.3 修改发送数值的方法
1. **若需修改已有字段的取值逻辑**：
   打开 [Bluetooth/ble_task.c](file:///c:/Users/Administrator/Desktop/淘晶驰git/Breath/Bluetooth/ble_task.c)，找到 `t.xxx = ...` 处修改即可。例如，若希望 APP 端显示的倒计时也采用分钟而不是秒数，可在此处修改 `t.remain_sec` 的装配公式。
2. **若需增加新的上报字段（如电池电量、Boost电压）**：
   - 第一步：在 [Bluetooth/bsp_ble.h](file:///c:/Users/Administrator/Desktop/淘晶驰git/Breath/Bluetooth/bsp_ble.h) 中的 `BleTelemetry_t` 结构体尾部新增变量（如 `uint8_t battery_pct;`）。
   - 第二步：在 [Bluetooth/bsp_ble.c](file:///c:/Users/Administrator/Desktop/淘晶驰git/Breath/Bluetooth/bsp_ble.c) 的 `BSP_BLE_SendTelemetry()` 函数中，将新变量追加写入 `frame[idx++]`，并更新 `LEN` 字段。
   - 第三步：在 `ble_task.c` 中对 `t.battery_pct = BSP_Power_GetBatteryPercent();` 进行赋值。

---

## 四、 APP 下发控制与协议解析实现（APP $\to$ 下位机）

当 APP 向单片机发送控制帧时，单片机在 `USART1_IRQHandler` 中接收字符并写入环形缓冲区；`BleTask` 中的 `BSP_BLE_Poll()` 会提取完整帧、完成 CRC 校验，并调用回调函数：
**`App_Therapy_OnBleCommand(const BleRxCommand_t *cmd)`**（位于 [MainTask/therapy_ctrl.c](file:///c:/Users/Administrator/Desktop/淘晶驰git/Breath/MainTask/therapy_ctrl.c#L862)）。

### 4.1 参数配置指令 (`0x10`, `BLE_CMD_SET_PARAMS`)
APP 可在设置页面调整参数后一键下发，payload 共 6 个字节：
```
Payload: [freq_hz] [ch1_level] [ch2_level] [ch3_level] [timer_min] [volume]
字节大小: 1 Byte    1 Byte     1 Byte     1 Byte     1 Byte      1 Byte
```
- **主控解析流程**：
```c
/* 文件位置：MainTask/therapy_ctrl.c */
void App_Therapy_OnBleCommand(const BleRxCommand_t *cmd)
{
    if (cmd == NULL) return;

    if (cmd->cmd_id == BLE_CMD_SET_PARAMS && cmd->payload_len >= 6U)
    {
        uint8_t freq = cmd->payload[0]; // 频率 1~100Hz (疏密波自动钳位至20Hz)
        uint8_t c1   = cmd->payload[1]; // 通道1档位 (0~10)
        uint8_t c2   = cmd->payload[2]; // 通道2档位 (0~10)
        uint8_t c3   = cmd->payload[3]; // 通道3档位 (0~10)
        uint8_t tmin = cmd->payload[4]; // 定时时长 (5~30分钟)
        uint8_t vol  = cmd->payload[5]; // 音量大小 (0~10)

        /* 调用业务 Setter 自动完成合法性约束与模式适配 */
        App_Therapy_SetStimFreq(freq);
        App_Therapy_SetStimLevel(0U, c1);
        App_Therapy_SetStimLevel(1U, c2);
        App_Therapy_SetStimLevel(2U, c3);
        App_Therapy_SetTimerMin(tmin);
        App_Therapy_SetVolume(vol);

        /* 同步刷新底层硬件输出与屏幕界面显示 */
        BSP_Stim_SetAllLevels(g_app.stim_level);
        BSP_Screen_SetStimParams(g_app.stim_freq_hz, g_app.stim_level);
        BSP_Screen_SetSettingsDisplay(g_app.volume, g_app.timer_min, g_app.ble_enabled);
    }
}
```

### 4.2 运行控制指令 (`0x11`, `BLE_CMD_CTRL_STATE`)
用于 APP 控制治疗流程：
```
Payload: [action] (1 Byte)
action = 0x01: 启动 / 暂停 切换 (触发 App_Therapy_StartOrPause())
action = 0x02: 停止治疗并退回主菜单 (触发 App_Therapy_StopToMenu())
```

### 4.3 如何扩展全新自定义协议指令
若后续需要支持“读取历史日志”或“设备自检”等新功能，只需两步：
1. **添加命令枚举**：
   在 [Bluetooth/bsp_ble.h](file:///c:/Users/Administrator/Desktop/淘晶驰git/Breath/Bluetooth/bsp_ble.h) 的 `BleCmdId_t` 中新增枚举，如 `BLE_CMD_SELF_TEST = 0x20U`。
2. **添加解析分支**：
   在 [MainTask/therapy_ctrl.c](file:///c:/Users/Administrator/Desktop/淘晶驰git/Breath/MainTask/therapy_ctrl.c) 的 `App_Therapy_OnBleCommand()` 中追加：
   ```c
   else if (cmd->cmd_id == 0x20U)
   {
       // 执行相应业务操作...
   }
   ```

---

## 五、 APP 联调测试 Hex 报文速查样例

以常用的 BLE 调试助手（如手机端 `nRF Connect` 或 `BLE调试宝`）为例，连接 `E104-BT5005A` 后向 Characteristic `0000fff2-...` 发送十六进制原始报文：

### 5.1 启动/暂停治疗指令
- **发送报文**：`AA 55 02 11 01 1E 0D`
  - `AA 55`: 帧头
  - `02`: 长度 2（命令 1 字节 + 载荷 1 字节）
  - `11`: 命令码 `BLE_CMD_CTRL_STATE`
  - `01`: 动作码（启动/暂停）
  - `1E`: CRC-8 校验值
  - `0D`: 结束符

### 5.2 停止治疗返回菜单指令
- **发送报文**：`AA 55 02 11 02 17 0D`
  - `02`: 停止返回动作码

### 5.3 批量下发参数指令
- **需求**：设置电刺激频率 25Hz，通道1为5档，通道2为6档，通道3为7档，定时20分钟，音量8档。
  - Payload 为 `19 05 06 07 14 08`
  - LEN 为 `07`
  - 计算从 `07 10 19 05 06 07 14 08` 的 CRC-8 得到检验码。
  - 组装后发送：`AA 55 07 10 19 05 06 07 14 08 [CRC8] 0D`
- 设备收到后会立即同步刷新屏幕参数并改变硬件输出。
