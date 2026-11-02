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
 * resuming mid-read. TX runs at lower priority than the RX task, so RX
 * cannot start between this check and TX CS. */
static bool wait_ready_with_rx(void)
{
    TickType_t start = xTaskGetTickCount();

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
    eif_pull_up_wakeup_io();
    if (!wait_ready_with_rx()) {
        return -1;
    }
    GPIO_WriteBit(BLE_INTERNAL_CS_PORT, BLE_INTERNAL_CS_PIN, 0);
    for (uint16_t i = 0; i < len; ++i) {
        (void)QSPI_ReadWriteByte(data[i]);
    }
    GPIO_WriteBit(BLE_INTERNAL_CS_PORT, BLE_INTERNAL_CS_PIN, 1);
    eif_delay_us(1000);
    return 0;
}
