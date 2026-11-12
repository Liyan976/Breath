#ifndef __BSP_BLE_H
#define __BSP_BLE_H

#include "main.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BLE_FRAME_SOF0      0xAAU
#define BLE_FRAME_SOF1      0x55U
#define BLE_FRAME_EOF       0x0DU

typedef enum {
    BLE_CMD_TELEMETRY       = 0x01U,
    BLE_CMD_SET_PARAMS      = 0x10U,
    BLE_CMD_CTRL_STATE      = 0x11U,
    BLE_CMD_QUERY_STATUS    = 0x12U
} BleCmdId_t;

typedef struct {
    uint8_t  breath_mode;     /* 0:Off, 1:Simple, 2:Biofeedback */
    uint8_t  stim_mode;       /* 0:Off, 1:Continuous, 2:Intermittent, 3:Dense-Disperse */
    uint8_t  stim_freq_hz;    /* 0..100 */
    uint8_t  stim_level[3];   /* 0..10 */
    uint8_t  timer_min;       /* 5..30 */
    uint16_t elapsed_sec;
    uint16_t remain_sec;
    uint8_t  guide_bpm;       /* 5..30 */
    uint8_t  real_cbpm;       /* 0..50 */
    uint8_t  elec_drop_mask;  /* bit0:CH1, bit1:CH2, bit2:CH3 */
    uint8_t  running;
    uint8_t  paused;
} BleTelemetry_t;

typedef struct {
    uint8_t cmd_id;
    uint8_t payload[24];
    uint8_t payload_len;
} BleRxCommand_t;

typedef void (*BleRxCallback_t)(const BleRxCommand_t *cmd);

void BSP_BLE_Init(BleRxCallback_t cb);
void BSP_BLE_Reset(void);
void BSP_BLE_SetEnabled(uint8_t enable);
uint8_t BSP_BLE_IsConnected(void);

void BSP_BLE_OnUartRxCplt(void);
void BSP_BLE_OnDataReadyExti(void);
void BSP_BLE_Poll(void);

void BSP_BLE_SendTelemetry(const BleTelemetry_t *t);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_BLE_H */
