#include "bsp_storage.h"
#include "fatfs.h"
#include <stdio.h>
#include <string.h>

static uint8_t s_mounted = 0U;
static volatile uint8_t s_log_pending = 0U;
static StorageSessionRecord_t s_pending_rec;

uint8_t BSP_Storage_IsCardPresent(void)
{
    /* SDIO_CD (PB0) is pulled up 10k; inserted card pulls LOW */
    return (HAL_GPIO_ReadPin(SDIO_CD_GPIO_Port, SDIO_CD_Pin) == GPIO_PIN_RESET) ? 1U : 0U;
}

uint8_t BSP_Storage_Init(void)
{
    if (!BSP_Storage_IsCardPresent())
    {
        s_mounted = 0U;
        return 0U;
    }

    if (f_mount(&SDFatFS, (TCHAR const *)SDPath, 1U) == FR_OK)
    {
        s_mounted = 1U;
        return 1U;
    }

    s_mounted = 0U;
    return 0U;
}

uint8_t BSP_Storage_LogSession(uint8_t breath_mode,
                               uint8_t stim_mode,
                               uint8_t stim_freq_hz,
                               const uint8_t levels[3],
                               uint16_t valid_sec,
                               uint8_t final_gbpm)
{
    FIL file;
    char line[96];
    UINT bw = 0U;
    FRESULT res;
    uint8_t write_header = 0U;

    if (!BSP_Storage_IsCardPresent())
    {
        s_mounted = 0U;
        return 0U;
    }

    if (!s_mounted && !BSP_Storage_Init())
    {
        return 0U;
    }

    if (f_stat("0:/session.csv", NULL) != FR_OK)
    {
        write_header = 1U;
    }

    res = f_open(&file, "0:/session.csv", FA_OPEN_ALWAYS | FA_WRITE);
    if (res != FR_OK)
    {
        return 0U;
    }

    f_lseek(&file, f_size(&file));

    if (write_header)
    {
        const char *hdr = "breath_mode,stim_mode,freq_hz,ch1,ch2,ch3,valid_sec,final_gbpm\r\n";
        f_write(&file, hdr, (UINT)strlen(hdr), &bw);
    }

    snprintf(line, sizeof(line), "%u,%u,%u,%u,%u,%u,%u,%u\r\n",
             breath_mode, stim_mode, stim_freq_hz,
             levels[0], levels[1], levels[2],
             valid_sec, final_gbpm);
    res = f_write(&file, line, (UINT)strlen(line), &bw);
    f_sync(&file);
    f_close(&file);

    return (res == FR_OK) ? 1U : 0U;
}

void BSP_Storage_RequestLog(const StorageSessionRecord_t *rec)
{
    if (rec == NULL) return;
    s_pending_rec = *rec;
    s_log_pending = 1U;
}

void BSP_Storage_ProcessTask(void)
{
    uint8_t present = BSP_Storage_IsCardPresent();

    if (!present)
    {
        s_mounted = 0U;
        return;
    }

    if (!s_mounted)
    {
        BSP_Storage_Init();
    }

    if (s_log_pending && s_mounted)
    {
        StorageSessionRecord_t rec = s_pending_rec;
        s_log_pending = 0U;
        BSP_Storage_LogSession(rec.breath_mode,
                               rec.stim_mode,
                               rec.stim_freq_hz,
                               rec.levels,
                               rec.valid_sec,
                               rec.final_gbpm);
    }
}
