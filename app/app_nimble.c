/*
 * NimBLE-host mode: on-chip host talking to the NZ8801 controller over the
 * SPI HCI transport. Two services:
 *
 *   Service     6E400001-B5A3-F393-E0A9-E50E24DCCA9E (NUS-compatible)
 *   RX char     6E400002-...  write / write-no-rsp  → echoed back
 *   TX char     6E400003-...  notify                ← echo + notifications
 *
 *   Service     7A02480E-6D4D-4E8E-B7A2-D1C4A2F13E01 (test/diagnostics)
 *   uptime      ...3E02  read    4-byte LE seconds since boot
 *   echo        ...3E03  r/w     up to 64 bytes, write stores / read returns
 *   phy         ...3E04  r/w     write 1=1M 2=2M 3/4=coded; read 6-byte status
 *
 * The test service matches the shared macOS/Android BLE clients under tools/.
 */
#if defined(APP_MODE_NIMBLE)

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"

#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_hs_mbuf.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "os/os_mbuf.h"

#include "log.h"
#include "app_nimble.h"
#include "hci_spi.h"

#define TAG "ble.app"

/*
 * Private NimBLE HCI API: vendor commands MUST go through ble_hs_hci_cmd_tx
 * rather than ble_transport_to_ll_cmd(); bypassing the host's HCI command
 * tracking pollutes the ACK queue and breaks the next command with
 * BLE_HS_ECONTROLLER.
 */
extern int ble_hs_hci_cmd_tx(uint16_t opcode, const void *cmd, uint8_t cmd_len,
                             void *rsp, uint8_t rsp_len);
/* Refresh the host-cached public BD_ADDR after the vendor command. */
extern void ble_hs_id_set_pub(const uint8_t *pub_addr);

static uint16_t g_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static uint16_t g_tx_attr_handle;

/* ------------------------------------------------------------------ GATT */

/* NUS UUIDs */
static const ble_uuid128_t g_nus_svc_uuid =
    BLE_UUID128_INIT(0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0,
                     0x93, 0xF3, 0xA3, 0xB5, 0x01, 0x00, 0x40, 0x6E);
static const ble_uuid128_t g_nus_rx_uuid =
    BLE_UUID128_INIT(0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0,
                     0x93, 0xF3, 0xA3, 0xB5, 0x02, 0x00, 0x40, 0x6E);
static const ble_uuid128_t g_nus_tx_uuid =
    BLE_UUID128_INIT(0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0,
                     0x93, 0xF3, 0xA3, 0xB5, 0x03, 0x00, 0x40, 0x6E);

static int nus_rx_write(uint16_t conn_handle, uint16_t attr_handle,
                        struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn_handle;
    (void)attr_handle;
    (void)arg;

    struct os_mbuf *om = ctxt->om;
    uint16_t len = OS_MBUF_PKTLEN(om);
    if (len > 200) {
        len = 200;
    }

    uint8_t echo[200];
    os_mbuf_copydata(om, 0, len, echo);

    LOGI(TAG, "rx %u bytes, echoing", len);

    if (g_conn_handle != BLE_HS_CONN_HANDLE_NONE && g_tx_attr_handle) {
        struct os_mbuf *tx = ble_hs_mbuf_from_flat(echo, len);
        if (tx) {
            int rc = ble_gatts_notify_custom(g_conn_handle, g_tx_attr_handle, tx);
            if (rc != 0) {
                LOGW(TAG, "notify rc=%d", rc);
            }
        }
    }
    return 0;
}

/* NimBLE requires an access callback even for a notify-only value. */
static int nus_tx_access(uint16_t conn_handle, uint16_t attr_handle,
                         struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn_handle;
    (void)attr_handle;
    (void)ctxt;
    (void)arg;
    return BLE_ATT_ERR_READ_NOT_PERMITTED;
}

/* --------------------------------------------------- test/diagnostics svc */

/* 7A02480E-6D4D-4E8E-B7A2-D1C4A2F13E0x, same layout the shared clients
 * under tools/ discover and verify. */
static const ble_uuid128_t g_tst_svc_uuid =
    BLE_UUID128_INIT(0x01, 0x3E, 0xF1, 0xA2, 0xC4, 0xD1, 0xA2, 0xB7,
                     0x8E, 0x4E, 0x4D, 0x6D, 0x0E, 0x48, 0x02, 0x7A);
static const ble_uuid128_t g_tst_uptime_uuid =
    BLE_UUID128_INIT(0x02, 0x3E, 0xF1, 0xA2, 0xC4, 0xD1, 0xA2, 0xB7,
                     0x8E, 0x4E, 0x4D, 0x6D, 0x0E, 0x48, 0x02, 0x7A);
static const ble_uuid128_t g_tst_echo_uuid =
    BLE_UUID128_INIT(0x03, 0x3E, 0xF1, 0xA2, 0xC4, 0xD1, 0xA2, 0xB7,
                     0x8E, 0x4E, 0x4D, 0x6D, 0x0E, 0x48, 0x02, 0x7A);
static const ble_uuid128_t g_tst_phy_uuid =
    BLE_UUID128_INIT(0x04, 0x3E, 0xF1, 0xA2, 0xC4, 0xD1, 0xA2, 0xB7,
                     0x8E, 0x4E, 0x4D, 0x6D, 0x0E, 0x48, 0x02, 0x7A);

static uint16_t g_tst_uptime_handle;
static uint16_t g_tst_echo_handle;
static uint16_t g_tst_phy_handle;

#define TST_ECHO_BUF_LEN 64
static uint8_t g_tst_echo_buf[TST_ECHO_BUF_LEN];
static uint16_t g_tst_echo_len;
static uint8_t g_phy_pending, g_phy_status, g_phy_requested;

/* Raw HCI status byte for the 3E04 read: host errors outside the HCI range
 * map to 0xFE so tool-side diagnostics can tell them apart. */
#define PHY_HCI_STATUS(rc) \
    (((rc) >= BLE_HS_ERR_HCI_BASE && \
      (rc) < (BLE_HS_ERR_HCI_BASE + 0x100)) ? \
     (uint8_t)((rc) - BLE_HS_ERR_HCI_BASE) : 0xFE)

static int tst_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                         struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    uint8_t phy[6] = { 1, 0, 0, 0, 0, 0 };
    uint32_t uptime;
    uint16_t len;
    int rc;

    (void)arg;

    switch (ctxt->op) {
    case BLE_GATT_ACCESS_OP_READ_CHR:
        if (attr_handle == g_tst_phy_handle) {
            rc = ble_gap_read_le_phy(conn_handle, &phy[1], &phy[2]);
            if (rc != 0) {
                /* The NZ8801 rejects the LE Read PHY command. LE mandates
                 * 1M as the default PHY, and no PHY update can succeed on
                 * this controller, so report the link default and surface
                 * the refusal in the status byte instead of failing the
                 * ATT read with an opaque error. */
                LOGW(TAG, "phy read hci rc=%d status=0x%02X", rc,
                     g_phy_status ? g_phy_status : PHY_HCI_STATUS(rc));
                phy[1] = phy[2] = 1;
                if (g_phy_status == 0) {
                    g_phy_status = PHY_HCI_STATUS(rc);
                }
            }
            phy[3] = g_phy_pending;
            phy[4] = g_phy_status;
            phy[5] = g_phy_requested;
            return os_mbuf_append(ctxt->om, phy, sizeof(phy)) == 0 ?
                   0 : BLE_ATT_ERR_INSUFFICIENT_RES;
        }
        if (attr_handle == g_tst_uptime_handle) {
            uptime = (uint32_t)(xTaskGetTickCount() / configTICK_RATE_HZ);
            return os_mbuf_append(ctxt->om, &uptime, sizeof(uptime)) == 0 ?
                   0 : BLE_ATT_ERR_INSUFFICIENT_RES;
        }
        if (attr_handle == g_tst_echo_handle) {
            return os_mbuf_append(ctxt->om, g_tst_echo_buf,
                                  g_tst_echo_len) == 0 ?
                   0 : BLE_ATT_ERR_INSUFFICIENT_RES;
        }
        return BLE_ATT_ERR_READ_NOT_PERMITTED;

    case BLE_GATT_ACCESS_OP_WRITE_CHR:
        if (attr_handle == g_tst_phy_handle) {
            uint8_t mask;
            uint16_t opts;

            rc = ble_hs_mbuf_to_flat(ctxt->om, phy, 1, &len);
            if (rc != 0 || len != 1 || phy[0] < 1 || phy[0] > 4) {
                return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
            }
            if (g_phy_pending) {
                return BLE_ATT_ERR_UNLIKELY;
            }
            switch (phy[0]) {
            case 2:
                mask = BLE_GAP_LE_PHY_2M_MASK;
                opts = BLE_GAP_LE_PHY_CODED_ANY;
                break;
            case 3:
                mask = BLE_GAP_LE_PHY_CODED_MASK;
                opts = BLE_GAP_LE_PHY_CODED_S2;
                break;
            case 4:
                mask = BLE_GAP_LE_PHY_CODED_MASK;
                opts = BLE_GAP_LE_PHY_CODED_S8;
                break;
            default:
                mask = BLE_GAP_LE_PHY_1M_MASK;
                opts = BLE_GAP_LE_PHY_CODED_ANY;
                break;
            }
            rc = ble_gap_set_prefered_le_phy(conn_handle, mask, mask, opts);
            if (rc != 0) {
                /* Accept the write at the ATT layer and report the
                 * controller's refusal through the status byte; failing
                 * the ATT write would hide the actual HCI error. */
                LOGW(TAG, "phy req: conn=%u sel=%u rc=%d hci=0x%02X",
                     conn_handle, phy[0], rc, PHY_HCI_STATUS(rc));
                g_phy_requested = phy[0];
                g_phy_status = PHY_HCI_STATUS(rc);
                g_phy_pending = 0;
                return 0;
            }
            g_phy_requested = phy[0];
            g_phy_pending = 1;
            g_phy_status = 0xff;
            return 0;
        }
        if (attr_handle == g_tst_echo_handle) {
            rc = ble_hs_mbuf_to_flat(ctxt->om, g_tst_echo_buf,
                                     sizeof(g_tst_echo_buf), &g_tst_echo_len);
            if (rc != 0) {
                return BLE_ATT_ERR_UNLIKELY;
            }
            return 0;
        }
        return BLE_ATT_ERR_WRITE_NOT_PERMITTED;

    default:
        return BLE_ATT_ERR_UNLIKELY;
    }
}

static const struct ble_gatt_svc_def g_gatt_services[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &g_nus_svc_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]){
            {
                .uuid = &g_nus_rx_uuid.u,
                .access_cb = nus_rx_write,
                .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
            },
            {
                .uuid = &g_nus_tx_uuid.u,
                .access_cb = nus_tx_access,
                .flags = BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &g_tx_attr_handle,
            },
            { 0 }
        },
    },
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &g_tst_svc_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]){
            {
                .uuid = &g_tst_uptime_uuid.u,
                .access_cb = tst_access_cb,
                .flags = BLE_GATT_CHR_F_READ,
                .val_handle = &g_tst_uptime_handle,
            },
            {
                .uuid = &g_tst_echo_uuid.u,
                .access_cb = tst_access_cb,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE,
                .val_handle = &g_tst_echo_handle,
            },
            {
                .uuid = &g_tst_phy_uuid.u,
                .access_cb = tst_access_cb,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE,
                .val_handle = &g_tst_phy_handle,
            },
            { 0 }
        },
    },
    { 0 }
};

/* ------------------------------------------------------------- advertise */

static void start_advertise(void);

static int gap_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;

    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            g_conn_handle = event->connect.conn_handle;
            g_phy_pending = g_phy_status = g_phy_requested = 0;
            LOGI(TAG, "connected: conn=%u", g_conn_handle);
            /* Peripheral may initiate the MTU exchange as well. */
            ble_gattc_exchange_mtu(g_conn_handle, NULL, NULL);
        } else {
            LOGE(TAG, "connect failed (status=%d)", event->connect.status);
            g_conn_handle = BLE_HS_CONN_HANDLE_NONE;
            start_advertise();
        }
        break;

    case BLE_GAP_EVENT_PHY_UPDATE_COMPLETE:
        g_phy_pending = 0;
        g_phy_status = event->phy_updated.status;
        LOGI(TAG, "phy updated: conn=%u status=%u tx=%u rx=%u",
             event->phy_updated.conn_handle, event->phy_updated.status,
             event->phy_updated.tx_phy, event->phy_updated.rx_phy);
        break;

    case BLE_GAP_EVENT_DISCONNECT:
        LOGI(TAG, "disconnected (reason=%d)", event->disconnect.reason);
        g_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        start_advertise();
        break;

    case BLE_GAP_EVENT_ADV_COMPLETE:
        start_advertise();
        break;

    case BLE_GAP_EVENT_SUBSCRIBE:
        LOGI(TAG, "subscribe: conn=%u attr=%u cur_notify=%u",
             event->subscribe.conn_handle, event->subscribe.attr_handle,
             event->subscribe.cur_notify);
        break;

    case BLE_GAP_EVENT_MTU:
        LOGI(TAG, "mtu: conn=%u cid=0x%04X mtu=%u",
             event->mtu.conn_handle, event->mtu.channel_id, event->mtu.value);
        break;

    case BLE_GAP_EVENT_CONN_UPDATE:
    case BLE_GAP_EVENT_CONN_UPDATE_REQ:
    default:
        break;
    }
    return 0;
}

static void start_advertise(void)
{
    struct ble_gap_adv_params adv_params;
    struct ble_hs_adv_fields fields;
    int rc;

    if (g_conn_handle != BLE_HS_CONN_HANDLE_NONE || ble_gap_adv_active()) {
        return;
    }

    memset(&fields, 0, sizeof(fields));
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    const char *name = ble_svc_gap_device_name();
    fields.name = (const uint8_t *)name;
    fields.name_len = strlen(name);
    fields.name_is_complete = 1;

    rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        LOGE(TAG, "adv_set_fields rc=%d", rc);
        return;
    }

    /* Repeat the complete name in scan response: active scans can otherwise
     * retain a stale cached name for the same public address. */
    memset(&fields, 0, sizeof(fields));
    fields.name = (const uint8_t *)name;
    fields.name_len = strlen(name);
    fields.name_is_complete = 1;
    rc = ble_gap_adv_rsp_set_fields(&fields);
    if (rc != 0) {
        LOGE(TAG, "adv_rsp_set_fields rc=%d", rc);
        return;
    }

    memset(&adv_params, 0, sizeof(adv_params));
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    adv_params.itvl_min = 32;   /* 20 ms */
    adv_params.itvl_max = 48;   /* 30 ms */

    rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER,
                           &adv_params, gap_event, NULL);
    if (rc != 0) {
        LOGE(TAG, "adv_start rc=%d", rc);
        return;
    }
    LOGI(TAG, "advertising as \"%s\"", name);
}

/* Vendor HCI command 0xFC32 (Set BD Address). The NZ8801 expects the
 * payload LSB-first; the address below is stored LSB-first already. */
static void vendor_set_bdaddr(const uint8_t addr_le[6])
{
    uint16_t opcode = 0xFC32;
    int rc = ble_hs_hci_cmd_tx(opcode, addr_le, 6, NULL, 0);
    if (rc == 0) {
        ble_hs_id_set_pub(addr_le);
        LOGI(TAG, "bdaddr set: %02X:%02X:%02X:%02X:%02X:%02X",
             addr_le[5], addr_le[4], addr_le[3], addr_le[2], addr_le[1], addr_le[0]);
    } else {
        LOGW(TAG, "vendor set bdaddr rc=%d", rc);
    }
}

static void on_sync(void)
{
    /* Static address, LSB-first. */
    static const uint8_t addr_le[6] = { 0x66, 0x84, 0x68, 0x26, 0xAB, 0x1E };
    vendor_set_bdaddr(addr_le);
    start_advertise();
}

static void on_reset(int reason)
{
    LOGW(TAG, "host reset (reason=%d)", reason);
}

static void host_task(void *param)
{
    nimble_port_run();          /* returns on nimble_port_stop() */

    vTaskDelete(NULL);
}

/* ------------------------------------------------------------- lifecycle */

void app_nimble_start(void)
{
    int rc;

    /* nimble_port_init() ends with ble_transport_ll_init() → SPI IRQ on. */
    nimble_port_init();

    rc = ble_gatts_count_cfg(g_gatt_services);
    configASSERT(rc == 0);
    rc = ble_gatts_add_svcs(g_gatt_services);
    configASSERT(rc == 0);

    ble_svc_gap_device_name_set("N32-NimBLE");

    ble_hs_cfg.sync_cb  = on_sync;
    ble_hs_cfg.reset_cb = on_reset;

    /* Host task at priority 4: above the SPI RX task (3) so TX-side
     * ready-waits in ble_spi_tx yield to a pending RX. */
    nimble_port_freertos_init(host_task);

    LOGI(TAG, "nimble host started");
}

#endif /* APP_MODE_NIMBLE */
