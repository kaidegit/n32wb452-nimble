#include "FreeRTOS.h"
typedef void *SemaphoreHandle_t;
static inline SemaphoreHandle_t xSemaphoreCreateBinary(void) { return (void *)1; }
static inline int xSemaphoreTake(SemaphoreHandle_t semaphore, uint32_t timeout) { return 1; }
static inline void xSemaphoreGiveFromISR(SemaphoreHandle_t semaphore, BaseType_t *woken) { }
