#include "bsp_audio.h"
#include "cmsis_os.h"

void AudioTask(void const *argument)
{
    (void)argument;

    osDelay(100U);

    for (;;)
    {
        BSP_Audio_Poll();
        osDelay(50U);
    }
}
