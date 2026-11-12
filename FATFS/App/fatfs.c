#include "fatfs.h"

uint8_t retSD;
char    SDPath[4];
FATFS   SDFatFS;
FIL     SDFile;

void MX_FATFS_Init(void)
{
    retSD = FATFS_LinkDriver(&SD_Driver, SDPath);
}

DWORD get_fattime(void)
{
    return ((DWORD)(2026 - 1980) << 25) |
           ((DWORD)11 << 21) |
           ((DWORD)12 << 16) |
           ((DWORD)12 << 11) |
           ((DWORD)0  << 5)  |
           ((DWORD)0  >> 1);
}
