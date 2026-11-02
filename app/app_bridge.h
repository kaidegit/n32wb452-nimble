#if defined(APP_MODE_BRIDGE)
#ifndef APP_BRIDGE_H
#define APP_BRIDGE_H

/* Called by the SPI RX task (hci_spi.c) with a complete H4 frame from the
 * controller: buf[0]=H4 type. Bridge forwards it to the USB host. */
void bridge_handle_controller_frame(const uint8_t *frame, uint16_t len);

void app_bridge_start(void);

void bridge_spi_lock(void);
void bridge_spi_unlock(void);

#endif /* APP_BRIDGE_H */

#endif /* APP_MODE_BRIDGE */
