#include "FreeRTOS.h"
#include "task.h"

#include "pico/sync.h"

#if (configNUMBER_OF_CORES == 1)
void vApplicationIdleHook(void) {
    __wfi();
};
#else
void vApplicationPassiveIdleHook(void) {
    __wfi();
}
#endif

void vApplicationStackOverflowHook(TaskHandle_t Task, char *pcTaskName) {
    panic("stack overflow (not the helpful kind) for %s\n", pcTaskName);
}

void vApplicationMallocFailedHook(void) {
    panic("Malloc Failed\n");
};
