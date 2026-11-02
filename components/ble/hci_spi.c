/*
 * NimBLE HCI SPI transport for N32WB452 + NZ8801.
 *
 * Delivery contract (nimble mode):
 *   - Events are delivered as flat buffers allocated with
 *     ble_transport_alloc_evt(): evbuf[0]=evt_code, [1]=param_len, [2..]=params.
 *   - ACL frames are delivered as os_mbuf containing handle[2] + len[2] + data
 *     (no H4 type prefix).
 */
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

#include "n32wb452.h"
#include "n32wb452_gpio.h"
#include "log.h"

#include "hci_spi.h"
#include "ble_spi_tx.h"
#include "hci_fixup.h"
#include "ble_platform.h"

#if defined(APP_MODE_NIMBLE)
#include "nimble/ble.h"
#include "nimble/hci_common.h"
#include "nimble/transport.h"
#include "os/os_mbuf.h"
#endif

/* ===== External SPI / IO helpers from libn32wb452_ble.a ===== */
extern int32_t eif_spi_recv_bytes(uint8_t *data, uint16_t len);
extern void eif_spi_recv_irq_enable(uint8_t enable);
extern void ble_clear_wakeup(void);

/* ===== H4 packet types ===== */
#define HCI_H4_CMD   0x01
#define HCI_H4_ACL   0x02
#define HCI_H4_EVT   0x04

#define TAG "hci.spi"

#define HCI_SPI_RX_BUF_SIZE   512
#define HCI_SPI_RX_STACK_SIZE 4096
#define HCI_SPI_RX_PRIORITY   3      /* below the NimBLE host task (4) */

static SemaphoreHandle_t s_rx_sem;
static SemaphoreHandle_t s_tx_mutex;
static TaskHandle_t s_rx_task;
static volatile uint8_t s_initialized;

/* RX activity flag shared with the checked TX path. */
static volatile int s_rx_active;

int ble_spi_rx_pending(void)
{
    return s_rx_active || GPIO_ReadInputDataBit(GPIOE, GPIO_PIN_14);
}

static uint8_t irq_line_set(void)
{
    /* Matches the GPIOE14 poll inside the vendor eif_spi_recv_bytes(). */
    return GPIO_ReadInputDataBit(GPIOE, GPIO_PIN_14) ? 1U : 0U;
}

static int wait_irq_line(uint32_t max_loops)
{
    while (max_loops-- > 0U) {
        if (irq_line_set()) {
            return 1;
        }
    }
    return 0;
}

static int spi_read(uint8_t *data, uint16_t len)
{
    if (eif_spi_recv_bytes(data, len) != 0) {
        return -1;
    }
    return 0;
}

/* ======================================================================
 * RX: read one complete H4 frame
 * ====================================================================== */
int hci_spi_read_frame(uint8_t *buf, uint16_t buf_size)
{
    uint16_t payload_len;

    if (buf_size < 5) {
        return -EINVAL;
    }

    /* The IRQ line must be high; a wake without it is stale. */
    if (!wait_irq_line(2048)) {
        return -EIO;
    }

    /* Do not clear wakeup before the threaded SPI read: the vendor ISR
     * clears and reads back-to-back in interrupt context; a delayed clear
     * can drop GPIOE14 before eif_spi_recv_bytes() polls it. */

    if (spi_read(&buf[0], 1) != 0) {
        goto fail;
    }

    switch (buf[0]) {
    case HCI_H4_EVT:
        if (spi_read(&buf[1], 2) != 0) {   /* evt_code + param_len */
            goto fail;
        }
        payload_len = buf[2];
        if ((uint16_t)(3 + payload_len) > buf_size) {
            goto fail;
        }
        if (payload_len && spi_read(&buf[3], payload_len) != 0) {
            goto fail;
        }
        hci_fixup_evt(&buf[1], (uint8_t)payload_len);
        ble_clear_wakeup();
        return 3 + payload_len;

    case HCI_H4_ACL:
        if (spi_read(&buf[1], 4) != 0) {   /* handle + len */
            goto fail;
        }
        payload_len = (uint16_t)buf[3] | ((uint16_t)buf[4] << 8);
        if ((uint16_t)(5 + payload_len) > buf_size) {
            goto fail;
        }
        if (payload_len && spi_read(&buf[5], payload_len) != 0) {
            goto fail;
        }
        ble_clear_wakeup();
        return 5 + payload_len;

    default:
        LOGW(TAG, "unknown H4 type 0x%02X", buf[0]);
        goto fail;
    }

fail:
    ble_clear_wakeup();
    return -EIO;
}

/* ======================================================================
 * NimBLE transport hooks (nimble mode only)
 * ====================================================================== */
#if defined(APP_MODE_NIMBLE)

int ble_transport_to_ll_cmd_impl(void *buf)
{
    uint8_t *cmd = (uint8_t *)buf;
    uint8_t param_len;
    uint16_t total_len;
    uint8_t h4_buf[260];
    int rc;

    if (!cmd || !s_tx_mutex) {
        return -1;
    }

    param_len = cmd[2];
    total_len = 3 + param_len;

    h4_buf[0] = HCI_H4_CMD;
    memcpy(&h4_buf[1], cmd, total_len);

    if (xSemaphoreTake(s_tx_mutex, portMAX_DELAY) != pdTRUE) {
        return -1;
    }
    rc = ble_spi_send_checked(h4_buf, 1 + total_len);
    xSemaphoreGive(s_tx_mutex);

    if (rc) {
        LOGE(TAG, "TX cmd ready timeout; frame not sent");
    }
    ble_transport_free(buf);
    return rc;
}

int ble_transport_to_ll_acl_impl(struct os_mbuf *om)
{
    uint16_t pkt_len;
    uint8_t h4_buf[260];
    int rc;

    if (!om || !s_tx_mutex) {
        if (om) {
            os_mbuf_free_chain(om);
        }
        return -1;
    }

    pkt_len = OS_MBUF_PKTLEN(om);
    if (pkt_len + 1 > sizeof(h4_buf)) {
        os_mbuf_free_chain(om);
        return -1;
    }

    h4_buf[0] = HCI_H4_ACL;
    os_mbuf_copydata(om, 0, pkt_len, &h4_buf[1]);

    if (xSemaphoreTake(s_tx_mutex, portMAX_DELAY) != pdTRUE) {
        os_mbuf_free_chain(om);
        return -1;
    }
    rc = ble_spi_send_checked(h4_buf, 1 + pkt_len);
    xSemaphoreGive(s_tx_mutex);

    if (rc) {
        LOGE(TAG, "TX ACL ready timeout; frame not sent");
    }
    os_mbuf_free_chain(om);
    return rc;
}

int ble_transport_to_ll_iso_impl(struct os_mbuf *om)
{
    if (om) {
        os_mbuf_free_chain(om);
    }
    return -ENOTSUP;
}

static void publish_frame_to_host(const uint8_t *frame, uint16_t len)
{
    if (frame[0] == HCI_H4_EVT) {
        uint8_t param_len = frame[2];
        uint8_t *evbuf = (uint8_t *)ble_transport_alloc_evt(0);
        if (!evbuf) {
            evbuf = (uint8_t *)ble_transport_alloc_evt(1);
        }
        if (!evbuf) {
            LOGW(TAG, "no evt buf, drop code=0x%02X", frame[1]);
            return;
        }
        memcpy(evbuf, &frame[1], 2 + param_len);
        int rc = ble_transport_to_hs_evt(evbuf);
        if (rc != 0) {
            LOGW(TAG, "to_hs_evt rc=%d", rc);
        }
    } else if (frame[0] == HCI_H4_ACL) {
        uint16_t data_len = (uint16_t)frame[3] | ((uint16_t)frame[4] << 8);
        struct os_mbuf *om = ble_transport_alloc_acl_from_ll();
        if (!om) {
            LOGW(TAG, "no acl mbuf, drop len=%u", data_len);
            return;
        }
        int rc = os_mbuf_append(om, &frame[1], 4 + data_len);
        if (rc != 0) {
            os_mbuf_free_chain(om);
            return;
        }
        rc = ble_transport_to_hs_acl(om);
        if (rc) {
            LOGW(TAG, "to_hs_acl rc=%d", rc);
        }
    }
}

#endif /* APP_MODE_NIMBLE */

/* ======================================================================
 * RX task
 * ====================================================================== */
static void hci_rx_task(void *arg)
{
    (void)arg;
    uint8_t frame[HCI_SPI_RX_BUF_SIZE];

    while (1) {
        if (xSemaphoreTake(s_rx_sem, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        s_rx_active = 1;
        int len = hci_spi_read_frame(frame, sizeof(frame));
        s_rx_active = 0;

        if (len <= 0) {
            continue;
        }
#if defined(APP_MODE_NIMBLE)
        publish_frame_to_host(frame, (uint16_t)len);
#else
        /* bridge mode: hci bridge subscribes and drains frames */
        extern void bridge_handle_controller_frame(const uint8_t *frame, uint16_t len);
        bridge_handle_controller_frame(frame, (uint16_t)len);
#endif
    }
}

void hci_spi_irq_handler(void)
{
    BaseType_t woken = pdFALSE;

    if (s_initialized && s_rx_sem) {
        xSemaphoreGiveFromISR(s_rx_sem, &woken);
    }
    portYIELD_FROM_ISR(woken);
}

/* ======================================================================
 * Init
 * ====================================================================== */
void hci_spi_transport_init(void)
{
    if (s_initialized) {
        return;
    }

    s_tx_mutex = xSemaphoreCreateMutex();
    configASSERT(s_tx_mutex);

    s_rx_sem = xSemaphoreCreateCounting(16, 0);
    configASSERT(s_rx_sem);

    if (xTaskCreate(hci_rx_task, "hci_rx", HCI_SPI_RX_STACK_SIZE / sizeof(StackType_t),
                    NULL, HCI_SPI_RX_PRIORITY, &s_rx_task) != pdPASS) {
        configASSERT(0);
    }

    /*
     * eif_spi_recv_irq_enable() → eif_spi_recv_irq_gpio() internally calls
     * NVIC_PriorityGroupConfig(0x500), silently switching the NVIC priority
     * group from Group_4 (FreeRTOS requirement) to Group_2. Restore Group_4
     * and re-encode the EXTI priority afterwards.
     */
    eif_spi_recv_irq_enable(1);
    NVIC_SetPriorityGrouping(3);   /* Group_4 */
    NVIC_SetPriority(EXTI15_10_IRQn, NVIC_EncodePriority(3, 6, 0));
    NVIC_EnableIRQ(EXTI15_10_IRQn);

    s_initialized = 1;
    LOGI(TAG, "transport up (irq_line=%u)", irq_line_set());
}

#if defined(APP_MODE_NIMBLE)
/* NimBLE calls this from nimble_port_init(). */
void ble_transport_ll_init(void)
{
    hci_spi_transport_init();
}
#endif
