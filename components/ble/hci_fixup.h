/*
 * Workarounds for incomplete HCI feature reports of the NZ8801 controller.
 * Applied to EVT Command Complete frames on their way to the host.
 */
#ifndef HCI_FIXUP_H
#define HCI_FIXUP_H

#include <stdint.h>

/* evbuf layout: [0]=event code, [1]=param_len, [2..]=params. */
void hci_fixup_evt(uint8_t *evbuf, uint8_t param_len);

#endif /* HCI_FIXUP_H */
