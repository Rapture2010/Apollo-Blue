/* lib/bluetooth/bluetooth.c
 *
 * Central-side Nordic-UART helper (Thingy:52 peripheral)
 * ------------------------------------------------------
 * 2025-05-30 – fixes
 *   • Safe notify_func() – no buffer overrun
 *   • Correct descriptor discovery/subscription
 *   • Full braces on every switch-case body
 */

#include "bluetooth.h"
#include "msgq.h"

#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/att.h>
#include <zephyr/bluetooth/uuid.h>

#include <string.h>
#include <stdarg.h>
#include <stdio.h>

/* ────────────────────────────────────────────────────────────────
 *  Application-level message queue (defined in main.c)
 * ────────────────────────────────────────────────────────────── */
extern struct k_msgq bt_msgq;

/* ────────────────────────────────────────────────────────────────
 *  Single connection & discovery bookkeeping
 * ────────────────────────────────────────────────────────────── */
static struct bt_conn *default_conn;

static struct bt_gatt_discover_params  disc;
static struct bt_gatt_subscribe_params subscribe_params;

static uint16_t svc_start_handle;
static uint16_t svc_end_handle;
static uint16_t nus_rx_handle;
static uint16_t tx_handle;
static bool     discovery_complete;

/* forward decl so device_found() can restart scans on failure */
static void start_scan(void);

/* ────────────────────────────────────────────────────────────────
 *  Nordic UART Service UUIDs
 * ────────────────────────────────────────────────────────────── */
#define BT_UUID_NUS_SERVICE_VAL \
    BT_UUID_128_ENCODE(0x6e400001,0xb5a3,0xf393,0xe0a9,0xe50e24dcca9e)
#define BT_UUID_NUS_RX_VAL \
    BT_UUID_128_ENCODE(0x6e400002,0xb5a3,0xf393,0xe0a9,0xe50e24dcca9e)
#define BT_UUID_NUS_TX_VAL \
    BT_UUID_128_ENCODE(0x6e400003,0xb5a3,0xf393,0xe0a9,0xe50e24dcca9e)

static const struct bt_uuid_128 svc_uuid = BT_UUID_INIT_128(BT_UUID_NUS_SERVICE_VAL);
static const struct bt_uuid_128 rx_uuid  = BT_UUID_INIT_128(BT_UUID_NUS_RX_VAL);
static const struct bt_uuid_128 tx_uuid  = BT_UUID_INIT_128(BT_UUID_NUS_TX_VAL);

/* MAC address of the Thingy:52 (capital letters, colon-delimited) */
static const char *mac_mobile_node = "FD:26:10:55:4A:37";

/* Re-usable write-params (filled in send_message) */
static struct bt_gatt_write_params write_params;

/* ────────────────────────────────────────────────────────────────
 *  Thread for BT initialisation + scanning
 * ────────────────────────────────────────────────────────────── */
#define BT_STACK_SIZE 1024
#define BT_PRIORITY    7

K_THREAD_STACK_DEFINE(bt_stack, BT_STACK_SIZE);
static struct k_thread bt_thread_data;

/* ────────────────────────────────────────────────────────────────
 *  Notification handler – put data into bt_msgq
 * ────────────────────────────────────────────────────────────── */
static uint8_t notify_func(struct bt_conn *conn,
                           struct bt_gatt_subscribe_params *params,
                           const void *data, uint16_t length)
{
    if (!data || length == 0) {
        return BT_GATT_ITER_CONTINUE;
    }

    bt_msg_t msg;
    msg.len = MIN(length, BLE_CHUNK_DATA_LEN);
    memcpy(msg.data, data, msg.len);          /* no NUL terminator */

    if (k_msgq_put(&bt_msgq, &msg, K_NO_WAIT) != 0) {
        printk("BLE-MSGQ full, dropped notification\n");
    }

    return BT_GATT_ITER_CONTINUE;
}

/* ────────────────────────────────────────────────────────────────
 *  Write-completion debug helper
 * ────────────────────────────────────────────────────────────── */
static void write_complete_cb(struct bt_conn *conn,
                              uint8_t err,
                              struct bt_gatt_write_params *params)
{
    printk("bt_gatt_write %s (err=%u)\n",
           err ? "FAILED" : "OK", err);
}

/* ────────────────────────────────────────────────────────────────
 *  API called from main.c
 * ────────────────────────────────────────────────────────────── */
void send_message(const char *msg)
{
    if (!default_conn || !discovery_complete) {
        printk("send_message: link not ready\n");
        return;
    }

    write_params.handle = nus_rx_handle;
    write_params.offset = 0;
    write_params.data   = (const uint8_t *)msg;
    write_params.length = strlen(msg);
    write_params.func   = write_complete_cb;

    int err = bt_gatt_write(default_conn, &write_params);
    printk("bt_gatt_write -> %d\n", err);
}

void send_messagef(const char *fmt, ...)
{
    char buf[64];

    va_list ap;
    va_start(ap, fmt);
    int len = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    if (len <= 0) { return; }
    if (len >= sizeof(buf)) { buf[sizeof(buf)-1] = '\0'; }

    send_message(buf);
}

/* ────────────────────────────────────────────────────────────────
 *  Discovery callback: service → characteristics → descriptor
 * ────────────────────────────────────────────────────────────── */
static uint8_t discover_func(struct bt_conn *conn,
                             const struct bt_gatt_attr *attr,
                             struct bt_gatt_discover_params *params)
{
    /* ── Phase finished (attr == NULL) – kick off the next one ── */
    if (!attr) {
        switch (params->type) {

        case BT_GATT_DISCOVER_PRIMARY: {
            memset(&disc, 0, sizeof(disc));
            disc.uuid         = NULL;                        /* all chrcs */
            disc.start_handle = svc_start_handle + 1;
            disc.end_handle   = svc_end_handle;
            disc.type         = BT_GATT_DISCOVER_CHARACTERISTIC;
            disc.func         = discover_func;
            bt_gatt_discover(conn, &disc);
            break;
        }

        case BT_GATT_DISCOVER_CHARACTERISTIC: {
            if (!tx_handle) {
                printk("No NUS-TX found – discovery aborted\n");
                return BT_GATT_ITER_STOP;
            }
            memset(&disc, 0, sizeof(disc));
            disc.uuid         = NULL;                        /* all descr */
            disc.start_handle = tx_handle + 1;
            disc.end_handle   = svc_end_handle;
            disc.type         = BT_GATT_DISCOVER_DESCRIPTOR;
            disc.func         = discover_func;
            bt_gatt_discover(conn, &disc);
            break;
        }

        case BT_GATT_DISCOVER_DESCRIPTOR: {
            discovery_complete = true;
            printk("Discovery complete – RX=0x%04x "
                   "TX=0x%04x CCC=0x%04x\n",
                   nus_rx_handle, tx_handle,
                   subscribe_params.ccc_handle);
            break;
        }
        }
        return BT_GATT_ITER_STOP;
    }

    /* ── Process the attribute we just received ── */
    switch (params->type) {

    case BT_GATT_DISCOVER_PRIMARY: {
        const struct bt_gatt_service_val *sv = attr->user_data;
        svc_start_handle = attr->handle;
        svc_end_handle   = sv->end_handle;
        printk("Found NUS service 0x%04x–0x%04x\n",
               svc_start_handle, svc_end_handle);
        return BT_GATT_ITER_CONTINUE;
    }

    case BT_GATT_DISCOVER_CHARACTERISTIC: {
        const struct bt_gatt_chrc *chrc = attr->user_data;

        if (!bt_uuid_cmp(chrc->uuid, &rx_uuid.uuid)) {
            nus_rx_handle = chrc->value_handle;
            printk("Found NUS-RX @ 0x%04x\n", nus_rx_handle);
        }
        else if (!bt_uuid_cmp(chrc->uuid, &tx_uuid.uuid)) {
            tx_handle = chrc->value_handle;
            printk("Found NUS-TX @ 0x%04x (props=0x%02x)\n",
                   tx_handle, chrc->properties);

            memset(&subscribe_params, 0, sizeof(subscribe_params));
            subscribe_params.notify       = notify_func;
            subscribe_params.value_handle = tx_handle;
            subscribe_params.value        = BT_GATT_CCC_NOTIFY;
        }
        return BT_GATT_ITER_CONTINUE;
    }

    case BT_GATT_DISCOVER_DESCRIPTOR: {
        /* Only act on the 0x2902 CCC descriptor */
        if (!bt_uuid_cmp(attr->uuid, BT_UUID_GATT_CCC)) {
            subscribe_params.ccc_handle = attr->handle;
            int err = bt_gatt_subscribe(conn, &subscribe_params);
            printk("bt_gatt_subscribe -> %d (CCC=0x%04x)\n",
                   err, subscribe_params.ccc_handle);
        }
        /* keep iterating until attr == NULL so Zephyr writes 0x0001 */
        return BT_GATT_ITER_CONTINUE;
    }
    }

    return BT_GATT_ITER_STOP;
}

/* ────────────────────────────────────────────────────────────────
 *  Begin discovery with PRIMARY search
 * ────────────────────────────────────────────────────────────── */
static void start_discovery(struct bt_conn *conn)
{
    discovery_complete = false;
    nus_rx_handle      = 0;
    tx_handle          = 0;

    memset(&disc, 0, sizeof(disc));
    disc.uuid         = &svc_uuid.uuid;
    disc.start_handle = BT_ATT_FIRST_ATTRIBUTE_HANDLE;
    disc.end_handle   = BT_ATT_LAST_ATTRIBUTE_HANDLE;
    disc.type         = BT_GATT_DISCOVER_PRIMARY;
    disc.func         = discover_func;

    int err = bt_gatt_discover(conn, &disc);
    printk("bt_gatt_discover (PRIMARY) -> %d\n", err);
}

/* ────────────────────────────────────────────────────────────────
 *  Passive scanner – connect to the matching MAC
 * ────────────────────────────────────────────────────────────── */
static void device_found(const bt_addr_le_t *addr, int8_t rssi,
                         uint8_t type, struct net_buf_simple *ad)
{
    if (default_conn ||
        (type != BT_HCI_ADV_IND && type != BT_HCI_ADV_DIRECT_IND)) {
        return;                       /* already connected / not connectable */
    }

    char addr_str[BT_ADDR_LE_STR_LEN];
    bt_addr_le_to_str(addr, addr_str, sizeof(addr_str));

    if (strncmp(addr_str, mac_mobile_node,
                strlen(mac_mobile_node)) != 0) {
        return;                       /* not our peripheral */
    }

    printk("Found peripheral %s (RSSI %d), connecting…\n",
           addr_str, rssi);
    bt_le_scan_stop();

    int err = bt_conn_le_create(addr, BT_CONN_LE_CREATE_CONN,
                                BT_LE_CONN_PARAM_DEFAULT,
                                &default_conn);
    if (err) {
        printk("bt_conn_le_create failed (%d) – restarting scan\n", err);
        default_conn = NULL;
        start_scan();
    }
}

/* ────────────────────────────────────────────────────────────────
 *  Start or restart scanning
 * ────────────────────────────────────────────────────────────── */
static void start_scan(void)
{
    struct bt_le_scan_param param = {
        .type     = BT_LE_SCAN_TYPE_PASSIVE,
        .options  = BT_LE_SCAN_OPT_NONE,
        .interval = BT_GAP_SCAN_FAST_INTERVAL,
        .window   = BT_GAP_SCAN_FAST_WINDOW,
    };
    bt_le_scan_start(&param, device_found);
    printk("Scanning for Thingy:52…\n");
}

/* ────────────────────────────────────────────────────────────────
 *  Connection callbacks
 * ────────────────────────────────────────────────────────────── */
static void connected_cb(struct bt_conn *conn, uint8_t err)
{
    if (err) {
        printk("Connection failed (%u)\n", err);
        start_scan();
        return;
    }

    printk("Connected – starting discovery\n");
    default_conn = bt_conn_ref(conn);
    start_discovery(default_conn);
}

static void disconnected_cb(struct bt_conn *conn, uint8_t reason)
{
    printk("Disconnected (reason 0x%02x)\n", reason);

    if (default_conn) {
        bt_conn_unref(default_conn);
        default_conn = NULL;
    }
    start_scan();
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
    .connected    = connected_cb,
    .disconnected = disconnected_cb,
};

/* ────────────────────────────────────────────────────────────────
 *  BT enable + initial scan
 * ────────────────────────────────────────────────────────────── */
static void bt_thread(void *p1, void *p2, void *p3)
{
    if (bt_enable(NULL) != 0) {
        printk("bt_enable failed\n");
        return;
    }
    printk("Bluetooth controller ready\n");
    start_scan();
}

void bluetooth_thread_start(void)
{
    k_thread_create(&bt_thread_data, bt_stack, BT_STACK_SIZE,
                    bt_thread, NULL, NULL, NULL,
                    BT_PRIORITY, 0, K_NO_WAIT);
}
