/*
 * blectl — command-line NimBLE host over the N32WB452 USB CDC HCI bridge.
 *
 * Examples:
 *   blectl info
 *   blectl scan -t 10
 *   blectl scan --name N32 -t 10
 *   blectl probe --name N32-NimBLE
 *   blectl probe --addr F1:AB:26:68:84:66 --write-hex "68 65 6c 6c 6f"
 *
 * NimBLE logs go to stderr; command output goes to stdout.
 */
#include <errno.h>
#include <getopt.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "nimble/nimble_port.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "host/ble_hs_id.h"
#include "host/ble_gatt.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

#include "hci_cdc.h"
#include "bridge_hid.h"

/* NUS service/characteristics of the nimble-mode firmware. */
static const ble_uuid128_t NUS_SVC =
    BLE_UUID128_INIT(0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0,
                     0x93, 0xF3, 0xA3, 0xB5, 0x01, 0x00, 0x40, 0x6E);
static const ble_uuid128_t NUS_RX =
    BLE_UUID128_INIT(0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0,
                     0x93, 0xF3, 0xA3, 0xB5, 0x02, 0x00, 0x40, 0x6E);
static const ble_uuid128_t NUS_TX =
    BLE_UUID128_INIT(0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0,
                     0x93, 0xF3, 0xA3, 0xB5, 0x03, 0x00, 0x40, 0x6E);

extern int ble_hs_hci_cmd_tx(uint16_t opcode, const void *cmd, uint8_t cmd_len,
                             void *rsp, uint8_t rsp_len);
extern void ble_hs_id_set_pub(const uint8_t *pub_addr);

static const char *g_cmd;
static const char *g_name_filter;
static char g_addr_filter[18];
static int g_scan_timeout = 10;
static const char *g_write_hex;
static const char *g_bdaddr;

/* scan/probe shared state */
static volatile int g_match_found;
static ble_addr_t g_match_addr;
static volatile int g_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static volatile int g_disc_done;
static volatile int g_echo_notify;

/* ------------------------------------------------------------------ util */

static void hexdump_line(const uint8_t *data, int len)
{
    for (int i = 0; i < len; i++) {
        printf("%02X ", data[i]);
    }
}

static int parse_bdaddr(const char *str, uint8_t out_le[6])
{
    unsigned b[6];
    if (sscanf(str, "%02X:%02X:%02X:%02X:%02X:%02X",
               &b[5], &b[4], &b[3], &b[2], &b[1], &b[0]) != 6) {
        return -1;
    }
    for (int i = 0; i < 6; i++) {
        out_le[i] = (uint8_t)b[i];
    }
    return 0;
}

static int hex_byte(const char *s)
{
    int hi = s[0], lo = s[1];
    if (hi >= '0' && hi <= '9') hi -= '0';
    else if (hi >= 'a' && hi <= 'f') hi -= 'a' - 10;
    else if (hi >= 'A' && hi <= 'F') hi -= 'A' - 10;
    else return -1;
    if (lo >= '0' && lo <= '9') lo -= '0';
    else if (lo >= 'a' && lo <= 'f') lo -= 'a' - 10;
    else if (lo >= 'A' && lo <= 'F') lo -= 'A' - 10;
    else return -1;
    return (hi << 4) | lo;
}

static int parse_hex(const char *str, uint8_t *out, int max)
{
    int n = 0;
    while (*str) {
        while (*str == ' ' || *str == ',') str++;
        if (!*str) break;
        int b = hex_byte(str);
        if (b < 0 || n >= max) return -1;
        out[n++] = (uint8_t)b;
        str += 2;
    }
    return n;
}

static void addr_str(const ble_addr_t *addr, char *buf, size_t len)
{
    const uint8_t *a = addr->val;
    snprintf(buf, len, "%02X:%02X:%02X:%02X:%02X:%02X",
             a[5], a[4], a[3], a[2], a[1], a[0]);
}

/* ----------------------------------------------------------------- sync --*/

static void on_sync(void)
{
    if (g_bdaddr) {
        uint8_t addr_le[6];
        if (parse_bdaddr(g_bdaddr, addr_le) == 0) {
            /* vendor 0xFC32 Set BD Address (LSB-first payload) */
            ble_hs_hci_cmd_tx(0xFC32, addr_le, 6, NULL, 0);
            ble_hs_id_set_pub(addr_le);
        }
    }
}

static int wait_sync(int timeout_ms)
{
    for (int waited = 0; waited < timeout_ms; waited += 50) {
        if (ble_hs_synced()) {
            return 0;
        }
        usleep(50000);
    }
    return -1;
}

/* --------------------------------------------------------------- scan ----*/

static int adv_matches(const struct ble_gap_disc_desc *disc, char *name_out, size_t name_cap)
{
    struct ble_hs_adv_fields fields;
    char addr[18];

    name_out[0] = '\0';
    addr_str(&disc->addr, addr, sizeof(addr));

    if (g_addr_filter[0]) {
        if (strcmp(addr, g_addr_filter) != 0) {
            return 0;
        }
    }

    if (ble_hs_adv_parse_fields(&fields, disc->data, disc->length_data) == 0 &&
        fields.name && fields.name_len > 0) {
        size_t n = fields.name_len < name_cap - 1 ? fields.name_len : name_cap - 1;
        memcpy(name_out, fields.name, n);
        name_out[n] = '\0';
    }

    if (g_name_filter && !strstr(name_out, g_name_filter)) {
        return 0;
    }
    return 1;
}

static int on_disc(struct ble_gap_event *event, void *arg)
{
    (void)arg;

    if (event->type != BLE_GAP_EVENT_DISC) {
        return 0;
    }

    char name[64];
    char addr[18];
    addr_str(&event->disc.addr, addr, sizeof(addr));

    if (!adv_matches(&event->disc, name, sizeof(name))) {
        return 0;
    }

    printf("%s  rssi=%d  name=%s\n", addr, event->disc.rssi, name);
    fflush(stdout);

    g_match_addr = event->disc.addr;
    g_match_found = 1;
    return 0;
}

static int do_scan(void)
{
    struct ble_gap_disc_params params = {
        .itvl = 0, .window = 0,
        .filter_policy = 0, .limited = 0, .passive = 0,
        .filter_duplicates = 1,
    };

    int rc = ble_gap_disc(BLE_OWN_ADDR_PUBLIC, g_scan_timeout * 1000,
                          &params, on_disc, NULL);
    if (rc != 0) {
        fprintf(stderr, "blectl: scan start failed rc=%d\n", rc);
        return 1;
    }

    for (int waited = 0; waited < g_scan_timeout * 1000 + 2000; waited += 100) {
        usleep(100000);
    }
    ble_gap_disc_cancel();
    return 0;
}

/* --------------------------------------------------------------- probe ---*/

static int on_disc_for_connect(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    if (event->type != BLE_GAP_EVENT_DISC) {
        return 0;
    }
    char name[64];
    if (adv_matches(&event->disc, name, sizeof(name))) {
        g_match_addr = event->disc.addr;
        g_match_found = 1;
    }
    return 0;
}

/* GATT tree printing + NUS echo */
struct svc_ctx {
    uint16_t svc_start;
    uint16_t svc_end;
};

static uint16_t g_nus_rx_handle, g_nus_tx_handle, g_nus_tx_cccd;

static int on_chr(uint16_t conn_handle, const struct ble_gatt_error *error,
                 const struct ble_gatt_chr *chr, void *arg)
{
    struct svc_ctx *svc = arg;
    char uuid_str[BLE_UUID_STR_LEN];

    (void)conn_handle;
    if (error != NULL && error->status != 0) {
        if (error->status != BLE_HS_EDONE) {
            fprintf(stderr, "blectl: chr disc error %d\n", error->status);
        }
        g_disc_done = 1;
        return 0;
    }
    if (chr == NULL) {
        g_disc_done = 1;
        return 0;
    }

    ble_uuid_to_str(&chr->uuid.u, uuid_str);
    printf("    chr %-6u props=0x%02x uuid=%s\n", chr->val_handle, chr->properties, uuid_str);

    if (ble_uuid_cmp(&chr->uuid.u, &NUS_RX.u) == 0) {
        g_nus_rx_handle = chr->val_handle;
    } else if (ble_uuid_cmp(&chr->uuid.u, &NUS_TX.u) == 0) {
        g_nus_tx_handle = chr->val_handle;
    }
    (void)svc;
    return 0;
}

static int on_svc(uint16_t conn_handle, const struct ble_gatt_error *error,
                 const struct ble_gatt_svc *svc, void *arg)
{
    char uuid_str[BLE_UUID_STR_LEN];

    (void)conn_handle;
    if (error != NULL && error->status != 0) {
        if (error->status != BLE_HS_EDONE) {
            fprintf(stderr, "blectl: svc disc error %d\n", error->status);
        }
        g_disc_done = 1;
        return 0;
    }
    if (svc == NULL) {
        g_disc_done = 1;
        return 0;
    }

    ble_uuid_to_str(&svc->uuid.u, uuid_str);
    printf("svc  %-6u..%-6u uuid=%s\n", svc->start_handle, svc->end_handle, uuid_str);

    /* discover characteristics inside this service */
    g_disc_done = 0;
    int rc = ble_gattc_disc_all_chrs(g_conn_handle, svc->start_handle, svc->end_handle,
                                     on_chr, NULL);
    if (rc != 0) {
        fprintf(stderr, "blectl: disc chrs rc=%d\n", rc);
        g_disc_done = 1;
    } else {
        while (!g_disc_done) {
            usleep(20000);
        }
    }
    (void)arg;
    return 0;
}

static int on_cccd_write(uint16_t conn_handle, const struct ble_gatt_error *error,
                         struct ble_gatt_attr *attr, void *arg)
{
    (void)conn_handle;
    (void)attr;
    (void)arg;
    if (error && error->status != 0) {
        fprintf(stderr, "blectl: CCCD write failed %d\n", error->status);
    }
    g_disc_done = 1;
    return 0;
}

static int on_write_done(uint16_t conn_handle, const struct ble_gatt_error *error,
                         struct ble_gatt_attr *attr, void *arg)
{
    (void)conn_handle;
    (void)attr;
    (void)arg;
    if (error && error->status != 0) {
        fprintf(stderr, "blectl: write failed %d\n", error->status);
    }
    g_disc_done = 1;
    return 0;
}

static int on_gap_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    struct ble_gap_conn_desc desc;

    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status != 0) {
            fprintf(stderr, "blectl: connect failed status=%d\n", event->connect.status);
            g_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        } else {
            g_conn_handle = event->connect.conn_handle;
            ble_gap_conn_find(g_conn_handle, &desc);
            printf("connected: conn=%u interval=%u ms\n",
                   g_conn_handle, desc.conn_itvl * 5 / 4);
        }
        break;

    case BLE_GAP_EVENT_DISCONNECT:
        printf("disconnected (reason=%d)\n", event->disconnect.reason);
        g_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        g_echo_notify = -1;   /* stop echo wait */
        break;

    case BLE_GAP_EVENT_MTU:
        printf("mtu: cid=0x%04x mtu=%u\n", event->mtu.channel_id, event->mtu.value);
        break;

    case BLE_GAP_EVENT_NOTIFY_RX:
        if (event->notify_rx.attr_handle == g_nus_tx_handle) {
            int len = OS_MBUF_PKTLEN(event->notify_rx.om);
            uint8_t data[256];
            if (len > (int)sizeof(data)) len = sizeof(data);
            os_mbuf_copydata(event->notify_rx.om, 0, len, data);
            printf("notify (NUS TX) %d bytes: ", len);
            hexdump_line(data, len);
            printf("\n");
            g_echo_notify = 1;
        }
        break;

    default:
        break;
    }
    return 0;
}

static int do_probe(void)
{
    /* 1) find the peer by scanning */
    struct ble_gap_disc_params params = { .filter_duplicates = 1 };
    int rc = ble_gap_disc(BLE_OWN_ADDR_PUBLIC, g_scan_timeout * 1000,
                          &params, on_disc_for_connect, NULL);
    if (rc != 0) {
        fprintf(stderr, "blectl: scan start failed rc=%d\n", rc);
        return 1;
    }

    for (int waited = 0; waited < g_scan_timeout * 1000 && !g_match_found; waited += 100) {
        usleep(100000);
    }
    ble_gap_disc_cancel();
    if (!g_match_found) {
        fprintf(stderr, "blectl: no matching device found\n");
        return 1;
    }

    char addr[18];
    addr_str(&g_match_addr, addr, sizeof(addr));
    printf("connecting to %s ...\n", addr);

    /* 2) connect */
    rc = ble_gap_connect(BLE_OWN_ADDR_PUBLIC, &g_match_addr, 15000, NULL,
                         on_gap_event, NULL);
    if (rc != 0) {
        fprintf(stderr, "blectl: connect rc=%d\n", rc);
        return 1;
    }
    for (int waited = 0; waited < 16000 && g_conn_handle == BLE_HS_CONN_HANDLE_NONE; waited += 100) {
        usleep(100000);
    }
    if (g_conn_handle == BLE_HS_CONN_HANDLE_NONE) {
        return 1;
    }

    /* 3) MTU + full discovery */
    ble_gattc_exchange_mtu(g_conn_handle, NULL, NULL);
    sleep(1);

    g_disc_done = 0;
    rc = ble_gattc_disc_all_svcs(g_conn_handle, on_svc, NULL);
    if (rc != 0) {
        fprintf(stderr, "blectl: disc svcs rc=%d\n", rc);
        goto out;
    }
    for (int waited = 0; waited < 10000 && !g_disc_done; waited += 100) {
        usleep(100000);
    }

    /* 4) NUS echo if present */
    if (g_nus_tx_handle) {
        g_nus_tx_cccd = g_nus_tx_handle + 1;   /* CCCD conventionally follows */
        uint16_t cc = 0x0001;
        g_disc_done = 0;
        rc = ble_gattc_write_flat(g_conn_handle, g_nus_tx_cccd, &cc, sizeof(cc),
                                  on_cccd_write, NULL);
        if (rc != 0) {
            fprintf(stderr, "blectl: cccd write rc=%d\n", rc);
        } else {
            for (int waited = 0; waited < 2000 && !g_disc_done; waited += 50) usleep(50000);
        }
    }

    if (g_nus_rx_handle) {
        uint8_t data[247];
        int len = 8;
        if (g_write_hex) {
            len = parse_hex(g_write_hex, data, sizeof(data));
            if (len < 0) {
                fprintf(stderr, "blectl: bad --write-hex\n");
                goto out;
            }
        } else {
            for (int i = 0; i < len; i++) data[i] = (uint8_t)i;
        }
        printf("writing %d bytes to NUS RX: ", len);
        hexdump_line(data, len);
        printf("\n");

        g_disc_done = 0;
        g_echo_notify = 0;
        rc = ble_gattc_write_flat(g_conn_handle, g_nus_rx_handle, data, len,
                                  on_write_done, NULL);
        if (rc != 0) {
            fprintf(stderr, "blectl: write rc=%d\n", rc);
            goto out;
        }
        for (int waited = 0; waited < 3000 && g_echo_notify == 0; waited += 50) {
            usleep(50000);
        }
        printf(g_echo_notify == 1 ? "echo round-trip OK\n"
                                  : "echo notification not received\n");
    } else {
        printf("(no NUS service on this peer; discovery only)\n");
    }

out:
    if (g_conn_handle != BLE_HS_CONN_HANDLE_NONE) {
        ble_gap_terminate(g_conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        sleep(1);
    }
    return 0;
}

/* --------------------------------------------------------------- info ----*/

static int do_info(void)
{
    uint8_t addr[6];
    char buf[18];

    ble_hs_id_copy_addr(BLE_ADDR_PUBLIC, addr, NULL);
    snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
             addr[5], addr[4], addr[3], addr[2], addr[1], addr[0]);
    printf("port: %s\n", hci_cdc_port());
    printf("own public addr: %s\n", buf);
    printf("host synced: %s\n", ble_hs_synced() ? "yes" : "no");
    return 0;
}

/* --------------------------------------------------------------- main ----*/

static void usage(void)
{
    fprintf(stderr,
        "usage: blectl [options] <command>\n"
        "\n"
        "commands:\n"
        "  boot                 run JSON boot sequence over HID\n"
        "  core-start           release reset and enter ROM download mode\n"
        "  core-reset           reset controller into ROM download mode\n"
        "  core-status          query controller state over HID\n"
        "  array                send configured --array NAME over HID\n"
        "  bypass / hci-enable  execute a controller control operation\n"
        "  check-config         validate JSON and print computed CRCs (offline)\n"
        "  info                 show bridge port and controller address\n"
        "  scan                 scan and print advertisers\n"
        "  probe                connect, discover GATT, NUS echo test\n"
        "\n"
        "options:\n"
        "  -p, --port PATH      serial port (default: detect N32 bridge USB ID)\n"
        "  -n, --name SUBSTR    filter/target by advertised name substring\n"
        "  -a, --addr AA:BB:..  filter/target by address\n"
        "  -t, --timeout SEC    scan/connect timeout (default 10)\n"
        "  -w, --write-hex HEX  bytes to write in probe (e.g. \"00 11 22\")\n"
        "      --bdaddr AA:..   set controller BD_ADDR via vendor 0xFC32\n"
        "      --config PATH    runtime boot JSON (default: build/bridge_boot.json)\n"
        "      --hid-path PATH  select HID device when multiple bridges exist\n"
        "      --array NAME     array name for the array command\n"
        "      --no-boot        skip boot before info/scan/probe\n"
        "  -v                   verbose NimBLE logs\n");
}

static void *host_thread_fn(void *arg)
{
    (void)arg;
    nimble_port_run();
    return NULL;
}

int main(int argc, char **argv)
{
    const char *port = NULL;
    int verbose = 0;
    int auto_boot = 1;
    const char *config = BLECTL_DEFAULT_CONFIG;
    const char *hid_path = NULL;
    const char *array_name = NULL;

    enum {
        OPT_BDADDR = 256,
        OPT_CONFIG,
        OPT_HID_PATH,
        OPT_ARRAY,
        OPT_NO_BOOT
    };
    static const struct option options[] = {
        { "port",      required_argument, NULL, 'p' },
        { "name",      required_argument, NULL, 'n' },
        { "addr",      required_argument, NULL, 'a' },
        { "timeout",   required_argument, NULL, 't' },
        { "write-hex", required_argument, NULL, 'w' },
        { "bdaddr",    required_argument, NULL, OPT_BDADDR },
        { "config",    required_argument, NULL, OPT_CONFIG },
        { "hid-path",  required_argument, NULL, OPT_HID_PATH },
        { "array",     required_argument, NULL, OPT_ARRAY },
        { "no-boot",   no_argument,       NULL, OPT_NO_BOOT },
        { "help",      no_argument,       NULL, 'h' },
        { NULL, 0, NULL, 0 }
    };
    int option;
    while ((option = getopt_long(argc, argv, "p:n:a:t:w:vh", options, NULL)) != -1) {
        switch (option) {
        case 'p': port = optarg; break;
        case 'n': g_name_filter = optarg; break;
        case 'a': snprintf(g_addr_filter, sizeof(g_addr_filter), "%s", optarg); break;
        case 't': {
            char *end;
            long seconds = strtol(optarg, &end, 10);
            if (*optarg == '\0' || *end != '\0' || seconds < 1 || seconds > 3600) {
                fprintf(stderr, "blectl: timeout must be 1..3600 seconds\n");
                return 2;
            }
            g_scan_timeout = (int)seconds;
            break;
        }
        case 'w': g_write_hex = optarg; break;
        case 'v': verbose = 1; break;
        case 'h': usage(); return 0;
        case OPT_BDADDR: g_bdaddr = optarg; break;
        case OPT_CONFIG: config = optarg; break;
        case OPT_HID_PATH: hid_path = optarg; break;
        case OPT_ARRAY: array_name = optarg; break;
        case OPT_NO_BOOT: auto_boot = 0; break;
        default: usage(); return 2;
        }
    }
    if (argc - optind != 1) {
        usage();
        return 2;
    }
    g_cmd = argv[optind];

    const char *hid_commands[] = {
        "boot", "core-start", "core-reset", "core-status", "array",
        "bypass", "hci-enable", "check-config"
    };
    for (size_t i = 0; i < sizeof(hid_commands) / sizeof(hid_commands[0]); i++) {
        if (strcmp(g_cmd, hid_commands[i]) == 0) {
            return bridge_hid_run(g_cmd, config, hid_path, array_name);
        }
    }
    if (strcmp(g_cmd, "info") != 0 && strcmp(g_cmd, "scan") != 0 &&
        strcmp(g_cmd, "probe") != 0) {
        fprintf(stderr, "unknown command %s\n", g_cmd);
        return 2;
    }

    if (!port) {
        port = hci_cdc_autodetect_port();
        if (!port) {
            fprintf(stderr, "blectl: no unique N32 bridge CDC found (check USB or use --port)\n");
            return 1;
        }
    }

    if (auto_boot && bridge_hid_run("boot", config, hid_path, NULL) != 0) {
        return 1;
    }

    if (hci_cdc_open(port) != 0) {
        return 1;
    }

    if (!verbose) {
        /* keep stderr quiet unless asked */
    }

    ble_hs_cfg.sync_cb = on_sync;
    nimble_port_init();

    pthread_t host_thread;
    pthread_create(&host_thread, NULL, host_thread_fn, NULL);

    if (wait_sync(5000) != 0) {
        fprintf(stderr, "blectl: controller did not sync (is the bridge firmware running?)\n");
        return 1;
    }

    int rc = 1;
    if (strcmp(g_cmd, "info") == 0) rc = do_info();
    else if (strcmp(g_cmd, "scan") == 0) rc = do_scan();
    else if (strcmp(g_cmd, "probe") == 0) rc = do_probe();
    else { fprintf(stderr, "unknown command %s\n", g_cmd); usage(); rc = 2; }

    hci_cdc_close();
    return rc;
}
