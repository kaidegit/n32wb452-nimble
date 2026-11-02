#ifndef PERF_MOCKS_H
#define PERF_MOCKS_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#define BLE_UUID128_INIT(...) {{0}}
typedef struct { uint8_t u[16]; } ble_uuid128_t;
#define BLE_ATT_ERR_UNLIKELY 14
#define BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN 13
#define BLE_ATT_ERR_INSUFFICIENT_RES 17
#define BLE_GATT_ACCESS_OP_READ_CHR 0
#define BLE_GATT_ACCESS_OP_WRITE_CHR 1
#define BLE_GAP_EVENT_SUBSCRIBE 1
#define BLE_GAP_EVENT_DISCONNECT 2
struct os_mbuf { uint8_t data[512]; uint16_t length; };
#define OS_MBUF_PKTLEN(om) ((om)->length)
struct ble_gatt_access_ctxt { int op; struct os_mbuf *om; };
struct ble_gap_event { int type; struct {uint16_t attr_handle;uint8_t cur_notify;} subscribe; };
struct ble_gap_conn_desc { uint16_t conn_itvl; };
struct ble_gap_upd_params { uint16_t itvl_min,itvl_max,supervision_timeout; };
struct ble_npl_event { int unused; };
struct ble_npl_callout { void (*callback)(struct ble_npl_event *); int scheduled; };
static uint32_t mock_time;
static int mock_notify_count;
static int mock_dle_result, mock_dle_requests;
static struct os_mbuf mock_tx;
static uint32_t ble_npl_time_get(void) { return mock_time; }
static uint32_t ble_npl_time_ticks_to_ms32(uint32_t n) { return n; }
static void *nimble_port_get_dflt_eventq(void) { return NULL; }
static void ble_npl_callout_init(struct ble_npl_callout *c, void *q,
    void (*callback)(struct ble_npl_event *), void *arg) { c->callback=callback; }
static void ble_npl_callout_stop(struct ble_npl_callout *c) { c->scheduled=0; }
static void ble_npl_callout_reset(struct ble_npl_callout *c, unsigned ticks) { c->scheduled=1; }
static unsigned os_msys_num_free(void) { return 24; }
static int os_mbuf_copydata(struct os_mbuf *om, unsigned offset, unsigned n, void *dst) {
    if (offset+n>om->length) return -1;
    memcpy(dst,om->data+offset,n);return 0;
}
static int os_mbuf_append(struct os_mbuf *om, const void *data, unsigned n) {
    if (om->length+n>sizeof(om->data)) return -1;
    memcpy(om->data+om->length,data,n);om->length+=n;return 0;
}
static int ble_hs_mbuf_to_flat(struct os_mbuf *om, void *dst, unsigned capacity, uint16_t *n) {
    if (om->length>capacity) return -1;
    *n=om->length;memcpy(dst,om->data,*n);return 0;
}
static struct os_mbuf *ble_hs_mbuf_from_flat(const void *data, unsigned n) {
    mock_tx.length=0;os_mbuf_append(&mock_tx,data,n);return &mock_tx;
}
static int ble_gatts_notify_custom(uint16_t c,uint16_t a,struct os_mbuf *om) {
    mock_notify_count++;return 0;
}
static int ble_gap_set_data_len(uint16_t conn, uint16_t octets, uint16_t time) {
    if (conn != 7 || octets != 251 || time != 2120) return -1;
    mock_dle_requests++;return mock_dle_result;
}
static uint16_t ble_att_mtu(uint16_t c) { return 247; }
static int ble_gap_conn_find(uint16_t c,struct ble_gap_conn_desc *d) {d->conn_itvl=12;return 0;}
static int ble_gap_update_params(uint16_t c,const struct ble_gap_upd_params *p) {return 0;}
#endif
