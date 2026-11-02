/*
 * NimBLE custom HCI transport: H4 over the internal SPI link to the
 * NZ8801 controller core.
 *
 * TX: ble_transport_to_ll_cmd/acl_impl → H4 frame → ble_spi_send_checked()
 * RX: PE14 EXTI (data-valid) → semaphore → RX task reads one H4 frame.
 *
 * Known silicon pitfalls handled here:
 *   - eif_spi_recv_irq_enable() internally reprograms the NVIC priority
 *     group to Group_2, breaking FreeRTOS ISR safety → restored to Group_4
 *     right after the call.
 *   - Feature reports are patched via hci_fixup_evt() before publishing.
 */
#ifndef HCI_SPI_H
#define HCI_SPI_H

#include <stdint.h>

/* Called from EXTI15_10_IRQHandler (ISR context). */
void hci_spi_irq_handler(void);

/* Bring the transport up: sync objects, RX task, SPI IRQ enable + NVIC
 * priority-group fix. Bridge creates the task with RX disabled until HID enables it. */
void hci_spi_transport_init(void);
/* Wait for any active read, then enable/disable controller RX for ROM access. */
void hci_spi_set_enabled(int enabled);

/* Read one H4 frame from the controller into a flat buffer
 * (buf[0] = H4 type, followed by the packet). Returns the total length or
 * a negative value on error. Only valid after hci_spi_transport_init(). */
int hci_spi_read_frame(uint8_t *buf, uint16_t buf_size);

#endif /* HCI_SPI_H */
