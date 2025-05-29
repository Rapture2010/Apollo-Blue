#ifndef BLUETOOTH_H
#define BLUETOOTH_H

#include <zephyr/types.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/net_buf.h>   /* for struct net_buf_simple */
#include <stdarg.h>
#include <stdio.h>    /* for vsnprintf */

#define BT_UUID_CUSTOM_SERVICE_VAL \
    BT_UUID_128_ENCODE(0x6e400001,0xb5a3,0xf393,0xe0a9,0xe50e24dcca9e)
#define BT_UUID_CUSTOM_CHAR_TX_VAL \
    BT_UUID_128_ENCODE(0x6e400003,0xb5a3,0xf393,0xe0a9,0xe50e24dcca9e)
#define BT_UUID_NUS_CHAR_RX_VAL \
    BT_UUID_128_ENCODE(0x6e400002,0xb5a3,0xf393,0xe0a9,0xe50e24dcca9e)

void bluetooth_thread_start(void);

uint8_t notify_func(struct bt_conn *conn,
                    struct bt_gatt_subscribe_params *params,
                    const void *data, uint16_t length);
uint8_t ccc_discover_func(struct bt_conn *conn,
                          const struct bt_gatt_attr *attr,
                          struct bt_gatt_discover_params *params);
uint8_t char_discover_func(struct bt_conn *conn,
                           const struct bt_gatt_attr *attr,
                           struct bt_gatt_discover_params *params);
uint8_t svc_discover_func(struct bt_conn *conn,
                          const struct bt_gatt_attr *attr,
                          struct bt_gatt_discover_params *params);
void connected_cb(struct bt_conn *conn, uint8_t err);
void disconnected_cb(struct bt_conn *conn, uint8_t reason);
void device_found(const bt_addr_le_t *addr,
                  int8_t rssi, uint8_t type,
                  struct net_buf_simple *ad);
void start_scan(void);
static uint8_t rx_discover_func(struct bt_conn *conn,
                                const struct bt_gatt_attr *attr,
                                struct bt_gatt_discover_params *params);
static void write_complete_cb(struct bt_conn *conn, 
                              uint8_t err,
                              struct bt_gatt_write_params *params);
void send_message(const char *msg);
void send_messagef(const char *fmt, ...);

#endif /* BLUETOOTH_H */
