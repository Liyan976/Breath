#include "bsp_stimulator.h"
#include "cmsis_os.h"

void StimTask(void const *argument)
{
    TickType_t last_wake;
    (void)argument;

    osDelay(50U);
    last_wake = xTaskGetTickCount();

    for (;;)
    {
        BSP_Stim_UpdateLoop10ms(HAL_GetTick());
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(10U));
    }
}
