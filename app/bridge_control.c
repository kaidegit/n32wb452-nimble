#if defined(APP_MODE_BRIDGE)
/* HID callbacks only exchange reports. The worker owns the controller and upload. */
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "app_bridge.h"
#include "bridge_control.h"
#include "bridge_protocol.h"
#include "hci_spi.h"

extern int32_t send_vendor_array(uint8_t command, const uint8_t *data,
                                  uint16_t size, uint16_t crc);
extern int32_t send_vendor_bypass_command(void);
extern void eif_reset_ble_core(void);

/* Vendor usage, one 63-byte feature report plus report ID 1. */
static const uint8_t s_report_descriptor[] = {
    0x06, 0x00, 0xff, 0x09, 0x01, 0xa1, 0x01, 0x85, 0x01,
    0x15, 0x00, 0x26, 0xff, 0x00, 0x75, 0x08, 0x95, 0x3f,
    0x09, 0x01, 0xb1, 0x02, 0xc0
};

_Static_assert(sizeof(s_report_descriptor) == BRIDGE_HID_REPORT_DESC_SIZE,
               "HID descriptor length must match the configuration descriptor");

static struct usbd_interface s_interface;
static SemaphoreHandle_t s_request_sem;
static uint8_t s_request[BRIDGE_REPORT_SIZE];
static uint8_t s_response[BRIDGE_REPORT_SIZE];
static volatile bool s_pending;
static volatile uint32_t s_usb_epoch;
volatile bridge_CoreState bridge_core_state = bridge_CoreState_RESET_HELD;

/* Only one array is buffered. BEGIN replaces an unfinished upload. */
static struct {
    uint8_t data[BRIDGE_ARRAY_MAX];
    uint32_t size;
    uint32_t received;
    uint16_t crc;
    uint8_t command;
} s_upload;

static void discard_upload(void)
{
    s_upload.size = 0;
    s_upload.received = 0;
}

/* USB ISR: no SPI access, delays, or blocking RTOS calls here. */
static int hid_request(uint8_t busid, struct usb_setup_packet *setup,
                       uint8_t **data, uint32_t *len)
{
    (void)busid;
    const uint16_t feature_report = (3U << 8) | BRIDGE_REPORT_ID;
    if (setup->wValue != feature_report) {
        return -1;
    }

    if (setup->bRequest == 0x01 && setup->bmRequestType == 0xa1) {
        memcpy(*data, s_response, BRIDGE_REPORT_SIZE);
        *len = BRIDGE_REPORT_SIZE;
        return 0;
    }

    if (setup->bRequest != 0x09 || setup->bmRequestType != 0x21 ||
        setup->wLength != BRIDGE_REPORT_SIZE || *len != BRIDGE_REPORT_SIZE || s_pending) {
        return -1;
    }

    bridge_Control message;
    if (bridge_unpack(*data, &message) != 0) {
        return -1;
    }
    memcpy(s_request, *data, BRIDGE_REPORT_SIZE);
    message.data.size = 0;
    message.result = bridge_Result_BUSY;
    message.state = bridge_core_state;
    bridge_pack(s_response, &message);
    s_pending = true;

    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(s_request_sem, &woken);
    portYIELD_FROM_ISR(woken);
    return 0;
}

void bridge_control_usb_reset(void)
{
    /* The next request must not continue an upload from a previous USB session. */
    s_usb_epoch++;
}

static bridge_Result begin_array(const bridge_Control *message)
{
    if (bridge_core_state != bridge_CoreState_ROM) {
        return bridge_Result_INVALID_STATE;
    }
    discard_upload();
    if (message->argument == 0 || message->argument > BRIDGE_ARRAY_MAX ||
        message->crc > UINT16_MAX ||
        (message->command != BRIDGE_COMMAND_RAMCODE &&
         message->command != BRIDGE_COMMAND_PATCH &&
         message->command != BRIDGE_COMMAND_NVDS)) {
        return bridge_Result_INVALID_ARGUMENT;
    }
    s_upload.size = message->argument;
    s_upload.crc = (uint16_t)message->crc;
    s_upload.command = (uint8_t)message->command;
    return bridge_Result_OK;
}

static bridge_Result append_array(const bridge_Control *message)
{
    if (bridge_core_state != bridge_CoreState_ROM || s_upload.size == 0) {
        return bridge_Result_INVALID_STATE;
    }
    if (message->offset != s_upload.received || message->data.size == 0 ||
        message->data.size > s_upload.size - s_upload.received) {
        return bridge_Result_INVALID_ARGUMENT;
    }
    memcpy(s_upload.data + s_upload.received, message->data.bytes, message->data.size);
    s_upload.received += message->data.size;
    return bridge_Result_OK;
}

static bridge_Result vendor_result(int32_t result, bridge_Control *reply)
{
    reply->vendor_result = result;
    if (result != 0) {
        /* ROM may have consumed part of a command: require reset before retry. */
        bridge_core_state = bridge_CoreState_ERROR;
        return bridge_Result_VENDOR_ERROR;
    }
    return bridge_Result_OK;
}

static bridge_Result commit_array(bridge_Control *message)
{
    if (bridge_core_state != bridge_CoreState_ROM || s_upload.size == 0 ||
        s_upload.received != s_upload.size) {
        return bridge_Result_INVALID_STATE;
    }
    uint16_t size = (uint16_t)s_upload.size;
    discard_upload();
    if (bridge_crc16(s_upload.data, size) != s_upload.crc) {
        return bridge_Result_CRC_MISMATCH;
    }
    int32_t result = send_vendor_array(s_upload.command, s_upload.data, size, s_upload.crc);
    return vendor_result(result, message);
}

static bridge_Result execute(bridge_Control *message)
{
    message->vendor_result = 0;
    switch (message->operation) {
    case bridge_Operation_STATUS:
        return bridge_Result_OK;

    case bridge_Operation_START:
    case bridge_Operation_RESET:
        /* Wait for an active HCI read to finish, then reserve SPI for ROM commands. */
        hci_spi_set_enabled(0);
        bridge_core_state = bridge_CoreState_RESET_HELD;
        discard_upload();
        eif_reset_ble_core();
        bridge_core_state = bridge_CoreState_ROM;
        return bridge_Result_OK;

    case bridge_Operation_ARRAY_BEGIN:
        return begin_array(message);
    case bridge_Operation_ARRAY_DATA:
        return append_array(message);
    case bridge_Operation_ARRAY_COMMIT:
        return commit_array(message);

    case bridge_Operation_BYPASS:
        if (bridge_core_state != bridge_CoreState_ROM || s_upload.size != 0) {
            return bridge_Result_INVALID_STATE;
        }
        return vendor_result(send_vendor_bypass_command(), message);

    case bridge_Operation_HCI_ENABLE:
        if (bridge_core_state != bridge_CoreState_ROM || s_upload.size != 0) {
            return bridge_Result_INVALID_STATE;
        }
        bridge_core_state = bridge_CoreState_HCI;
        hci_spi_set_enabled(1);
        return bridge_Result_OK;

    default:
        return bridge_Result_INVALID_ARGUMENT;
    }
}

static void control_task(void *arg)
{
    (void)arg;
    uint32_t last_epoch = 0;
    for (;;) {
        xSemaphoreTake(s_request_sem, portMAX_DELAY);
        uint32_t epoch = s_usb_epoch;
        if (last_epoch != epoch) {
            discard_upload();
        }
        last_epoch = epoch;

        bridge_Control message = bridge_Control_init_zero;
        bridge_Result result = bridge_Result_INVALID_ARGUMENT;
        if (bridge_unpack(s_request, &message) == 0) {
            /* CDC TX takes this mutex too; reset cannot overlap an H4 write. */
            bridge_spi_lock();
            result = execute(&message);
            bridge_spi_unlock();
        }
        message.result = result;
        message.state = bridge_core_state;
        message.data.size = 0;
        uint8_t report[BRIDGE_REPORT_SIZE];
        bridge_pack(report, &message);

        /* GET_REPORT runs in USB ISR context and must see a complete response. */
        taskENTER_CRITICAL();
        memcpy(s_response, report, BRIDGE_REPORT_SIZE);
        s_pending = false;
        taskEXIT_CRITICAL();
    }
}

struct usbd_interface *bridge_control_init(void)
{
    s_request_sem = xSemaphoreCreateBinary();
    configASSERT(s_request_sem);
    configASSERT(xTaskCreate(control_task, "hidctl", 1024, NULL, 3, NULL) == pdPASS);

    bridge_Control message = bridge_Control_init_zero;
    bridge_pack(s_response, &message);
    s_interface.class_interface_handler = hid_request;
    s_interface.hid_report_descriptor = s_report_descriptor;
    s_interface.hid_report_descriptor_len = sizeof(s_report_descriptor);
    return &s_interface;
}
#endif
