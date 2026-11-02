#include "FreeRTOS.h"
static inline BaseType_t xTaskCreate(void (*task)(void *), const char *name,
                                    unsigned stack, void *arg, unsigned priority, void *handle)
{
    return pdPASS;
}
