#ifndef __BSP_STORAGE_H
#define __BSP_STORAGE_H

#include "main.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t  breath_mode;
    uint8_t  stim_mode;
    uint8_t  stim_freq_hz;
    uint8_t  levels[3];
    uint16_t valid_sec;
    uint8_t  final_gbpm;
} StorageSessionRecord_t;

uint8_t BSP_Storage_IsCardPresent(void);
uint8_t BSP_Storage_Init(void);
uint8_t BSP_Storage_LogSession(uint8_t breath_mode,
                               uint8_t stim_mode,
                               uint8_t stim_freq_hz,
                               const uint8_t levels[3],
                               uint16_t valid_sec,
                               uint8_t final_gbpm);

/* Non-blocking request processed by StorageTask */
void BSP_Storage_RequestLog(const StorageSessionRecord_t *rec);
void BSP_Storage_ProcessTask(void);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_STORAGE_H */
