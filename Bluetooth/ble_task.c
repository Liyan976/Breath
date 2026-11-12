#include "bsp_ble.h"
#include "app_types.h"
#include "cmsis_os.h"

void BleTask(void const *argument)
{
    uint32_t tick_cnt = 0U;
    (void)argument;

    osDelay(100U);

    for (;;)
    {
        BSP_BLE_Poll();

        if ((tick_cnt % 10U) == 0U && g_app.ble_enabled) /* Every 500 ms */
        {
            BleTelemetry_t t;
            uint32_t total_s = (uint32_t)g_app.timer_min * 60U;
            uint32_t elap_s  = g_app.active_elapsed_ms / 1000U;

            t.breath_mode    = (uint8_t)g_app.Breath_Mode;
            t.stim_mode      = (uint8_t)g_app.Stim_Mode;
            t.stim_freq_hz   = g_app.stim_freq_hz;
            t.stim_level[0]  = g_app.stim_level[0];
            t.stim_level[1]  = g_app.stim_level[1];
            t.stim_level[2]  = g_app.stim_level[2];
            t.timer_min      = g_app.timer_min;
            t.elapsed_sec    = (uint16_t)elap_s;
            t.remain_sec     = (total_s > elap_s) ? (uint16_t)(total_s - elap_s) : 0U;
            t.guide_bpm      = g_app.guide_bpm;
            t.real_cbpm      = g_app.real_cbpm;
            t.elec_drop_mask = g_app.elec_drop_mask;
            t.running        = g_app.running;
            t.paused         = g_app.paused;
            BSP_BLE_SendTelemetry(&t);
        }

        tick_cnt++;
        osDelay(50U);
    }
}
