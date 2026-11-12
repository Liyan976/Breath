#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os.h"

osThreadId Main_TaskHandle;
osThreadId Screen_TaskHandle;
osThreadId Stim_TaskHandle;
osThreadId Ble_TaskHandle;
osThreadId Audio_TaskHandle;
osThreadId Storage_TaskHandle;

extern void MainTask(void const *argument);
extern void ScreenTask(void const *argument);
extern void StimTask(void const *argument);
extern void BleTask(void const *argument);
extern void AudioTask(void const *argument);
extern void StorageTask(void const *argument);

void MX_FREERTOS_Init(void);

void vApplicationGetIdleTaskMemory(StaticTask_t **ppxIdleTaskTCBBuffer,
                                   StackType_t **ppxIdleTaskStackBuffer,
                                   uint32_t *pulIdleTaskStackSize);

static StaticTask_t xIdleTaskTCBBuffer;
static StackType_t xIdleStack[configMINIMAL_STACK_SIZE];

void vApplicationGetIdleTaskMemory(StaticTask_t **ppxIdleTaskTCBBuffer,
                                   StackType_t **ppxIdleTaskStackBuffer,
                                   uint32_t *pulIdleTaskStackSize)
{
  *ppxIdleTaskTCBBuffer = &xIdleTaskTCBBuffer;
  *ppxIdleTaskStackBuffer = &xIdleStack[0];
  *pulIdleTaskStackSize = configMINIMAL_STACK_SIZE;
}

void MX_FREERTOS_Init(void)
{
  osThreadDef(Main_Task, MainTask, osPriorityAboveNormal, 0, 512);
  Main_TaskHandle = osThreadCreate(osThread(Main_Task), NULL);

  osThreadDef(Stim_Task, StimTask, osPriorityHigh, 0, 384);
  Stim_TaskHandle = osThreadCreate(osThread(Stim_Task), NULL);

  osThreadDef(Screen_Task, ScreenTask, osPriorityNormal, 0, 384);
  Screen_TaskHandle = osThreadCreate(osThread(Screen_Task), NULL);

  osThreadDef(Ble_Task, BleTask, osPriorityBelowNormal, 0, 384);
  Ble_TaskHandle = osThreadCreate(osThread(Ble_Task), NULL);

  osThreadDef(Audio_Task, AudioTask, osPriorityBelowNormal, 0, 384);
  Audio_TaskHandle = osThreadCreate(osThread(Audio_Task), NULL);

  osThreadDef(Storage_Task, StorageTask, osPriorityLow, 0, 512);
  Storage_TaskHandle = osThreadCreate(osThread(Storage_Task), NULL);
}
