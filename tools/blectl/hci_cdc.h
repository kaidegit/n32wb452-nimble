/*
 * NimBLE custom HCI transport over a USB CDC ACM serial device (H4 byte
 * stream). The N32WB452 bridge firmware forwards every byte untouched
 * between this serial port and the NZ8801 controller.
 */
#ifndef BLECTL_HCI_CDC_H
#define BLECTL_HCI_CDC_H

#include <stdint.h>

/* Open the serial device and start the RX reader thread.
 * Returns 0 on success. */
int hci_cdc_open(const char *dev_path);

/* Close and join the reader thread. */
void hci_cdc_close(void);

const char *hci_cdc_port(void);

/* Autodetect: first /dev/cu.usbmodem* device. Returns NULL if none. */
const char *hci_cdc_autodetect_port(void);

#endif /* BLECTL_HCI_CDC_H */
