#include "bsp_power.h"

static PowerOffRequestCallback_t s_off_cb = NULL;
static uint8_t  s_boot_release_seen = 0U;
static uint32_t s_press_start_ms = 0U;
static uint8_t  s_off_triggered = 0U;

void BSP_Power_Init(PowerOffRequestCallback_t off_cb)
{
    s_off_cb = off_cb;
    s_boot_release_seen = 0U;
    s_press_start_ms = 0U;
    s_off_triggered = 0U;
    BSP_Power_LatchOn();
}

void BSP_Power_LatchOn(void)
{
    HAL_GPIO_WritePin(PWR_ON_GPIO_Port, PWR_ON_Pin, GPIO_PIN_SET);
}

void BSP_Power_ShutdownNow(void)
{
    HAL_GPIO_WritePin(PWR_ON_GPIO_Port, PWR_ON_Pin, GPIO_PIN_RESET);
}

uint8_t BSP_Power_IsKeyPressed(void)
{
    /* KEY1 pulls D9 cathode to GND -> P_SW1 (PA11) is active LOW */
    return (HAL_GPIO_ReadPin(P_SW1_GPIO_Port, P_SW1_Pin) == GPIO_PIN_RESET) ? 1U : 0U;
}

void BSP_Power_Poll10ms(uint32_t now_ms)
{
    uint8_t pressed = BSP_Power_IsKeyPressed();

    /* Ignore the initial power-on hold until user releases KEY1 once */
    if (!s_boot_release_seen)
    {
        if (!pressed) s_boot_release_seen = 1U;
        return;
    }

    if (pressed)
    {
        if (s_press_start_ms == 0U)
        {
            s_press_start_ms = now_ms;
        }
        else if (!s_off_triggered && ((now_ms - s_press_start_ms) >= POWER_KEY_LONG_PRESS_MS))
        {
            s_off_triggered = 1U;
            if (s_off_cb != NULL)
            {
                s_off_cb();
            }
            BSP_Power_ShutdownNow();
        }
    }
    else
    {
        s_press_start_ms = 0U;
    }
}

uint8_t BSP_Power_GetBatteryPercent(void)
{
    return 100U;
}
