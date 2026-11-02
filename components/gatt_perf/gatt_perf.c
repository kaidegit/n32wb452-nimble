/* One-way GATT benchmark shared by the embedded and desktop NimBLE hosts.
 * All mutable benchmark state belongs to the host event queue. Bulk transfers
 * have no application acknowledgments; the receiver checks every byte. */
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "nimble/nimble_port.h"
#include "host/ble_hs.h"
#include "host/ble_hs_mbuf.h"
#include "os/os_mbuf.h"
#include "gatt_perf.h"
#include "hci_fixup.h"

#define MAX_PAYLOAD 244
#define TX_RESERVE_MBUFS 5
#define PUMP_BUDGET 8

uint16_t gatt_perf_control_handle;
uint16_t gatt_perf_data_handle;
const ble_uuid128_t gatt_perf_control_uuid =
    BLE_UUID128_INIT(0x05,0x3e,0xf1,0xa2,0xc4,0xd1,0xa2,0xb7,
                     0x8e,0x4e,0x4d,0x6d,0x0e,0x48,0x02,0x7a);
const ble_uuid128_t gatt_perf_data_uuid =
    BLE_UUID128_INIT(0x06,0x3e,0xf1,0xa2,0xc4,0xd1,0xa2,0xb7,
                     0x8e,0x4e,0x4d,0x6d,0x0e,0x48,0x02,0x7a);

static struct ble_npl_callout tx_pump;
static uint16_t connection, payload_size;
static uint8_t direction;
static bool running, done, subscribed, dle_requested;
static int32_t dle_request_result;
static uint32_t bytes, packets, errors, allocation_retries;
static uint32_t started_us, last_data_us, duration_ms, blocked_cycles, min_free;

static uint32_t get_u32(const uint8_t *data)
{
    return (uint32_t)data[0] | (uint32_t)data[1] << 8 |
           (uint32_t)data[2] << 16 | (uint32_t)data[3] << 24;
}

static void put_u32(uint8_t *data, uint32_t value)
{
    for (unsigned i = 0; i < 4; i++) data[i] = value >> (8 * i);
}

static uint8_t payload_byte(uint32_t sequence, unsigned offset)
{
    return (uint8_t)(sequence * 131u + offset * 17u);
}

static void stop_transfer(void)
{
    running = false;
    done = true;
    ble_npl_callout_stop(&tx_pump);
}

static void receive_payload(struct os_mbuf *om)
{
    uint8_t payload[MAX_PAYLOAD];
    uint16_t length = OS_MBUF_PKTLEN(om);
    bool valid = length == payload_size && length <= sizeof(payload);
    if (valid) valid = os_mbuf_copydata(om, 0, length, payload) == 0;
    if (valid) {
        valid = get_u32(payload) == packets;
        for (unsigned i = 4; i < length; i++) {
            if (payload[i] != payload_byte(packets, i)) valid = false;
        }
    }
    if (!valid) errors++;
    bytes += length;
    packets++;
    last_data_us = gatt_perf_time_us();
}

static void send_payloads(struct ble_npl_event *event)
{
    (void)event;
    if (!running || direction != 2) return;
    if ((uint32_t)(gatt_perf_time_us() - started_us) / 1000 >= duration_ms) {
        stop_transfer();
        return;
    }
    unsigned free_count = os_msys_num_free();
    if (free_count < min_free) min_free = free_count;
    if (free_count < TX_RESERVE_MBUFS) blocked_cycles++;

    for (unsigned i = 0; i < PUMP_BUDGET; i++) {
        /* Keep buffers available for incoming ATT control and HCI traffic. */
        if (os_msys_num_free() < TX_RESERVE_MBUFS) break;
        uint8_t payload[MAX_PAYLOAD];
        put_u32(payload, packets);
        for (unsigned j = 4; j < payload_size; j++) {
            payload[j] = payload_byte(packets, j);
        }
        struct os_mbuf *om = ble_hs_mbuf_from_flat(payload, payload_size);
        if (!om) {
            allocation_retries++;
            break;
        }
        free_count = os_msys_num_free();
        if (free_count < min_free) min_free = free_count;
        /* notify_custom consumes om on both success and failure. */
        int rc = ble_gatts_notify_custom(connection, gatt_perf_data_handle, om);
        if (rc != 0) {
            errors++;
            stop_transfer();
            return;
        }
        bytes += payload_size;
        packets++;
        last_data_us = gatt_perf_time_us();
    }
    if (running) ble_npl_callout_reset(&tx_pump, 1);
}

void gatt_perf_init(void)
{
    ble_npl_callout_init(&tx_pump, nimble_port_get_dflt_eventq(), send_payloads, NULL);
}

int gatt_perf_access(uint16_t conn_handle, uint16_t attr_handle,
                     struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)arg;
    if (attr_handle == gatt_perf_data_handle) {
        if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR || !running ||
            direction != 1 || conn_handle != connection) return BLE_ATT_ERR_UNLIKELY;
        receive_payload(ctxt->om);
        return 0;
    }
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        /* Version 5 reports host DLE request diagnostics at offsets 32/36 and
         * precise elapsed microseconds at 96; reserved bytes remain zero. */
        uint8_t status[100] = {5, 1};
        struct ble_gap_conn_desc desc;
        struct hci_link_stats link;
        status[2] = direction;
        status[3] = running;
        status[4] = done;
        status[5] = payload_size;
        status[6] = payload_size >> 8;
        status[7] = ble_att_mtu(conn_handle);
        put_u32(status + 8, bytes);
        put_u32(status + 12, packets);
        put_u32(status + 16, errors);
        put_u32(status + 20, (uint32_t)(last_data_us - started_us) / 1000);
        put_u32(status + 96, last_data_us - started_us);
        put_u32(status + 24, allocation_retries);
        put_u32(status + 28, os_msys_num_free());
        put_u32(status + 32, (uint32_t)dle_request_result);
        status[36] = dle_requested;
        put_u32(status + 80, blocked_cycles);
        put_u32(status + 84, min_free);
        if (hci_link_stats_get(conn_handle, &link)) {
            status[88] = link.tx_octets;
            status[89] = link.rx_octets;
            status[90] = link.tx_time_us;
            status[91] = link.tx_time_us >> 8;
            status[92] = link.rx_time_us;
            status[93] = link.rx_time_us >> 8;
        }
        if (ble_gap_conn_find(conn_handle, &desc) == 0) {
            status[94] = desc.conn_itvl;
            status[95] = desc.conn_itvl >> 8;
        }
        return os_mbuf_append(ctxt->om, status, sizeof(status)) == 0 ?
               0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) return BLE_ATT_ERR_UNLIKELY;
    uint8_t command[12];
    uint16_t length;
    if (ble_hs_mbuf_to_flat(ctxt->om, command, sizeof(command), &length) != 0 ||
        length != sizeof(command) || command[0] != 1 || command[1] != 1) {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }
    switch (command[3]) {
    case 0:
        if (running && conn_handle != connection) return BLE_ATT_ERR_UNLIKELY;
        stop_transfer();
        return 0;
    case 3: {
        if (running) return BLE_ATT_ERR_UNLIKELY;
        /* Separate from the CI request: concurrent LL procedures can collide.
         * A successful HCI request is not proof of negotiated DLE; the status
         * record only reports the later LE Data Length Change event. */
        int rc = ble_gap_set_data_len(conn_handle, 251, 2120);
        dle_requested = true;
        dle_request_result = rc;
        printf("gatt-perf: request DLE 251/2120 us rc=%d\n", rc);
        return rc == 0 ? 0 : BLE_ATT_ERR_UNLIKELY;
    }
    case 2: {
        struct ble_gap_upd_params params = {
            .itvl_min = 6, .itvl_max = 12, .supervision_timeout = 200
        };
        /* Central chooses the final interval; status reports the actual value. */
        ble_gap_update_params(conn_handle, &params);
        return 0;
    }
    case 1:
        break;
    default:
        return BLE_ATT_ERR_UNLIKELY;
    }
    uint16_t size = (uint16_t)command[4] | (uint16_t)command[5] << 8;
    uint32_t duration = get_u32(command + 8);
    if (running || command[2] < 1 || command[2] > 2 || size < 4 ||
        size > MAX_PAYLOAD || size > ble_att_mtu(conn_handle) - 3 ||
        duration == 0 || duration > 3600000 || (command[2] == 2 && !subscribed)) {
        return BLE_ATT_ERR_UNLIKELY;
    }
    connection = conn_handle;
    direction = command[2];
    payload_size = size;
    duration_ms = duration;
    bytes = packets = errors = allocation_retries = blocked_cycles = 0;
    min_free = os_msys_num_free();
    done = false;
    running = true;
    started_us = last_data_us = gatt_perf_time_us();
    if (direction == 2) ble_npl_callout_reset(&tx_pump, 1);
    return 0;
}

void gatt_perf_gap_event(struct ble_gap_event *event)
{
    if (event->type == BLE_GAP_EVENT_SUBSCRIBE &&
        event->subscribe.attr_handle == gatt_perf_data_handle) {
        subscribed = event->subscribe.cur_notify;
        if (!subscribed && running && direction == 2) stop_transfer();
    } else if (event->type == BLE_GAP_EVENT_DISCONNECT) {
        stop_transfer();
        subscribed = false;
        dle_requested = false;
        dle_request_result = 0;
    }
}
