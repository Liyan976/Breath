# Breath - Fatty Liver Regulatory Instrument Firmware (STM32F405RGT6)

STM32Cube HAL + FreeRTOS + FatFs + CMSIS-DSP project for the Fatty Liver Regulatory Instrument (**STM32F405RGT6 @ 168 MHz**), structured after the FPM reference template and verified against the live EasyEDA Pro schematic (`main & breath pcb(breath)`).

## Directory Structure

- `Core/`: System clock (HSE 8 MHz -> 168 MHz, SDIO 48 MHz), GPIO, ADC1+DMA, TIM2/3/4/5/6/12/14, USART1/2/3, SDIO, FreeRTOS task creation.
- `Screen/`: TJC serial screen driver (`bsp_screen.c/.h`) and `ScreenTask` (`screen_task.c`).
- `Bluetooth/`: E104-BT5005A BLE driver (`bsp_ble.c/.h`) and `BleTask` (`ble_task.c`).
- `Audio/`: N9300 + 8002A audio driver (`bsp_audio.c/.h`) and `AudioTask` (`audio_task.c`).
- `Stimulator/`: 3-channel Boost/CC/H-Bridge/Electrode driver (`bsp_stimulator.c/.h`) and `StimTask` (`stim_task.c`).
- `BreathSensor/`: NTC breath ADC & jack detect (`bsp_breath_sensor.c/.h`, sampled by `MainTask`).
- `Storage/`: SDIO TF card & FatFs CSV logging (`bsp_storage.c/.h`) and `StorageTask` (`storage_task.c`).
- `MainTask/`: Global state machine (`app_types.h`, `therapy_ctrl.c/.h`), power latch (`bsp_power.c/.h`), and `MainTask` (`main_task.c`).
- `Algorithm/`: `algo_breath` (NTC LPF + first-order difference `k1`/`k2` + amplitude threshold `fmin` valley detection, 3-breath CBPM -> GBPM adaptive step-down, Simple 10->5 BPM table, strict 4:6 unified guide generator).
- `FATFS/`: SDIO 4-bit TF card driver and FatFs R0.12c integration.
- `MDK-ARM/`: Keil uVision5 project (`Breath.uvprojx`) targeting `STM32F405RGTx`.

## Verified Schematic Pinout (STM32F405RGT6 LQFP-64)

| Subsystem | Pin | Net Name | Peripheral / Function |
|---|---|---|---|
| **TJC Screen** | PB10 / PB11 | `OLED_RX` / `OLED_TX` | `USART3_TX` / `USART3_RX` (115200 8N1) |
| **BLE (E104-BT5005A)** | PA9 / PA10 | `USART1_TX` / `USART1_RX` | `USART1_TX` / `USART1_RX` (115200 8N1) |
| **BLE Control** | PA6 / PB2 / PB7 / PB3 / PB4 | `BLE_LINK` / `BLE_INT` / `BLE_RST` / `BLE_WKP` / `BLE_MOD` | Link input (active low) / EXTI2 / Reset / Wake / Mode |
| **Audio (N9300 + 8002A)** | PA2 / PA3 / PB8 / PA4 / PA8 | `USART2_TX` / `USART2_RX` / `BUSY` / `HP_DET` / `HP_MIC` | `USART2` (9600 8N1) / Busy / Headphone Detect |
| **Breath Sensor (NTC)** | PB1 / PA5 | `BREATH_RECEIVE` / `BREATH_DET` | `ADC1_IN9` / Jack Insert Detect |
| **Boost PWM (CH1/2/3)** | PA0 / PA7 / PB6 | `PWM1` / `PWM2` / `PWM3` | `TIM5_CH1` / `TIM14_CH1` / `TIM4_CH1` (50 kHz) |
| **Boost Feedback ADC** | PA1 / PC0 / PC1 | `PWM1_OUT` / `PWM2_OUT` / `PWM3_OUT` | `ADC1_IN1` / `ADC1_IN10` / `ADC1_IN11` |
| **CC DAC PWM (CH1/2/3)** | PC6 / PC7 / PB15 | `PWM11` / `PWM22` / `PWM33` | `TIM3_CH1` / `TIM3_CH2` / `TIM12_CH2` (10 kHz) |
| **H-Bridge (CH1/2/3)** | PB9,PB5 / PB12,PB13 / PB14,PC5 | `S11,S12` / `S21,S22` / `S31,S32` | Bipolar 250 us pulse (`TIM2` 50 us ISR) |
| **Electrode Drop ADC** | PC2 / PC3 / PC4 | `PWM_1IN` / `PWM_2IN` / `PWM_3IN` | `ADC1_IN12` / `ADC1_IN13` / `ADC1_IN14` |
| **SD Card (SDIO)** | PC8~PC12, PD2, PB0 | `SDIO_D0..D3,CK,CMD,CD` | `SDIO` 4-bit + Card Detect |
| **Power Control** | PA12 / PA11 | `PWR_ON` / `P_SW1` | Latch Output / Key Input (Active Low, 1.5 s) |
