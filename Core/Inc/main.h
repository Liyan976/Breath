/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   Pin definitions verified against EasyEDA Pro schematic
  *                   (main & breath pcb(breath), STM32F405RGT6 LQFP-64).
  ******************************************************************************
  */
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f4xx_hal.h"
#include <stdint.h>
#include <stdbool.h>

void Error_Handler(void);

/* ================= Power & Key ================= */
#define PWR_ON_Pin              GPIO_PIN_12
#define PWR_ON_GPIO_Port        GPIOA
#define P_SW1_Pin               GPIO_PIN_11
#define P_SW1_GPIO_Port         GPIOA

/* ================= TJC Serial Screen (USART3) ================= */
#define OLED_TX_Pin             GPIO_PIN_10   /* PB10 -> USART3_TX -> CN6-3 (OLED_RX) */
#define OLED_TX_GPIO_Port       GPIOB
#define OLED_RX_Pin             GPIO_PIN_11   /* PB11 -> USART3_RX -> CN6-2 (OLED_TX) */
#define OLED_RX_GPIO_Port       GPIOB

/* ================= Bluetooth E104-BT5005A (USART1) ================= */
#define BLE_TX_Pin              GPIO_PIN_9    /* PA9  -> USART1_TX -> U15 RXD */
#define BLE_TX_GPIO_Port        GPIOA
#define BLE_RX_Pin              GPIO_PIN_10   /* PA10 -> USART1_RX -> U15 TXD */
#define BLE_RX_GPIO_Port        GPIOA
#define BLE_LINK_Pin            GPIO_PIN_6    /* PA6  -> U15 LINK (Active Low) */
#define BLE_LINK_GPIO_Port      GPIOA
#define BLE_INT_Pin             GPIO_PIN_2    /* PB2  -> U15 DATA (EXTI2) */
#define BLE_INT_GPIO_Port       GPIOB
#define BLE_INT_EXTI_IRQn       EXTI2_IRQn
#define BLE_WKP_Pin             GPIO_PIN_3    /* PB3  -> U15 WKP */
#define BLE_WKP_GPIO_Port       GPIOB
#define BLE_MOD_Pin             GPIO_PIN_4    /* PB4  -> U15 MOD */
#define BLE_MOD_GPIO_Port       GPIOB
#define BLE_RST_Pin             GPIO_PIN_7    /* PB7  -> U15 RST (Active Low) */
#define BLE_RST_GPIO_Port       GPIOB

/* ================= Audio N9300 & 8002A (USART2) ================= */
#define AUDIO_TX_Pin            GPIO_PIN_2    /* PA2  -> USART2_TX -> N9300 RX */
#define AUDIO_TX_GPIO_Port      GPIOA
#define AUDIO_RX_Pin            GPIO_PIN_3    /* PA3  -> USART2_RX -> N9300 TX */
#define AUDIO_RX_GPIO_Port      GPIOA
#define AUDIO_BUSY_Pin          GPIO_PIN_8    /* PB8  -> N9300 BUSY */
#define AUDIO_BUSY_GPIO_Port    GPIOB
#define HP_DET_Pin              GPIO_PIN_4    /* PA4  -> 8002A SHUTDOWN & PJ-342A */
#define HP_DET_GPIO_Port        GPIOA
#define HP_MIC_Pin              GPIO_PIN_8    /* PA8  -> PJ-342A pin 2 */
#define HP_MIC_GPIO_Port        GPIOA

/* ================= Breath Sensor (NTC + OPA350) ================= */
#define BREATH_DET_Pin          GPIO_PIN_5    /* PA5  -> U45 PJ-342A jack detect */
#define BREATH_DET_GPIO_Port    GPIOA
#define BREATH_RECEIVE_Pin      GPIO_PIN_1    /* PB1  -> ADC1_IN9 */
#define BREATH_RECEIVE_GPIO_Port GPIOB

/* ================= Electrical Stimulation (3 Channels) ================= */
/* Boost PWMs */
#define BOOST_PWM1_Pin          GPIO_PIN_0    /* PA0  -> TIM5_CH1 */
#define BOOST_PWM1_GPIO_Port    GPIOA
#define BOOST_PWM2_Pin          GPIO_PIN_7    /* PA7  -> TIM14_CH1 */
#define BOOST_PWM2_GPIO_Port    GPIOA
#define BOOST_PWM3_Pin          GPIO_PIN_6    /* PB6  -> TIM4_CH1 */
#define BOOST_PWM3_GPIO_Port    GPIOB

/* Constant Current DAC PWMs */
#define CC_PWM11_Pin            GPIO_PIN_6    /* PC6  -> TIM3_CH1 */
#define CC_PWM11_GPIO_Port      GPIOC
#define CC_PWM22_Pin            GPIO_PIN_7    /* PC7  -> TIM3_CH2 */
#define CC_PWM22_GPIO_Port      GPIOC
#define CC_PWM33_Pin            GPIO_PIN_15   /* PB15 -> TIM12_CH2 */
#define CC_PWM33_GPIO_Port      GPIOB

/* H-Bridge Control (CH1: PB9/PB5, CH2: PB12/PB13, CH3: PB14/PC5) */
#define S11_Pin                 GPIO_PIN_9
#define S11_GPIO_Port           GPIOB
#define S12_Pin                 GPIO_PIN_5
#define S12_GPIO_Port           GPIOB
#define S21_Pin                 GPIO_PIN_12
#define S21_GPIO_Port           GPIOB
#define S22_Pin                 GPIO_PIN_13
#define S22_GPIO_Port           GPIOB
#define S31_Pin                 GPIO_PIN_14
#define S31_GPIO_Port           GPIOB
#define S32_Pin                 GPIO_PIN_5
#define S32_GPIO_Port           GPIOC

/* Feedback & Electrode Drop Detection ADCs */
#define PWM1_OUT_Pin            GPIO_PIN_1    /* PA1  -> ADC1_IN1 */
#define PWM1_OUT_GPIO_Port      GPIOA
#define PWM2_OUT_Pin            GPIO_PIN_0    /* PC0  -> ADC1_IN10 */
#define PWM2_OUT_GPIO_Port      GPIOC
#define PWM3_OUT_Pin            GPIO_PIN_1    /* PC1  -> ADC1_IN11 */
#define PWM3_OUT_GPIO_Port      GPIOC
#define PWM_1IN_Pin             GPIO_PIN_2    /* PC2  -> ADC1_IN12 */
#define PWM_1IN_GPIO_Port       GPIOC
#define PWM_2IN_Pin             GPIO_PIN_3    /* PC3  -> ADC1_IN13 */
#define PWM_2IN_GPIO_Port       GPIOC
#define PWM_3IN_Pin             GPIO_PIN_4    /* PC4  -> ADC1_IN14 */
#define PWM_3IN_GPIO_Port       GPIOC

/* ================= SD Card (SDIO) ================= */
#define SDIO_CD_Pin             GPIO_PIN_0    /* PB0  -> TF Card Detect (Active Low) */
#define SDIO_CD_GPIO_Port       GPIOB

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
