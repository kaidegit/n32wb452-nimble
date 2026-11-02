/*
 * NZ8801 feature-report workarounds (verified against silicon):
 *
 * 1) Read_Local_Supported_Features (0x1003)
 *    The NZ8801 is a BLE-only controller and reports all-zero BR/EDR LMP
 *    features. Inject the "LE Supported (Controller)" bits so NimBLE does
 *    not reject the controller as LE-incapable.
 *
 * 2) LE_Read_Local_Supported_Features (0x2003)
 *    Silicon reports features = 0x01 only, but bench testing showed DLE
 *    commands work (0x202F returns 251/2120, 0x2022 succeeds and raises a
 *    Data Length Change event). Inject the Data Length Extension bit.
 *
 * Both injections are idempotent, so running them on both sides of the USB
 * bridge is harmless.
 */
#include "hci_fixup.h"
#include <stdatomic.h>

#define HCI_EVT_CMD_COMPLETE        0x0E
#define HCI_OP_RD_LOC_SUPP_FEAT     0x1003U
#define HCI_OP_LE_RD_LOC_SUPP_FEAT  0x2003U

#define HCI_LMP_FEAT_LE_SUPPORTED   0x60U  /* features[4] bit5 | bit6 */
#define HCI_LE_FEAT_DATA_LEN_EXT    0x20U  /* le_features[0] bit5 */

/* One active test connection. RX can run on a separate thread from the host.
 * A bounded snapshot avoids waiting on a preempted writer on the MCU. */
static _Atomic uint32_t s_link_version;
static _Atomic uint32_t s_link_handle = UINT16_MAX;
static _Atomic uint32_t s_link_tx;
static _Atomic uint32_t s_link_rx;

static uint16_t read_le16(const uint8_t *data)
{
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static void observe_link_event(const uint8_t *event, uint8_t len)
{
    if (event[0] == 0x3e && len >= 11 && event[2] == 0x07) {
        atomic_fetch_add(&s_link_version, 1);
        atomic_store(&s_link_handle, read_le16(event + 3));
        atomic_store(&s_link_tx, (uint32_t)read_le16(event + 5) |
                     ((uint32_t)read_le16(event + 7) << 16));
        atomic_store(&s_link_rx, (uint32_t)read_le16(event + 9) |
                     ((uint32_t)read_le16(event + 11) << 16));
        atomic_fetch_add(&s_link_version, 1);
    } else if ((event[0] == 0x3e && len >= 4 &&
                (event[2] == 0x01 || event[2] == 0x0a) && event[3] == 0) ||
               (event[0] == 0x05 && len >= 4 && event[2] == 0 &&
                read_le16(event + 3) == atomic_load(&s_link_handle)) ||
               (event[0] == 0x0e && len >= 4 && event[5] == 0 &&
                read_le16(event + 3) == 0x0c03)) {
        atomic_store(&s_link_handle, UINT16_MAX);
    }
}

int hci_link_stats_get(uint16_t conn_handle, struct hci_link_stats *out)
{
    uint32_t version = atomic_load(&s_link_version);
    uint32_t handle = atomic_load(&s_link_handle);
    uint32_t tx = atomic_load(&s_link_tx);
    uint32_t rx = atomic_load(&s_link_rx);
    if (!out || handle == UINT16_MAX || (version & 1) || version != atomic_load(&s_link_version) ||
        handle != conn_handle || atomic_load(&s_link_handle) != handle) {
        return 0;
    }
    out->tx_octets = (uint16_t)tx;
    out->tx_time_us = (uint16_t)(tx >> 16);
    out->rx_octets = (uint16_t)rx;
    out->rx_time_us = (uint16_t)(rx >> 16);
    return 1;
}

void hci_fixup_evt(uint8_t *evbuf, uint8_t param_len)
{
    uint16_t op;

    observe_link_event(evbuf, param_len);
    if (evbuf[0] != HCI_EVT_CMD_COMPLETE || param_len < 12) {
        return;
    }

    op = (uint16_t)evbuf[3] | ((uint16_t)evbuf[4] << 8);
    if (evbuf[5] != 0x00) {
        return;  /* only patch successful completions */
    }

    if (op == HCI_OP_RD_LOC_SUPP_FEAT) {
        /* return params: status + features[8]; features[4] = evbuf[10] */
        evbuf[10] |= HCI_LMP_FEAT_LE_SUPPORTED;
    } else if (op == HCI_OP_LE_RD_LOC_SUPP_FEAT) {
        /* return params: status + le_features[8]; features[0] = evbuf[6] */
        evbuf[6] |= HCI_LE_FEAT_DATA_LEN_EXT;
    }
}
