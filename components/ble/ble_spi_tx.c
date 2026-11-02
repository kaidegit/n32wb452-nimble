/*
 * Checked SPI TX for the NZ8801 link (see ble_spi_tx.h).
 * Ported from the vendor-order-preserving sender of the reference stack.
 */
#include <stdbool.h>
#include "FreeRTOS.h"
#include "task.h"
#include "ble_spi_tx.h"
#include "n32wb452.h"
#include "n32wb452_gpio.h"

extern void eif_pull_up_wakeup_io(void);
extern uint8_t eif_get_status_io_value(void);
extern uint8_t QSPI_ReadWriteByte(uint8_t data);
extern void eif_delay_us(uint32_t count);
int ble_spi_rx_pending(void);   /* from hci_spi.c */

/* Internal BLE CS GPIO (verified from the driver archive's literal pool;
 * not one of the public GPIOA..GPIOE definitions). */
#define BLE_INTERNAL_CS_PORT ((GPIO_Module *)0x40011c00UL)
#define BLE_INTERNAL_CS_PIN  ((uint16_t)1)

/* Only sleep before CS assertion. RX may clear wakeup, so reassert after
 * draining. Waiting for rx_pending as well as the IRQ line prevents
 * resuming mid-read. The caller rechecks with task switching suspended
 * before asserting CS, since an RX task can otherwise clear the handshake. */
static bool wait_ready_with_rx(TickType_t start)
{
    eif_pull_up_wakeup_io();
    while (xTaskGetTickCount() - start < pdMS_TO_TICKS(1600)) {
        if (ble_spi_rx_pending()) {
            do {
                vTaskDelay(1);
            } while (ble_spi_rx_pending() &&
                     xTaskGetTickCount() - start < pdMS_TO_TICKS(1600));
            eif_pull_up_wakeup_io();
            continue;
        }
        if (eif_get_status_io_value()) {
            return true;
        }
        eif_delay_us(50);
    }
    return false;
}

int ble_spi_send_checked(const uint8_t *data, uint16_t len)
{
    TickType_t start = xTaskGetTickCount();
    for (;;) {
        if (!wait_ready_with_rx(start)) return -1;

        /* RX and TX use the same SPI peripheral. Keep the CS transaction
         * indivisible with respect to other tasks, while allowing USB/SPI
         * interrupts to run and queue their work. Never suspend while waiting
         * for ready: the pending RX task must be allowed to drain first. */
        vTaskSuspendAll();
        if (ble_spi_rx_pending() || !eif_get_status_io_value()) {
            xTaskResumeAll();
            continue;
        }
        GPIO_WriteBit(BLE_INTERNAL_CS_PORT, BLE_INTERNAL_CS_PIN, 0);
        for (uint16_t i = 0; i < len; ++i) {
            (void)QSPI_ReadWriteByte(data[i]);
        }
        GPIO_WriteBit(BLE_INTERNAL_CS_PORT, BLE_INTERNAL_CS_PIN, 1);
        xTaskResumeAll();
        break;
    }
    /* Preserve the vendor driver's inter-frame delay. */
    eif_delay_us(1000);
    return 0;
}
