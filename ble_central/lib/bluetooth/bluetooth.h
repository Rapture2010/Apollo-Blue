#ifndef BLUETOOTH_H
#define BLUETOOTH_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/** Send a NUL-terminated string to the peripheral’s RX characteristic */
void send_message(const char *msg);

#ifdef __cplusplus
}
#endif

#endif /* CENTRAL_BLUETOOTH_H */