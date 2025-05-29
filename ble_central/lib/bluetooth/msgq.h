#ifndef MSGQ_H
#define MSGQ_H

#include <zephyr/kernel.h>

#define BLE_CHUNK_DATA_LEN 19    /* max raw bytes per notification */
#define MSGQ_MAX_MSGS      64    /* increase depth to avoid drops */
#define MSGQ_ALIGN         4

/* One BLE chunk: raw bytes + length */
typedef struct {
    uint8_t  data[BLE_CHUNK_DATA_LEN];
    uint16_t len;
} bt_msg_t;

/* Exported queue from main.c */
extern struct k_msgq bt_msgq;

#endif /* MSGQ_H */
