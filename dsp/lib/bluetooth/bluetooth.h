#ifndef BLUETOOTH_H
#define BLUETOOTH_H

#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/sys/printk.h>

#define MAX_NOTE_LEN 4
extern char target_note[MAX_NOTE_LEN];

enum bt_mode{
    MODE_READ,
    MODE_TUNE
};

extern enum bt_mode current_mode;

extern struct bt_conn *current_conn;
extern const struct bt_gatt_attr *nus_tx_attr; 
void init_bluetooth(void);

#endif