#include <assert.h>
#include <string.h>
#include "hci_fixup.h"

int main(void)
{
    struct hci_link_stats stats;
    assert(!hci_link_stats_get(0, &stats));
    assert(!hci_link_stats_get(UINT16_MAX, &stats));

    /* LE Data Length Change: handle 7, TX 251/2120 us, RX 100/900 us. */
    uint8_t dle[] = {0x3e, 11, 7, 7, 0, 251, 0, 0x48, 8, 100, 0, 0x84, 3};
    hci_fixup_evt(dle, 10);
    assert(!hci_link_stats_get(7, &stats));
    hci_fixup_evt(dle, 11);
    assert(hci_link_stats_get(7, &stats));
    assert(stats.tx_octets == 251 && stats.tx_time_us == 2120);
    assert(stats.rx_octets == 100 && stats.rx_time_us == 900);
    assert(!hci_link_stats_get(8, &stats));

    uint8_t disconnect[] = {5, 4, 0, 8, 0, 0x13};
    hci_fixup_evt(disconnect, 4);
    assert(hci_link_stats_get(7, &stats));
    disconnect[3] = 7;
    hci_fixup_evt(disconnect, 4);
    assert(!hci_link_stats_get(7, &stats));

    hci_fixup_evt(dle, 11);
    uint8_t reset[] = {0x0e, 4, 1, 3, 0x0c, 0};
    hci_fixup_evt(reset, 4);
    assert(!hci_link_stats_get(7, &stats));
    hci_fixup_evt(dle, 11);
    uint8_t connected[] = {0x3e, 4, 1, 0, 7, 0};
    hci_fixup_evt(connected, 4);
    assert(!hci_link_stats_get(7, &stats));

    /* Truncated feature completions must not modify bytes beyond parameters. */
    uint8_t features[14] = {0x0e, 12, 1, 3, 0x10, 0};
    uint8_t original[14];
    memcpy(original, features, sizeof(features));
    hci_fixup_evt(features, 9);
    assert(memcmp(features, original, sizeof(features)) == 0);
    hci_fixup_evt(features, 12);
    assert(features[10] == 0x60);
    return 0;
}
