/*
 * NimBLE HCI transport over USB CDC ACM (H4 byte stream).
 *
 * TX: ble_transport_to_ll_cmd/acl_impl → framed H4 write() to the serial fd.
 * RX: reader thread → poll()/read() → H4 parser → hci_fixup_evt() →
 *     ble_transport_to_hs_evt / ble_transport_to_hs_acl.
 *
 * The NZ8801 feature-report patches mirror the firmware-side transport
 * (hci_fixup.c) and are idempotent, so it does not matter that the bridge
 * may have applied them already.
 */
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/serial/IOSerialKeys.h>
#include "bridge_protocol.h"
#endif

#include "nimble/ble.h"
#include "nimble/hci_common.h"
#include "nimble/transport.h"
#include "os/os_mbuf.h"

#include "hci_cdc.h"
#include "hci_fixup.h"

#define HCI_H4_CMD  0x01
#define HCI_H4_ACL  0x02
#define HCI_H4_EVT  0x04

static int s_fd = -1;
static char s_port[256];
static pthread_t s_rx_thread;
static pthread_mutex_t s_tx_mutex = PTHREAD_MUTEX_INITIALIZER;
static volatile int s_running;

/* ------------------------------------------------------------------ port */

const char *hci_cdc_port(void)
{
    return s_port[0] ? s_port : NULL;
}

#ifdef __APPLE__
/* Resolve serial services through their USB ancestors. The IOUSB registry plane
 * does not include serial child services, so parsing `ioreg -p IOUSB` cannot
 * reliably associate a CDC path with its device. */
static int usb_ancestor_id(io_registry_entry_t service, CFStringRef key)
{
    CFTypeRef value = IORegistryEntrySearchCFProperty(
        service, kIOServicePlane, key, kCFAllocatorDefault,
        kIORegistryIterateParents | kIORegistryIterateRecursively);
    int number = -1;
    if (value) {
        if (CFGetTypeID(value) == CFNumberGetTypeID()) {
            CFNumberGetValue((CFNumberRef)value, kCFNumberIntType, &number);
        }
        CFRelease(value);
    }
    return number;
}

const char *hci_cdc_autodetect_port(void)
{
    static char found[256];
    io_iterator_t services;
    kern_return_t result = IOServiceGetMatchingServices(
        kIOMainPortDefault, IOServiceMatching(kIOSerialBSDServiceValue), &services);
    if (result != KERN_SUCCESS) {
        return NULL;
    }

    unsigned matches = 0;
    io_object_t service;
    while ((service = IOIteratorNext(services)) != IO_OBJECT_NULL) {
        int vendor = usb_ancestor_id(service, CFSTR("idVendor"));
        int product = usb_ancestor_id(service, CFSTR("idProduct"));
        if (vendor == BRIDGE_VID && product == BRIDGE_PID) {
            CFTypeRef path = IORegistryEntryCreateCFProperty(
                service, CFSTR(kIOCalloutDeviceKey), kCFAllocatorDefault, 0);
            if (path) {
                if (CFGetTypeID(path) == CFStringGetTypeID() &&
                    CFStringGetCString((CFStringRef)path, found, sizeof(found), kCFStringEncodingUTF8)) {
                    matches++;
                }
                CFRelease(path);
            }
        }
        IOObjectRelease(service);
    }
    IOObjectRelease(services);
    if (matches > 1) {
        fprintf(stderr, "blectl: multiple N32 bridges found; select one with --port\n");
    }
    return matches == 1 ? found : NULL;
}
#else
const char *hci_cdc_autodetect_port(void)
{
    /* Automatic CDC discovery currently supports macOS. */
    return NULL;
}
#endif

/* ----------------------------------------------------------------- open --*/

int hci_cdc_open(const char *dev_path)
{
    struct termios tio;

    if (!dev_path || !dev_path[0]) {
        return -1;
    }

    s_fd = open(dev_path, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (s_fd < 0) {
        fprintf(stderr, "blectl: open %s failed: %s\n", dev_path, strerror(errno));
        return -1;
    }

    memset(&tio, 0, sizeof(tio));
    if (tcgetattr(s_fd, &tio) == 0) {
        cfmakeraw(&tio);
        tio.c_cflag |= CLOCAL | CREAD;
        tio.c_cflag &= ~(unsigned)HUPCL;
        tio.c_cc[VMIN] = 1;
        tio.c_cc[VTIME] = 0;
        cfsetispeed(&tio, B115200);   /* ignored by CDC-ACM */
        cfsetospeed(&tio, B115200);
        tcsetattr(s_fd, TCSANOW, &tio);
    }
    /* Drain any stale bytes so the H4 parser starts on a frame boundary. */
    tcflush(s_fd, TCIOFLUSH);

    snprintf(s_port, sizeof(s_port), "%s", dev_path);
    s_running = 1;
    return 0;
}

void hci_cdc_close(void)
{
    if (s_fd >= 0) {
        s_running = 0;
        close(s_fd);           /* unblocks poll() in the reader thread */
        s_fd = -1;
        pthread_join(s_rx_thread, NULL);
    }
    s_port[0] = '\0';
}

/* ----------------------------------------------------------------- h4 rx -*/

static void publish_evt(const uint8_t *pkt, uint8_t param_len)
{
    uint8_t *evbuf = (uint8_t *)ble_transport_alloc_evt(0);
    if (!evbuf) {
        evbuf = (uint8_t *)ble_transport_alloc_evt(1);
    }
    if (!evbuf) {
        fprintf(stderr, "blectl: evt pool empty, drop\n");
        return;
    }
    memcpy(evbuf, pkt, 2 + param_len);
    hci_fixup_evt(evbuf, param_len);
    ble_transport_to_hs_evt(evbuf);
}

static void publish_acl(const uint8_t *pkt, uint16_t data_len)
{
    struct os_mbuf *om = ble_transport_alloc_acl_from_ll();
    if (!om) {
        fprintf(stderr, "blectl: acl pool empty, drop\n");
        return;
    }
    if (os_mbuf_append(om, pkt, 4 + data_len) != 0) {
        os_mbuf_free_chain(om);
        return;
    }
    ble_transport_to_hs_acl(om);
}

/* Byte-wise H4 reassembly (stream may split frames arbitrarily). */
static void feed_byte(uint8_t b)
{
    static enum { ST_TYPE, ST_HDR, ST_PAYLOAD } state = ST_TYPE;
    static uint8_t type;
    static uint8_t hdr[4];
    static uint8_t hdr_len, hdr_got;
    static uint16_t payload_len, payload_got;
    static uint8_t payload[260];

    switch (state) {
    case ST_TYPE:
        type = b;
        hdr_got = 0;
        payload_got = 0;
        if (type == HCI_H4_EVT) {
            hdr_len = 2;                 /* evt_code, param_len */
        } else if (type == HCI_H4_ACL) {
            hdr_len = 4;                 /* handle, len */
        } else {
            fprintf(stderr, "blectl: H4 desync (type 0x%02X)\n", type);
            return;
        }
        state = ST_HDR;
        break;

    case ST_HDR:
        hdr[hdr_got++] = b;
        if (hdr_got < hdr_len) {
            break;
        }
        if (type == HCI_H4_EVT) {
            payload_len = hdr[1];
        } else {
            payload_len = (uint16_t)hdr[2] | ((uint16_t)hdr[3] << 8);
        }
        if (payload_len == 0) {
            goto frame_done;
        }
        if (payload_len > sizeof(payload)) {
            fprintf(stderr, "blectl: H4 frame too long (%u), resync\n", payload_len);
            state = ST_TYPE;
            break;
        }
        state = ST_PAYLOAD;
        break;

    case ST_PAYLOAD:
        payload[payload_got++] = b;
        if (payload_got == payload_len) {
            goto frame_done;
        }
        break;
    }
    return;

frame_done:
    if (type == HCI_H4_EVT) {
        uint8_t full[2 + 255];
        full[0] = hdr[0];
        full[1] = hdr[1];
        if (payload_len) {
            memcpy(&full[2], payload, payload_len);
        }
        publish_evt(full, hdr[1]);
    } else {
        uint8_t full[4 + 256];
        memcpy(full, hdr, 4);
        if (payload_len) {
            memcpy(&full[4], payload, payload_len);
        }
        publish_acl(full, payload_len);
    }
    state = ST_TYPE;
}

static void *rx_thread_fn(void *arg)
{
    (void)arg;
    uint8_t buf[512];

    while (s_running) {
        struct pollfd pfd = { .fd = s_fd, .events = POLLIN };
        int pr = poll(&pfd, 1, 200);
        if (pr <= 0) {
            continue;
        }
        ssize_t n = read(s_fd, buf, sizeof(buf));
        if (n <= 0) {
            if (n < 0 && (errno == EAGAIN || errno == EINTR)) {
                continue;
            }
            if (s_running) {
                fprintf(stderr, "blectl: serial read error: %s\n", strerror(errno));
            }
            break;
        }
        for (ssize_t i = 0; i < n; i++) {
            feed_byte(buf[i]);
        }
    }
    return NULL;
}

/* ------------------------------------------------------------- transport */

static int write_all(const uint8_t *data, size_t len)
{
    size_t off = 0;
    while (off < len) {
        ssize_t n = write(s_fd, data + off, len - off);
        if (n < 0) {
            if (errno == EAGAIN || errno == EINTR) {
                continue;
            }
            return -1;
        }
        off += (size_t)n;
    }
    return 0;
}

int ble_transport_to_ll_cmd_impl(void *buf)
{
    uint8_t *cmd = (uint8_t *)buf;
    uint8_t h4[260];
    uint16_t total = 3 + cmd[2];
    int rc = -1;

    pthread_mutex_lock(&s_tx_mutex);
    if (s_fd >= 0) {
        h4[0] = HCI_H4_CMD;
        memcpy(&h4[1], cmd, total);
        rc = write_all(h4, 1 + total);
    }
    pthread_mutex_unlock(&s_tx_mutex);

    ble_transport_free(buf);
    return rc;
}

int ble_transport_to_ll_acl_impl(struct os_mbuf *om)
{
    uint16_t len = OS_MBUF_PKTLEN(om);
    uint8_t h4[1 + 260];
    int rc = -1;

    if (len + 1 > sizeof(h4)) {
        os_mbuf_free_chain(om);
        return -1; /* frame too large */
    }

    pthread_mutex_lock(&s_tx_mutex);
    if (s_fd >= 0) {
        h4[0] = HCI_H4_ACL;
        os_mbuf_copydata(om, 0, len, &h4[1]);
        rc = write_all(h4, 1 + len);
    }
    pthread_mutex_unlock(&s_tx_mutex);

    os_mbuf_free_chain(om);
    return rc;
}

int ble_transport_to_ll_iso_impl(struct os_mbuf *om)
{
    os_mbuf_free_chain(om);
    return -ENOTSUP;
}

/* NimBLE calls this from nimble_port_init(). */
void ble_transport_ll_init(void)
{
    if (pthread_create(&s_rx_thread, NULL, rx_thread_fn, NULL) != 0) {
        fprintf(stderr, "blectl: cannot spawn rx thread\n");
        exit(1);
    }
}
