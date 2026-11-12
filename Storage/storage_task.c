#include "bsp_storage.h"
#include "cmsis_os.h"

void StorageTask(void const *argument)
{
    (void)argument;

    osDelay(200U);

    for (;;)
    {
        BSP_Storage_ProcessTask();
        osDelay(500U);
    }
}
