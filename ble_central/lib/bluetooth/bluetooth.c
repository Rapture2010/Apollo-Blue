#include "bluetooth.h"
#include "msgq.h"
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>
#include <string.h>

extern struct k_msgq bt_msgq;
static struct bt_conn                   *default_conn;
static struct bt_gatt_subscribe_params   subscribe_params;
static struct bt_gatt_discover_params    svc_disc, char_disc, ccc_disc;
static uint16_t svc_end_handle;
static const char *mac_mobile_node = "FD:26:10:55:4A:37";

/* Thread setup */
#define BT_STACK_SIZE 1024
#define BT_PRIORITY    7

K_THREAD_STACK_DEFINE(bt_stack, BT_STACK_SIZE);
static struct k_thread bt_thread_data;

/* Handle of the peer’s NUS RX characteristic (0x6E400002…) */
static uint16_t nus_rx_handle;

/* Single bt_gatt_write_params to reuse for each send */
static struct bt_gatt_write_params write_params;

static void bt_thread(void *p1, void *p2, void *p3) {
    if (bt_enable(NULL)) {
        printk("bt_enable failed\n");
        return;
    }
    printk("BT enabled, starting scan\n");
    start_scan();
}

void bluetooth_thread_start(void) {
    k_thread_create(&bt_thread_data, bt_stack, BT_STACK_SIZE,
                    bt_thread, NULL, NULL, NULL,
                    BT_PRIORITY, 0, K_NO_WAIT);
}

static uint8_t rx_discover_func(struct bt_conn *conn,
                                const struct bt_gatt_attr *attr,
                                struct bt_gatt_discover_params *params)
{
    if (!attr) {
        printk("RX discovery complete\n");
        memset(params, 0, sizeof(*params));
        return BT_GATT_ITER_STOP;
    }

    const struct bt_gatt_chrc *chrc = attr->user_data;
    nus_rx_handle = chrc->value_handle;
    printk("Found NUS RX handle: 0x%04x\n", nus_rx_handle);

    return BT_GATT_ITER_STOP;
}

static void write_complete_cb(struct bt_conn *conn, 
                              uint8_t err,
                              struct bt_gatt_write_params *params)
{
    printk("GATT write %s (err %u)\n",
           err ? "failed" : "succeeded",
           err);
}

/* Notification callback: enqueue raw chunk */
uint8_t notify_func(struct bt_conn *conn,
                    struct bt_gatt_subscribe_params *params,
                    const void *data, uint16_t length)
{
    if (!data || length == 0) {
        return BT_GATT_ITER_CONTINUE;
    }
    bt_msg_t msg;
    msg.len = MIN(length, BLE_CHUNK_DATA_LEN);
    memcpy(msg.data, data, msg.len);

    /* Enqueue with short wait to avoid drops */
    if (k_msgq_put(&bt_msgq, &msg, K_MSEC(10)) != 0) {
        printk("MSGQ full, chunk dropped\n");
    }
    return BT_GATT_ITER_CONTINUE;
}

/* CCC descriptor discovery */
uint8_t ccc_discover_func(struct bt_conn *conn,
                          const struct bt_gatt_attr *attr,
                          struct bt_gatt_discover_params *params)
{
    if (!attr) {
        printk("CCC discover done\n");
        return BT_GATT_ITER_STOP;
    }
    subscribe_params.ccc_handle = attr->handle;
    bt_gatt_subscribe(conn, &subscribe_params);
    return BT_GATT_ITER_STOP;
}

/* Characteristic discovery */
uint8_t char_discover_func(struct bt_conn *conn,
                           const struct bt_gatt_attr *attr,
                           struct bt_gatt_discover_params *params)
{
    if (!attr) {
        return BT_GATT_ITER_STOP;
    }
    const struct bt_gatt_chrc *chrc = attr->user_data;
    if (!bt_uuid_cmp(chrc->uuid,
        BT_UUID_DECLARE_128(BT_UUID_CUSTOM_CHAR_TX_VAL))) {
        printk("Found TX char (0x%04x)\n", chrc->value_handle);
        subscribe_params.notify       = notify_func;
        subscribe_params.value_handle = chrc->value_handle;
        subscribe_params.ccc_handle   = 0;
        subscribe_params.value        = BT_GATT_CCC_NOTIFY;

        ccc_disc.uuid         = BT_UUID_GATT_CCC;
        ccc_disc.start_handle = chrc->value_handle + 1;
        ccc_disc.end_handle   = svc_end_handle;
        ccc_disc.type         = BT_GATT_DISCOVER_DESCRIPTOR;
        ccc_disc.func         = ccc_discover_func;
        bt_gatt_discover(conn, &ccc_disc);
        printk("Discovering CCC\n");
        return BT_GATT_ITER_STOP;
    }
    return BT_GATT_ITER_CONTINUE;
}

void send_message(const char *msg)
{
    if (!default_conn || !nus_rx_handle) {
        printk("Cannot send: no conn or RX handle\n");
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

    if (len <= 0) {
        return;
    }
    if (len >= sizeof(buf)) {
        buf[sizeof(buf)-1] = '\0';
        len = sizeof(buf)-1;
    }

    /* forward to your BLE‐write helper */
    send_message(buf);
}

/* Service discovery */
uint8_t svc_discover_func(struct bt_conn *conn,
                          const struct bt_gatt_attr *attr,
                          struct bt_gatt_discover_params *params)
{
    if (!attr) {
        printk("Service discover complete\n");
        return BT_GATT_ITER_STOP;
    }
    const struct bt_gatt_service_val *svc = attr->user_data;
    svc_end_handle = svc->end_handle;
    printk("Service 0x%04x–0x%04x\n", attr->handle, svc_end_handle);

    static struct bt_uuid_128 tx_uuid =
        BT_UUID_INIT_128(BT_UUID_CUSTOM_CHAR_TX_VAL);
    char_disc.uuid         = &tx_uuid.uuid;
    char_disc.start_handle = attr->handle + 1;
    char_disc.end_handle   = svc_end_handle;
    char_disc.type         = BT_GATT_DISCOVER_CHARACTERISTIC;
    char_disc.func         = char_discover_func;

    /* After setting up char_disc for TX… */
    bt_gatt_discover(conn, &char_disc);

    /* Now kick off RX discovery on the same service range */
    static struct bt_gatt_discover_params rx_disc_params;
    static struct bt_uuid_128 rx_uuid = BT_UUID_INIT_128(BT_UUID_NUS_CHAR_RX_VAL);

    rx_disc_params.uuid         = &rx_uuid.uuid;
    rx_disc_params.start_handle = attr->handle + 1;
    rx_disc_params.end_handle   = svc_end_handle;
    rx_disc_params.type         = BT_GATT_DISCOVER_CHARACTERISTIC;
    rx_disc_params.func         = rx_discover_func;

    bt_gatt_discover(conn, &rx_disc_params);

    return BT_GATT_ITER_STOP;
}

/* Connection callbacks */
void connected_cb(struct bt_conn *conn, uint8_t err) {
    if (err) {
        printk("Connect failed (err %u)\n", err);
        return;
    }
    default_conn = bt_conn_ref(conn);
    static struct bt_uuid_128 svc_uuid =
        BT_UUID_INIT_128(BT_UUID_CUSTOM_SERVICE_VAL);
    svc_disc.uuid         = &svc_uuid.uuid;
    svc_disc.start_handle = BT_ATT_FIRST_ATTRIBUTE_HANDLE;
    svc_disc.end_handle   = BT_ATT_LAST_ATTRIBUTE_HANDLE;
    svc_disc.type         = BT_GATT_DISCOVER_PRIMARY;
    svc_disc.func         = svc_discover_func;
    bt_gatt_discover(default_conn, &svc_disc);
}

void disconnected_cb(struct bt_conn *conn, uint8_t reason) {
    printk("Disconnected (reason %u)\n", reason);
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

/* Scan callback */
void device_found(const bt_addr_le_t *addr, int8_t rssi,
                  uint8_t type, struct net_buf_simple *ad)
{
    if (default_conn ||
        (type != BT_HCI_ADV_IND && type != BT_HCI_ADV_DIRECT_IND)) {
        return;
    }
    char addr_str[BT_ADDR_LE_STR_LEN];
    bt_addr_le_to_str(addr, addr_str, sizeof(addr_str));
    if (strncmp(addr_str, mac_mobile_node, strlen(mac_mobile_node))) {
        return;
    }
    printk("Found %s (RSSI %d)\n", addr_str, rssi);
    bt_le_scan_stop();
    bt_conn_le_create(addr, BT_CONN_LE_CREATE_CONN,
                      BT_LE_CONN_PARAM_DEFAULT, &default_conn);
}

/* Start scanning */
void start_scan(void) {
    struct bt_le_scan_param param = {
        .type     = BT_LE_SCAN_TYPE_PASSIVE,
        .options  = BT_LE_SCAN_OPT_NONE,
        .interval = BT_GAP_SCAN_FAST_INTERVAL,
        .window   = BT_GAP_SCAN_FAST_WINDOW,
    };
    bt_le_scan_start(&param, device_found);
    printk("Scanning...\n");
}
