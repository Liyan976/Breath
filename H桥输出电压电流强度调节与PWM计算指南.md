# H 桥输出电压电流强度调节与 PWM 计算指南

本指南系统分析了本调控仪电刺激通道的硬件电路拓扑、刺激体感的物理与生理学本质、各组 PWM 的分工与精确计算公式，并提供了如何在软件代码中调整最终 H 桥输出强度与频率的完整实操方法。

---

## 一、 电刺激输出的物理实质与体感认知

### 1.1 为什么万用表测得 H 桥电压只有 3V 多、电流 0.5mA？
在调试过程中，实测 VOUT 升压输出可达 30V 以上，但在电极或纯阻假负载端用万用表测得 H 桥输出电压仅约 3V~4V，电流仅约 0.5mA。这是**正常的仪表测量平均值/有效值效应**，并非电路未能输出高压：
- **脉冲占空比极低**：
  若基频设定为 $F = 20\text{ Hz}$（周期 $T = 50\text{ ms} = 50000\mu\text{s}$），单相脉宽为 $T_{pulse} = 250\mu\text{s}$（正负各一相，总导通时间 $500\mu\text{s}$）。
  $$\text{占空比 } D = \frac{500\mu\text{s}}{50000\mu\text{s}} = 0.01 = 1\%$$
- **测量原理差异**：
  - 数字万用表直流档（DC）测得的是时间平均值：$V_{DC} \approx 0\text{ V}$（正负脉冲对称抵消）。
  - 数字万用表交流档（AC True-RMS）测得的是方波脉冲的有效值：
    $$V_{RMS} = V_{peak} \times \sqrt{D} \approx 32\text{ V} \times \sqrt{0.01} = 3.2\text{ V}$$
  - 因此，万用表显示的 3.2V 恰恰印证了**瞬态峰值电压确实达到了 32V 满幅输出**！若要观察真实的 30V 脉冲幅度，请使用**示波器**挂接探头查看。

### 1.2 肌肉收缩与电刺激体感的生理学规律（Lapicque 曲线）
神经肌肉刺激的兴奋阈值取决于**单相脉冲电荷量 $Q$**：
$$Q = I_{peak} \times T_{pulse}$$
以及脉冲能量：
$$E = V_{peak} \times I_{peak} \times T_{pulse}$$
- **脉宽影响**：人体皮肤和皮下组织存在显著的容抗特性（阻容并联模型，$\tau \approx 100\mu\text{s} \sim 300\mu\text{s}$）。原 $250\mu\text{s}$ 脉宽过窄，脉冲能量多被电容旁路，尚未充分极化运动神经膜电位便已结束，导致深层肌肉无法收缩。
- **强度倍增方案**：将脉宽拉宽至 **$400\mu\text{s}$**（8 个 tick），同时微调瞬态峰值限流值，即可成倍提升神经纤维的动作电位募集率，带来显著深层肌肉收缩感。

---

## 二、 硬件拓扑与三组 PWM 定时器架构

电刺激系统由三级串联电路构成，主控 STM32 分别通过三组独立定时器进行精确调制：

```
+---------------------------------------------------------------------------------------+
| 1. Boost 升压源 (PA0/PA7/PB6 -> 50 kHz PWM)                                           |
|    将锂电池 3.7V~4.2V 闭环升压至 VOUT = 5V ~ 30V                                      |
+---------------------------------------------------------------------------------------+
                                           | (提供高压顺应母线)
                                           v
+---------------------------------------------------------------------------------------+
| 2. 恒流源反馈控制 (PC6/PC7/PB15 -> 10 kHz PWM DAC 偏置)                                |
|    比较器 + 10Ω 采样电阻，限制每相脉冲的瞬时峰值电流 I_peak                             |
+---------------------------------------------------------------------------------------+
                                           | (恒流限流驱动)
                                           v
+---------------------------------------------------------------------------------------+
| 3. 全桥 H 桥输出级 (PB9,PB5 / PB12,PB13 / PB14,PC5 -> TIM2 50us ISR 极性调制)           |
|    生成正负交替双极性方波脉冲 (正脉冲 -> 死区 -> 负脉冲 -> 空闲)                      |
+---------------------------------------------------------------------------------------+
```

### 2.1 第一级：Boost 升压 PWM（电压顺应性源）
- **外设与引脚**：
  - 通道 1: `TIM5_CH1  (PA0)`
  - 通道 2: `TIM14_CH1 (PA7)`
  - 通道 3: `TIM4_CH1  (PB6)`
- **PWM 频率**：固定为 **50 kHz**（周期 $20\mu\text{s}$）。
- **控制算法**：闭环 PI 调节器（位于 `Stimulator/bsp_stimulator.c` 的 `BSP_Stim_UpdateLoop10ms()`）。
- **目标电压公式**：
  $$V_{target\_mV} = STIM\_BOOST\_MIN\_MV + \frac{Level}{10} \times (STIM\_BOOST\_MAX\_MV - STIM\_BOOST\_MIN\_MV)$$
  - 1 档目标约 $7.5\text{ V}$，10 档目标约 $30\text{ V}$（实测可稳定输出至 $32\text{ V}$）。

### 2.2 第二级：恒流基准 DAC PWM（峰值电流限制）
- **外设与引脚**：
  - 通道 1: `TIM3_CH1  (PC6)`
  - 通道 2: `TIM3_CH2  (PC7)`
  - 通道 3: `TIM12_CH2 (PB15)`
- **PWM 频率**：固定为 **10 kHz**（自动重装载值 `CC_DAC_PWM_PERIOD = 8400U`）。
- **电路分压与偏置推导**：
  - 硬件原理图中，单片机 3.3V PWM 经 RC 低通滤波后通过分压网络（$R_{top} = 3.74\text{ k}\Omega, R_{bot} = 20\text{ k}\Omega$）偏置到比较器基准端：
    $$V_{pwm\_max} = 3.3\text{ V} \times \frac{20\text{ k}}{20\text{ k} + 3.74\text{ k}} \approx 2.780\text{ V}$$
  - 采样电阻为 $R_{shunt} = 10\ \Omega$。当需要设定最大峰值电流为 $I_{peak}$ 时，采样电阻上的目标压降为：
    $$V_{set} = I_{peak} \times R_{shunt} = I_{peak} \times 10\ \Omega$$
  - PWM 比较寄存器 CCR 值的计算通用公式为：
    $$CCR = CC\_DAC\_PWM\_PERIOD \times \frac{V_{set}}{V_{pwm\_max}} = 8400 \times \frac{I_{peak} \times 10}{2.780}$$

### 2.3 第三级：H 桥双极性脉冲调制（时序与频率）
- **外设与中断**：`TIM2` 基础中断，每 **50 μs** 触发一次中断（$Tick\_Rate = 20\text{ kHz}$）。
- **状态机时序流程**：
  ```
  [HB_STATE_IDLE] 
       │ 经过周期等待 ticks (与设定频率成反比)
       ▼
  [HB_STATE_POS_PULSE] : S_x1=1, S_x2=0  (持续 PULSE_WIDTH_TICKS)
       │
       ▼
  [HB_STATE_DEADTIME]  : S_x1=0, S_x2=0  (持续 PULSE_DEADTIME_TICKS)
       │
       ▼
  [HB_STATE_NEG_PULSE] : S_x1=0, S_x2=1  (持续 PULSE_WIDTH_TICKS)
       │
       ▼
  [HB_STATE_IDLE]      : S_x1=0, S_x2=0  (等待下一个刺激周期)
  ```
- **工作周期 ticks 与频率换算公式**：
  $$Period\_ticks = \frac{TICK\_RATE\_HZ}{Freq\_Hz} = \frac{20000}{Freq\_Hz}$$
  - 当 $Freq = 20\text{ Hz}$ 时，$Period\_ticks = 1000$（周期 50 ms）；
  - 当 $Freq = 100\text{ Hz}$ 时，$Period\_ticks = 200$（周期 10 ms）。

---

## 三、 刺激强度“调至 2 倍”理论计算推导

根据用户要求，对比竞品最大档仅相当于对方中间档，需将刺激强度**理论计算调至 2 倍**：

### 3.1 原方案参数评估
- 原脉冲宽度：`PULSE_WIDTH_TICKS = 5U` $\implies T_{pulse} = 5 \times 50\mu\text{s} = 250\mu\text{s}$
- 原峰值限流：$I_{peak\_max} = 30\text{ mA} \implies CCR = 8400 \times \frac{0.300}{2.780} = 906$
- **原单相刺激电荷量**：
  $$Q_{old} = I_{peak\_max} \times T_{pulse} = 30\text{ mA} \times 250\mu\text{s} = 7500\text{ nC}$$

### 3.2 2 倍强度计算与参数选型
要实现 $Q_{new} = 2.0 \times Q_{old} = 15000\text{ nC}$：
1. **脉宽优化**：
   人体神经兴奋的最佳脉宽区间在 $350\mu\text{s} \sim 500\mu\text{s}$。将脉宽设为 **8 个 tick**：
   $$T_{pulse\_new} = 8 \times 50\mu\text{s} = 400\mu\text{s} \quad (增益 1.6 \times)$$
2. **电流上限计算**：
   为达成整整 2.0 倍电荷量，所需峰值电流为：
   $$I_{peak\_new} = \frac{Q_{new}}{T_{pulse\_new}} = \frac{15000\text{ nC}}{400\mu\text{s}} = 37.5\text{ mA}$$
3. **对应 DAC 寄存器 CCR 计算**：
   $$V_{set} = 0.0375\text{ A} \times 10\ \Omega = 0.375\text{ V}$$
   $$CCR_{new} = 8400 \times \frac{0.375\text{ V}}{2.780\text{ V}} \approx \mathbf{1133}$$
4. **电荷量校核**：
   $$Q_{new} = 37.5\text{ mA} \times 400\mu\text{s} = 15000\text{ nC} = \mathbf{2.00 \times Q_{old}}$$

---

## 四、 软件修改位置与参数配置速查表

所有强度与频率控制参数均已模块化封装在 [Stimulator/bsp_stimulator.c](file:///c:/Users/Administrator/Desktop/淘晶驰git/Breath/Stimulator/bsp_stimulator.c) 与对应头文件中，修改如下：

### 4.1 参数速查表
| 参数宏 / 变量 | 文件路径 | 原默认值 | 当前 2 倍值 | 极强体验备选 | 作用说明 |
|---|---|---|---|---|---|
| `PULSE_WIDTH_TICKS` | `Stimulator/bsp_stimulator.c` (L7) | 5 (250μs) | **8 (400μs)** | 10 (500μs) | H桥单相脉冲持续时间 |
| `PULSE_DEADTIME_TICKS` | `Stimulator/bsp_stimulator.c` (L8) | 1 (50μs) | **1 (50μs)** | 2 (100μs) | 正负脉冲切换死区时间 |
| `CC_DAC_CCR_MAX` | `Stimulator/bsp_stimulator.c` (L18)| 906 (30mA) | **1133 (37.5mA)**| 1360 (45mA) | 恒流源 10 档峰值电流上限 |
| `STIM_BOOST_MIN_MV` | `Stimulator/bsp_stimulator.h` (L16)| 5000 (5V) | **5000 (5V)** | 7500 (7.5V) | 1 档 Boost 最低目标升压 |
| `STIM_BOOST_MAX_MV` | `Stimulator/bsp_stimulator.h` (L15)| 30000 (30V) | **30000 (30V)**| 35000 (35V) | 10 档 Boost 最高目标升压 |

### 4.2 核心代码修改位置直达

#### 1) 脉宽与限流上限修改位置：[Stimulator/bsp_stimulator.c](file:///c:/Users/Administrator/Desktop/淘晶驰git/Breath/Stimulator/bsp_stimulator.c#L6-L19)
```c
/* 50 us TIM2 tick constants: 400 us pulse width = 8 ticks, 50 us dead-time = 1 tick */
#define PULSE_WIDTH_TICKS       8U     /* 设为 8 对应 400us；设为 10 对应 500us */
#define PULSE_DEADTIME_TICKS    1U     /* 死区 50us */
#define TICK_RATE_HZ            20000U

/* 恒流 DAC 占空比 CCR 设置 (2倍强度：37.5mA 对应 1133；若需 45mA 填 1360) */
#define CC_DAC_CCR_MAX          1133U
```

#### 2) 各档位电流线性映射函数：[Stimulator/bsp_stimulator.c](file:///c:/Users/Administrator/Desktop/淘晶驰git/Breath/Stimulator/bsp_stimulator.c#L324-L328)
```c
/* 1~10 档平滑线性映射至 0 ~ CC_DAC_CCR_MAX */
uint32_t dac_ccr = (uint32_t)((s_current_level_f[ch] / (float)STIM_LEVEL_MAX) * (float)CC_DAC_CCR_MAX);
set_cc_dac_ccr(ch, dac_ccr);
```

#### 3) 运行时频率动态刷新函数：[Stimulator/bsp_stimulator.c](file:///c:/Users/Administrator/Desktop/淘晶驰git/Breath/Stimulator/bsp_stimulator.c#L185-L198)
```c
void BSP_Stim_SetRuntimeFreqHz(uint8_t freq_hz)
{
    uint32_t period;
    if (freq_hz < STIM_FREQ_MIN_HZ) freq_hz = STIM_FREQ_MIN_HZ;
    if (freq_hz > STIM_FREQ_MAX_HZ) freq_hz = STIM_FREQ_MAX_HZ;
    s_runtime_freq_hz = freq_hz;
    
    /* 周期自动随设定的 freq_hz 动态更新 */
    period = TICK_RATE_HZ / (uint32_t)freq_hz;
    if (period < (PULSE_WIDTH_TICKS * 2U + PULSE_DEADTIME_TICKS + 4U))
    {
        period = PULSE_WIDTH_TICKS * 2U + PULSE_DEADTIME_TICKS + 4U;
    }
    s_period_ticks = period;
}
```

---

## 五、 实测调校建议步骤

1. **示波器挂载验证**：
   将示波器探头地线接板卡模拟地，探针接 H 桥通道 1 的输出端（配合 1kΩ 假负载电阻），上电运行电刺激模式：
   - 观察双极性正负方波宽度应稳定为 **400 μs**；
   - 切换 1~10 档，观察峰值电压从约 7.5V 平滑上升至 30V；
   - 切换频率设置（如 20Hz、50Hz、100Hz），观察脉冲重复周期严格由 50ms 变至 20ms、10ms。
2. **人体贴片体感测试**：
   贴上电极片进行实操体验：
   - 档位 1~3 档：应能感受到轻微温和的电流震颤感；
   - 档位 4~7 档：应能感受到明显的节律性肌肉微收缩；
   - 档位 8~10 档：肌肉应产生充分有力的收缩带动，感受与竞品高档位一致。
3. **若仍需进一步增强**：
   若个别肌肉部位较厚仍需更强体感，可直接将 `CC_DAC_CCR_MAX` 由 `1133` 提高至 `1360`（45mA）甚至 `1500`（50mA）。
