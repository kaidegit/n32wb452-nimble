/*
 * N32WB452 NimBLE firmware entry.
 *
 * Common bring-up (both modes):
 *   clocks (SystemInit, 144 MHz HSE PLL) → RTT console → FreeRTOS →
 *   mode-specific controller setup. NimBLE downloads the built-in firmware;
 *   bridge holds the controller in reset until a HID request arrives.
 *
 * Then, per firmware mode (compile-time switch injected by CMake):
 *   APP_MODE_NIMBLE : NimBLE host on-chip, advertising an echo service.
 *   APP_MODE_BRIDGE : USB CDC ACM ↔ SPI H4 passthrough for a host on the PC.
 */
#include <stdio.h>
#include <stdint.h>

#include "n32wb452.h"
#include "SEGGER_RTT.h"
#include "FreeRTOS.h"
#include "task.h"

#include "log.h"
#include "ble_platform.h"
#include "n32wb452_it.h"

#if defined(APP_MODE_NIMBLE)
#include "app_nimble.h"
#define APP_MODE_NAME "nimble"
#elif defined(APP_MODE_BRIDGE)
#include "app_bridge.h"
#define APP_MODE_NAME "cdc-bridge"
#else
#error "No APP_MODE_* defined (expected APP_MODE_NIMBLE or APP_MODE_BRIDGE)"
#endif

#define TAG  "main"
#define MAIN_TASK_STACK  1024
#define MAIN_TASK_PRIO   4

#if defined(APP_MODE_NIMBLE)
static void ble_bringup(void)
{
    int32_t rc;

    LOGI(TAG, "BLE interface init");
    ble_interface_init();

    LOGI(TAG, "BLE hardware init (SPI + reset core)");
    rc = ble_hardware_init();
    if (rc != 0) {
        LOGE(TAG, "ble_hardware_init rc=%ld", (long)rc);
    }

    /* Firmware download to the NZ8801: Copy/Ramcode/Patch/Nvds arrays,
     * each frame acknowledged by a 5-byte H4 event from the core ROM. */
    LOGI(TAG, "downloading NZ8801 firmware...");
    rc = ble_initdata_down();
    if (rc != 0) {
        LOGE(TAG, "ble_initdata_down rc=%ld (core stuck? power-cycle the board)", (long)rc);
    } else {
        LOGI(TAG, "NZ8801 firmware download ok");
    }
}

#endif

static void main_task(void *arg)
{
    (void)arg;

    LOGI(TAG, "mode: " APP_MODE_NAME);

#if defined(APP_MODE_NIMBLE)
    ble_bringup();
#else
    ble_controller_prepare();
#endif

#if defined(APP_MODE_NIMBLE)
    app_nimble_start();
#else
    app_bridge_start();
#endif

    uint32_t loops = 0;
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(30000));
        LOGI(TAG, "alive #%lu", (unsigned long)++loops);
    }
}

int main(void)
{
    /* NVIC priority group 4: mandatory for FreeRTOS (all 4 bits preempt). */
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_4);


    printf("\r\n=== n32wb452-nimble (" APP_MODE_NAME ") ===\r\n");
    printf("sysclk=%lu Hz  FreeRTOS heap=%u bytes\r\n",
           SystemCoreClock, (unsigned)configTOTAL_HEAP_SIZE);

    if (xTaskCreate(main_task, "main", MAIN_TASK_STACK, NULL,
                    MAIN_TASK_PRIO, NULL) != pdPASS) {
        printf("FATAL: cannot create main task\r\n");
        while (1) { }
    }

    vTaskStartScheduler();

    /* unreachable */
    printf("FATAL: scheduler returned\r\n");
    while (1) { }
}

/* ---- FreeRTOS hooks ---- */
void vApplicationMallocFailedHook(void)
{
    printf("FATAL: FreeRTOS heap exhausted\r\n");
    app_panic("oom");
}

void vApplicationStackOverflowHook(TaskHandle_t task, char *name)
{
    (void)task;
    printf("FATAL: stack overflow in '%s'\r\n", name ? name : "?");
    app_panic("stack");
}
