/*
 * Checked SPI TX to the NZ8801: waits for the ready line (PD6) before
 * asserting CS, yields to a pending RX, and preserves the vendor CS /
 * byte / delay ordering. The vendor eif_spi_send_bytes() busy-waits and
 * starves the lower-priority RX task, so runtime HCI traffic goes through
 * this path instead. Firmware download (Interface.c) still uses the
 * vendor routine.
 */
#ifndef BLE_SPI_TX_H
#define BLE_SPI_TX_H

#include <stdint.h>

/* 0 on success, -1 on ready-timeout (frame NOT sent). */
int ble_spi_send_checked(const uint8_t *data, uint16_t len);

#endif /* BLE_SPI_TX_H */
