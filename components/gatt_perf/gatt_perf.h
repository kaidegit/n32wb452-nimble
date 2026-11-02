#ifndef GATT_PERF_H
#define GATT_PERF_H

#include "host/ble_gap.h"
#include "host/ble_gatt.h"

extern uint16_t gatt_perf_control_handle;
extern uint16_t gatt_perf_data_handle;
extern const ble_uuid128_t gatt_perf_control_uuid;
extern const ble_uuid128_t gatt_perf_data_uuid;

/* Platform-provided monotonic 1 MHz timestamp, wrapping modulo 2^32. */
uint32_t gatt_perf_time_us(void);
void gatt_perf_init(void);
void gatt_perf_gap_event(struct ble_gap_event *event);
int gatt_perf_access(uint16_t conn_handle, uint16_t attr_handle,
                     struct ble_gatt_access_ctxt *ctxt, void *arg);

#endif
