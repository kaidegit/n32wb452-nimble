#if defined(APP_MODE_BRIDGE)
/*
 * USB CDC ACM ↔ SPI H4 passthrough ("bridge" firmware mode).
 *
 *   macOS host ── CDC ACM bulk ──► H4 parser ──► SPI ──► NZ8801 controller
 *   macOS host ◄── CDC ACM bulk ◄── frame queue ◄── SPI ◄── (PE14 IRQ)
 *
 * HID controls controller reset and firmware download before H4 is enabled.
 *
 * Flow notes:
 *  - USB→SPI: one 64-byte OUT transfer per packet; bytes land in a ring
 *    buffer from the endpoint callback (ISR context, priority 6, safe for
 *    FreeRTOS FromISR API), a task parses H4 frames and forwards them.
 *  - SPI→USB: complete frames are queued; the CDC task paces writes with
 *    the IN-endpoint completion (plus ZLP when a write is MPS-aligned).
 */
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

#include "usbd_core.h"
#include "usbd_cdc_acm.h"
#include "usb_config.h"

#include "n32wb452.h"
#include "log.h"
#include "app_bridge.h"
#include "bridge_control.h"
#include "hci_spi.h"
#include "ble_spi_tx.h"

#define TAG "bridge"

#define CDC_OUT_EP      0x02
#define CDC_IN_EP       0x81
#define CDC_INT_EP      0x83
#define CDC_MAX_MPS     64

#define USBD_VID        0x34B7
#define USBD_PID        0x0002
#define USBD_MAX_POWER  100

#define HID_IN_EP       0x84
#define HID_DESCRIPTOR_SIZE (9 + 9 + 7)
#define USB_CONFIG_SIZE (9 + CDC_ACM_DESCRIPTOR_LEN + HID_DESCRIPTOR_SIZE)

/* ------------------------------------------------------------ descriptors */

static const uint8_t s_dev_desc[] = {
    USB_DEVICE_DESCRIPTOR_INIT(USB_2_0, 0xEF, 0x02, 0x01, USBD_VID, USBD_PID, 0x0200, 0x01)
};

static const uint8_t s_cfg_desc[] = {
    USB_CONFIG_DESCRIPTOR_INIT(USB_CONFIG_SIZE, 0x03, 0x01, USB_CONFIG_BUS_POWERED, USBD_MAX_POWER),
    CDC_ACM_DESCRIPTOR_INIT(0x00, CDC_INT_EP, CDC_OUT_EP, CDC_IN_EP, CDC_MAX_MPS, 0x02),
    /* Interface 2: vendor HID, feature reports over EP0, interrupt IN reserved. */
    USB_INTERFACE_DESCRIPTOR_INIT(2, 0, 1, 3, 0, 0, 0),
    /* HID 1.11, no country code, one report descriptor. */
    9, 0x21, 0x11, 0x01, 0, 1, 0x22, BRIDGE_HID_REPORT_DESC_SIZE, 0,
    USB_ENDPOINT_DESCRIPTOR_INIT(HID_IN_EP, USB_ENDPOINT_TYPE_INTERRUPT, 64, 10)
};

static const char *const s_strings[] = {
    (const char[]){ 0x09, 0x04 },   /* LangID */
    "n32wb452-nimble",              /* Manufacturer */
    "N32 BLE HCI Bridge",           /* Product */
    "000000000002",                 /* Serial */
};

static const uint8_t *dev_desc_cb(uint8_t speed)            { (void)speed; return s_dev_desc; }
static const uint8_t *cfg_desc_cb(uint8_t speed)            { (void)speed; return s_cfg_desc; }
static const uint8_t *dev_qual_desc_cb(uint8_t speed)       { (void)speed; return NULL; }
static const char *string_desc_cb(uint8_t speed, uint8_t idx)
{
    (void)speed;
    if (idx >= sizeof(s_strings) / sizeof(s_strings[0])) {
        return NULL;
    }
    return s_strings[idx];
}

static const struct usb_descriptor s_cdc_desc = {
    .device_descriptor_callback = dev_desc_cb,
    .config_descriptor_callback = cfg_desc_cb,
    .device_quality_descriptor_callback = dev_qual_desc_cb,
    .string_descriptor_callback = string_desc_cb,
};

/* -------------------------------------------------------------- state ---- */

static struct usbd_interface s_intf0;
static struct usbd_interface s_intf1;

static USB_MEM_ALIGNX uint8_t s_ep_out_buf[CDC_MAX_MPS];

/* USB→SPI byte ring (single producer: OUT callback; single consumer task) */
#define USB_RX_RING_SIZE 8192
static uint8_t s_usb_ring[USB_RX_RING_SIZE];
static volatile uint32_t s_usb_ring_head;   /* ISR producer */
static volatile uint32_t s_usb_ring_tail;   /* task consumer */
static volatile uint32_t s_usb_ring_drop;

/* SPI→USB frame queue (single producer: hci RX task; single consumer task) */
#define CDC_TXQ_FRAMES 8
#define CDC_TXQ_FRAME_SIZE 300
static uint8_t s_txq[CDC_TXQ_FRAMES][CDC_TXQ_FRAME_SIZE];
static uint16_t s_txq_len[CDC_TXQ_FRAMES];
static volatile uint32_t s_txq_head;
static volatile uint32_t s_txq_tail;
static volatile uint32_t s_txq_drop;

static SemaphoreHandle_t s_usb_rx_sem;      /* bytes landed in ring */
static SemaphoreHandle_t s_cdc_tx_sem;      /* frame queued */
static SemaphoreHandle_t s_cdc_done_sem;    /* IN write completion */
static SemaphoreHandle_t s_spi_tx_mutex;

static volatile bool s_usb_configured;
static volatile bool s_dtr;

/* ------------------------------------------------------------ USB events --*/

static void ep_out_cb(uint8_t busid, uint8_t ep, uint32_t nbytes);
static void ep_in_cb(uint8_t busid, uint8_t ep, uint32_t nbytes);

static struct usbd_endpoint s_cdc_out_ep = { .ep_addr = CDC_OUT_EP, .ep_cb = ep_out_cb };
static struct usbd_endpoint s_cdc_in_ep  = { .ep_addr = CDC_IN_EP,  .ep_cb = ep_in_cb };

static void usbd_event_cb(uint8_t busid, uint8_t event)
{
    switch (event) {
    case USBD_EVENT_CONFIGURED:
        s_usb_configured = true;
        usbd_ep_start_read(busid, CDC_OUT_EP, s_ep_out_buf, CDC_MAX_MPS);
        break;
    case USBD_EVENT_RESET:
    case USBD_EVENT_DISCONNECTED:
        bridge_control_usb_reset();
        s_usb_configured = false;
        s_dtr = false;
        break;
    default:
        break;
    }
}

static void ep_out_cb(uint8_t busid, uint8_t ep, uint32_t nbytes)
{
    BaseType_t woken = pdFALSE;

    for (uint32_t i = 0; i < nbytes; i++) {
        uint32_t next = (s_usb_ring_head + 1) % USB_RX_RING_SIZE;
        if (next == s_usb_ring_tail) {
            s_usb_ring_drop++;
            break;
        }
        s_usb_ring[s_usb_ring_head] = s_ep_out_buf[i];
        s_usb_ring_head = next;
    }

    usbd_ep_start_read(busid, CDC_OUT_EP, s_ep_out_buf, CDC_MAX_MPS);
    (void)ep;

    if (s_usb_rx_sem) {
        xSemaphoreGiveFromISR(s_usb_rx_sem, &woken);
    }
    portYIELD_FROM_ISR(woken);
}

static void ep_in_cb(uint8_t busid, uint8_t ep, uint32_t nbytes)
{
    BaseType_t woken = pdFALSE;
    (void)busid;
    (void)ep;
    (void)nbytes;

    if (s_cdc_done_sem) {
        xSemaphoreGiveFromISR(s_cdc_done_sem, &woken);
    }
    portYIELD_FROM_ISR(woken);
}

void usbd_cdc_acm_set_dtr(uint8_t busid, uint8_t intf, bool dtr)
{
    (void)busid;
    (void)intf;
    s_dtr = dtr;
}

/* ------------------------------------------------------- USB→SPI path ----*/

static int ring_pop(uint8_t *out, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) {
        if (s_usb_ring_tail == s_usb_ring_head) {
            return -1;   /* underrun mid-frame: wait for more bytes */
        }
        out[i] = s_usb_ring[s_usb_ring_tail];
        s_usb_ring_tail = (s_usb_ring_tail + 1) % USB_RX_RING_SIZE;
    }
    return 0;
}

static uint8_t ring_peek(uint32_t off)
{
    return s_usb_ring[(s_usb_ring_tail + off) % USB_RX_RING_SIZE];
}

void bridge_spi_lock(void)
{
    xSemaphoreTake(s_spi_tx_mutex, portMAX_DELAY);
}

void bridge_spi_unlock(void)
{
    xSemaphoreGive(s_spi_tx_mutex);
}

/* Forward one complete H4 frame from the ring to the controller.
 * Returns 0: frame forwarded; -1: incomplete (need more bytes);
 * -2: ring empty. */
static int forward_one_frame(void)
{
    uint8_t frame[1 + 255 + 5];
    uint32_t need;

    if (s_usb_ring_tail == s_usb_ring_head) {
        return -2;
    }

    uint8_t type = ring_peek(0);
    switch (type) {
    case 0x01:  /* command: opcode(2) + plen(1) + params */
        need = 4;
        break;
    case 0x02:  /* ACL: handle(2) + len(2) + data */
        need = 5;
        break;
    default:
        LOGE(TAG, "H4 desync: bad type 0x%02X, resyncing", type);
        /* drop one byte and try again */
        s_usb_ring_tail = (s_usb_ring_tail + 1) % USB_RX_RING_SIZE;
        return -1;
    }

    /* header available? */
    uint32_t avail = (s_usb_ring_head + USB_RX_RING_SIZE - s_usb_ring_tail) % USB_RX_RING_SIZE;
    if (avail < need) {
        return -1;
    }

    if (type == 0x01) {
        need += ring_peek(3);                 /* param_len */
    } else {
        need += (uint16_t)ring_peek(3) | ((uint16_t)ring_peek(4) << 8);
    }

    if (need > sizeof(frame)) {
        s_usb_ring_tail = s_usb_ring_head;
        return -1;
    }
    if (avail < need) {
        return -1;
    }

    if (ring_pop(frame, need) != 0) {
        return -1;
    }

    if (xSemaphoreTake(s_spi_tx_mutex, portMAX_DELAY) != pdTRUE) {
        return -1;
    }
    int rc = -1;
    if (bridge_core_state == bridge_CoreState_HCI) {
        rc = ble_spi_send_checked(frame, (uint16_t)need);
    }
    xSemaphoreGive(s_spi_tx_mutex);

    if (rc != 0) {
        LOGE(TAG, "SPI TX dropped H4 frame (type 0x%02X len %u)", type, (unsigned)need);
    }
    return 0;
}

static void usb_to_spi_task(void *arg)
{
    (void)arg;
    while (1) {
        xSemaphoreTake(s_usb_rx_sem, portMAX_DELAY);
        while (forward_one_frame() == 0) {
            /* drain all complete frames */
        }
    }
}

/* ------------------------------------------------------- SPI→USB path ----*/

void bridge_handle_controller_frame(const uint8_t *frame, uint16_t len)
{
    BaseType_t woken = pdFALSE;

    if (len > CDC_TXQ_FRAME_SIZE) {
        s_txq_drop++;
        return;
    }

    uint32_t next = (s_txq_head + 1) % CDC_TXQ_FRAMES;
    if (next == s_txq_tail) {
        s_txq_drop++;
        return;
    }

    memcpy(s_txq[s_txq_head], frame, len);
    s_txq_len[s_txq_head] = len;
    s_txq_head = next;

    if (s_cdc_tx_sem) {
        xSemaphoreGiveFromISR(s_cdc_tx_sem, &woken);   /* also fine from task */
    }
    portYIELD_FROM_ISR(woken);
}

/* Write one buffer to the CDC IN endpoint, waiting for completion. */
static int cdc_write(const uint8_t *data, uint16_t len)
{
    if (!s_usb_configured) {
        return -1;
    }

    /* Drain any stale completion token (e.g. after USB reset). */
    while (xSemaphoreTake(s_cdc_done_sem, 0) == pdTRUE) {
    }

    if (usbd_ep_start_write(0, CDC_IN_EP, data, len) < 0) {
        return -1;
    }
    if (xSemaphoreTake(s_cdc_done_sem, pdMS_TO_TICKS(200)) != pdTRUE) {
        return -1;
    }

    /* MPS-aligned writes need a ZLP so the host sees the message boundary. */
    if (len && (len % CDC_MAX_MPS) == 0) {
        usbd_ep_start_write(0, CDC_IN_EP, NULL, 0);
        xSemaphoreTake(s_cdc_done_sem, pdMS_TO_TICKS(200));
    }
    return 0;
}

static void spi_to_usb_task(void *arg)
{
    (void)arg;
    uint32_t drop_report = 0;

    while (1) {
        xSemaphoreTake(s_cdc_tx_sem, portMAX_DELAY);

        while (s_txq_tail != s_txq_head) {
            const uint8_t *frame = s_txq[s_txq_tail];
            uint16_t len = s_txq_len[s_txq_tail];

            if (cdc_write(frame, len) != 0) {
                if (s_usb_configured) {
                    LOGW(TAG, "cdc write stalled; frame dropped");
                }
                /* Drop rather than block: HCI must not stall the SPI side. */
            }
            s_txq_tail = (s_txq_tail + 1) % CDC_TXQ_FRAMES;
        }

        if (s_txq_drop != drop_report) {
            drop_report = s_txq_drop;
            LOGW(TAG, "txq drops total=%lu", (unsigned long)drop_report);
        }
    }
}

/* ------------------------------------------------------------- lifecycle --*/

void app_bridge_start(void)
{
    s_usb_rx_sem   = xSemaphoreCreateCounting(64, 0);
    s_cdc_tx_sem   = xSemaphoreCreateCounting(CDC_TXQ_FRAMES, 0);
    s_cdc_done_sem = xSemaphoreCreateBinary();
    s_spi_tx_mutex = xSemaphoreCreateMutex();
    configASSERT(s_usb_rx_sem && s_cdc_tx_sem && s_cdc_done_sem && s_spi_tx_mutex);

    xTaskCreate(usb_to_spi_task, "usb2spi", 1024, NULL, 3, NULL);
    xTaskCreate(spi_to_usb_task, "spi2usb", 1024, NULL, 3, NULL);

    /* SPI IRQ path up (PE14 EXTI → hci RX task → bridge_handle_controller_frame) */
    hci_spi_transport_init();

    usbd_desc_register(0, &s_cdc_desc);
    usbd_add_interface(0, usbd_cdc_acm_init_intf(0, &s_intf0));
    usbd_add_interface(0, usbd_cdc_acm_init_intf(0, &s_intf1));
    usbd_add_interface(0, bridge_control_init());
    usbd_add_endpoint(0, &s_cdc_out_ep);
    usbd_add_endpoint(0, &s_cdc_in_ep);
    usbd_initialize(0, USB_BASE, usbd_event_cb);

    LOGI(TAG, "CDC + HID bridge started; controller held in reset");
}

#endif /* APP_MODE_BRIDGE */
