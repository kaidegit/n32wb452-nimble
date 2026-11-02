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

#define HCI_EVT_CMD_COMPLETE        0x0E
#define HCI_OP_RD_LOC_SUPP_FEAT     0x1003U
#define HCI_OP_LE_RD_LOC_SUPP_FEAT  0x2003U

#define HCI_LMP_FEAT_LE_SUPPORTED   0x60U  /* features[4] bit5 | bit6 */
#define HCI_LE_FEAT_DATA_LEN_EXT    0x20U  /* le_features[0] bit5 */

void hci_fixup_evt(uint8_t *evbuf, uint8_t param_len)
{
    uint16_t op;

    if (evbuf[0] != HCI_EVT_CMD_COMPLETE || param_len < 9) {
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
