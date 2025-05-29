#include "bluetooth.h"

/* 128-bit Nordic UART Service (NUS) UUIDs */
#define BT_UUID_NUS_SERVICE_VAL   \
  BT_UUID_128_ENCODE(0x6e400001,0xb5a3,0xf393,0xe0a9,0xe50e24dcca9e)
#define BT_UUID_NUS_CHAR_RX_VAL   \
  BT_UUID_128_ENCODE(0x6e400002,0xb5a3,0xf393,0xe0a9,0xe50e24dcca9e)
#define BT_UUID_NUS_CHAR_TX_VAL   \
  BT_UUID_128_ENCODE(0x6e400003,0xb5a3,0xf393,0xe0a9,0xe50e24dcca9e)

char target_note[MAX_NOTE_LEN];
enum bt_mode current_mode = MODE_READ;

static ssize_t on_nus_rx(struct bt_conn *conn,
    const struct bt_gatt_attr *attr,
    const void *buf, uint16_t len,
    uint16_t offset, uint8_t flags){

    const char *in = (const char *)buf;
    if (len == 0){
        return len;
    }

    switch (in[0])
    {
        case 'r':{
            current_mode = MODE_READ;
            printk("BT: Mode = READ_ANY_FREQUENCY\n");
            break;
        }
        case 't':{
            /* Find how many chars until space, newline, or end */
            size_t note_len = strcspn(in + 1, " \r\n");

            /* Clamp to leave room for NUL */
            if (note_len >= MAX_NOTE_LEN) {
                note_len = MAX_NOTE_LEN - 1;
            }

            /* Copy the note substring (in[1..1+note_len)) */
            strncpy(target_note, in + 1, note_len);
            target_note[note_len] = '\0';

            if (note_len > 0) {
                current_mode = MODE_TUNE;
                printk("BT: MODE_TUNE, target_note='%s'\n", target_note);
            } else {
                printk("BT: T received but no note provided\n");
            }
            break;
        }
        default:{
            printk("BT: Unknown command '%c'\n", in[0]);
            break;
        }
    }
}

/* Notification CCC configuration changed callback */
static void tx_ccc_cfg_changed(const struct bt_gatt_attr *attr,
                               uint16_t value)
{
    uint16_t handle = bt_gatt_attr_get_handle(attr);
    bool enabled = (value == BT_GATT_CCC_NOTIFY);
    printk("Notifications %s (handle 0x%04x)\n",
           enabled ? "enabled" : "disabled",
           handle);
}

/* Define the NUS service */
BT_GATT_SERVICE_DEFINE(nus_svc,
    BT_GATT_PRIMARY_SERVICE(
        BT_UUID_DECLARE_128(BT_UUID_NUS_SERVICE_VAL)),

    /* RX characteristic: central -> peripheral */
    BT_GATT_CHARACTERISTIC(
        BT_UUID_DECLARE_128(BT_UUID_NUS_CHAR_RX_VAL),
        BT_GATT_CHRC_WRITE | BT_GATT_CHRC_WRITE_WITHOUT_RESP,
        BT_GATT_PERM_WRITE,
        NULL, on_nus_rx, NULL),

    /* TX characteristic: peripheral -> central */
    BT_GATT_CHARACTERISTIC(
        BT_UUID_DECLARE_128(BT_UUID_NUS_CHAR_TX_VAL),
        BT_GATT_CHRC_NOTIFY,
        BT_GATT_PERM_NONE,
        NULL, NULL, NULL),

    /* CCC for TX notifications */
    BT_GATT_CCC(tx_ccc_cfg_changed,
                BT_GATT_PERM_READ | BT_GATT_PERM_WRITE)
);

void init_bluetooth(void)
{
    int err = bt_enable(NULL);
    printk("bt_enable -> %d\n", err);

    const struct bt_data ad[] = {
        BT_DATA_BYTES(BT_DATA_FLAGS,
                      BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR),
        BT_DATA_BYTES(BT_DATA_UUID128_ALL,
                      BT_UUID_NUS_SERVICE_VAL)
    };

    err = bt_le_adv_start(BT_LE_ADV_CONN_NAME,
                          ad, ARRAY_SIZE(ad),
                          NULL, 0);
    printk("bt_le_adv_start -> %d\n", err);
}
