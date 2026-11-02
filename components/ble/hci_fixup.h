/*
 * Workarounds for incomplete HCI feature reports of the NZ8801 controller.
 * Applied to EVT Command Complete frames on their way to the host.
 */
#ifndef HCI_FIXUP_H
#define HCI_FIXUP_H

#include <stdint.h>

/* evbuf layout: [0]=event code, [1]=param_len, [2..]=params. */
void hci_fixup_evt(uint8_t *evbuf, uint8_t param_len);

/* Effective values observed in LE Data Length Change, never inferred from MTU.
 * Returns 0 until the controller reports values for this connection. */
struct hci_link_stats {
    uint16_t tx_octets;
    uint16_t tx_time_us;
    uint16_t rx_octets;
    uint16_t rx_time_us;
};
int hci_link_stats_get(uint16_t conn_handle, struct hci_link_stats *out);

#endif /* HCI_FIXUP_H */
