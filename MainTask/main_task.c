#include "therapy_ctrl.h"
#include "cmsis_os.h"

void MainTask(void const *argument)
{
    TickType_t last_wake;
    uint32_t div_20ms = 0U;
    (void)argument;

    App_Therapy_Init();
    last_wake = xTaskGetTickCount();

    for (;;)
    {
        uint32_t now_ms = HAL_GetTick();

        App_Therapy_MainTick10ms(now_ms);

        /* 50 Hz (20 ms) breath sensor read & unified time-axis sync */
        div_20ms ^= 1U;
        if (div_20ms == 0U)
        {
            App_Therapy_UnifiedTimeAxisStep20ms(now_ms);
        }

        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(10U));
    }
}
