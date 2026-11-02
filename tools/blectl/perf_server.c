/* Desktop peripheral for comparing the same GATT bulk service over USB HCI. */
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include "host/ble_hs.h"
#include "host/ble_hs_mbuf.h"
#include "services/gap/ble_svc_gap.h"
#include "gatt_perf.h"
#include "perf_server.h"

uint32_t gatt_perf_time_us(void)
{
    struct timespec timestamp;
    clock_gettime(CLOCK_MONOTONIC, &timestamp);
    return (uint32_t)((uint64_t)timestamp.tv_sec * 1000000 + timestamp.tv_nsec / 1000);
}

static volatile sig_atomic_t interrupted;
static volatile sig_atomic_t failed;
static uint32_t started_at;
static uint8_t echo[64];
static uint16_t echo_length;
static uint8_t requested_phy, phy_status;
static uint16_t uptime_handle, echo_handle, phy_handle;
static const ble_uuid128_t service_uuid = BLE_UUID128_INIT(
    0x01,0x3e,0xf1,0xa2,0xc4,0xd1,0xa2,0xb7,0x8e,0x4e,0x4d,0x6d,0x0e,0x48,0x02,0x7a);
static const ble_uuid128_t uptime_uuid = BLE_UUID128_INIT(
    0x02,0x3e,0xf1,0xa2,0xc4,0xd1,0xa2,0xb7,0x8e,0x4e,0x4d,0x6d,0x0e,0x48,0x02,0x7a);
static const ble_uuid128_t echo_uuid = BLE_UUID128_INIT(
    0x03,0x3e,0xf1,0xa2,0xc4,0xd1,0xa2,0xb7,0x8e,0x4e,0x4d,0x6d,0x0e,0x48,0x02,0x7a);
static const ble_uuid128_t phy_uuid = BLE_UUID128_INIT(
    0x04,0x3e,0xf1,0xa2,0xc4,0xd1,0xa2,0xb7,0x8e,0x4e,0x4d,0x6d,0x0e,0x48,0x02,0x7a);

static uint8_t hci_status(int rc)
{
    return rc >= BLE_HS_ERR_HCI_BASE && rc < BLE_HS_ERR_HCI_BASE + 256 ?
           rc - BLE_HS_ERR_HCI_BASE : 0xfe;
}

static int diagnostic_access(uint16_t conn, uint16_t attr,
                             struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)arg;
    uint8_t data[6] = {1,1,1,0,phy_status,requested_phy};
    uint16_t length;
    int rc;
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        if (attr == uptime_handle) {
            uint32_t seconds = (ble_npl_time_ticks_to_ms32(ble_npl_time_get()) - started_at) / 1000;
            for (unsigned i = 0; i < 4; i++) data[i] = seconds >> (8*i);
            rc = os_mbuf_append(ctxt->om, data, 4);
        } else if (attr == echo_handle) {
            rc = os_mbuf_append(ctxt->om, echo, echo_length);
        } else {
            rc = ble_gap_read_le_phy(conn, data + 1, data + 2);
            if (rc != 0) {
                data[1] = data[2] = 1; /* LE default; actual command error retained. */
                data[4] = phy_status ? phy_status : hci_status(rc);
            }
            rc = os_mbuf_append(ctxt->om, data, sizeof(data));
        }
        return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) return BLE_ATT_ERR_UNLIKELY;
    if (attr == echo_handle) {
        return ble_hs_mbuf_to_flat(ctxt->om, echo, sizeof(echo), &echo_length) == 0 ?
               0 : BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }
    if (attr != phy_handle || ble_hs_mbuf_to_flat(ctxt->om, data, 1, &length) != 0 ||
        length != 1 || data[0] < 1 || data[0] > 4) return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    requested_phy = data[0];
    uint8_t mask = data[0] == 1 ? BLE_GAP_LE_PHY_1M_MASK :
                   data[0] == 2 ? BLE_GAP_LE_PHY_2M_MASK : BLE_GAP_LE_PHY_CODED_MASK;
    rc = ble_gap_set_prefered_le_phy(conn, mask, mask, BLE_GAP_LE_PHY_CODED_ANY);
    phy_status = rc == 0 ? 0 : hci_status(rc);
    return 0;
}

static const struct ble_gatt_svc_def services[] = {{
    .type = BLE_GATT_SVC_TYPE_PRIMARY,
    .uuid = &service_uuid.u,
    .characteristics = (struct ble_gatt_chr_def[]) {
        {.uuid=&uptime_uuid.u, .access_cb=diagnostic_access,
         .flags=BLE_GATT_CHR_F_READ, .val_handle=&uptime_handle},
        {.uuid=&echo_uuid.u, .access_cb=diagnostic_access,
         .flags=BLE_GATT_CHR_F_READ|BLE_GATT_CHR_F_WRITE, .val_handle=&echo_handle},
        {.uuid=&phy_uuid.u, .access_cb=diagnostic_access,
         .flags=BLE_GATT_CHR_F_READ|BLE_GATT_CHR_F_WRITE, .val_handle=&phy_handle},
        {.uuid=&gatt_perf_control_uuid.u, .access_cb=gatt_perf_access,
         .flags=BLE_GATT_CHR_F_READ|BLE_GATT_CHR_F_WRITE, .val_handle=&gatt_perf_control_handle},
        {.uuid=&gatt_perf_data_uuid.u, .access_cb=gatt_perf_access,
         .flags=BLE_GATT_CHR_F_WRITE_NO_RSP|BLE_GATT_CHR_F_NOTIFY, .val_handle=&gatt_perf_data_handle},
        {0}
    }
}, {0}};

static int advertise(void);
static int gap_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    gatt_perf_gap_event(event);
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            printf("perf-server: connected handle=%u\n", event->connect.conn_handle);
            requested_phy = phy_status = 0;
            int rc = ble_gattc_exchange_mtu(event->connect.conn_handle, NULL, NULL);
            printf("perf-server: MTU exchange rc=%d\n", rc);
        } else advertise();
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        printf("perf-server: disconnected reason=%d\n", event->disconnect.reason);
        advertise();
        break;
    case BLE_GAP_EVENT_MTU:
        printf("perf-server: MTU=%u\n", event->mtu.value);
        break;
    case BLE_GAP_EVENT_ADV_COMPLETE:
        advertise();
        break;
    case BLE_GAP_EVENT_PHY_UPDATE_COMPLETE:
        phy_status = event->phy_updated.status;
        break;
    default:
        break;
    }
    fflush(stdout);
    return 0;
}

static int advertise(void)
{
    struct ble_hs_adv_fields fields = {0};
    const char *name = ble_svc_gap_device_name();
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.name = (const uint8_t *)name;
    fields.name_len = strlen(name);
    fields.name_is_complete = 1;
    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) return rc;
    memset(&fields, 0, sizeof(fields));
    fields.name = (const uint8_t *)name;
    fields.name_len = strlen(name);
    fields.name_is_complete = 1;
    rc = ble_gap_adv_rsp_set_fields(&fields);
    if (rc != 0) return rc;
    struct ble_gap_adv_params params = {
        .conn_mode=BLE_GAP_CONN_MODE_UND, .disc_mode=BLE_GAP_DISC_MODE_GEN,
        .itvl_min=32, .itvl_max=48
    };
    rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER, &params, gap_event, NULL);
    if (rc != 0) {
        fprintf(stderr, "perf-server: advertise rc=%d\n", rc);
        failed = interrupted = 1;
    }
    return rc;
}

int perf_server_init(void)
{
    gatt_perf_init();
    ble_att_set_preferred_mtu(247);
    started_at = ble_npl_time_ticks_to_ms32(ble_npl_time_get());
    int rc = ble_gatts_count_cfg(services);
    if (rc == 0) rc = ble_gatts_add_svcs(services);
    if (rc == 0) rc = ble_svc_gap_device_name_set("N32-Bridge-Perf");
    return rc;
}

void perf_server_on_sync(void)
{
    int rc = advertise();
    if (rc != 0) failed = interrupted = 1;
}

static void stop_signal(int signal_number)
{
    (void)signal_number;
    interrupted = 1;
}

int perf_server_run(void)
{
    signal(SIGINT, stop_signal);
    signal(SIGTERM, stop_signal);
    printf("perf-server: advertising N32-Bridge-Perf; Ctrl-C to stop\n");
    fflush(stdout);
    while (!interrupted) usleep(100000);
    return failed ? 1 : 0;
}
