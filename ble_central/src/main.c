#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/shell/shell.h>
#include <stdio.h>
#include <stdlib.h>
#include "bluetooth.h"
#include "msgq.h"

#define CMD_BUFF_LEN 20

LOG_MODULE_REGISTER(tune_cmd, LOG_LEVEL_DBG);

/* Instantiate the queue (e.g. 64 entries, no drops) */
K_MSGQ_DEFINE(bt_msgq,
              sizeof(bt_msg_t),
              MSGQ_MAX_MSGS,
              MSGQ_ALIGN);

/* Kalman filter state (1-D) */
static float x_est = 0.0f;    // current estimate
static float p_est = 1.0f;    // estimate covariance
const float Q = 1e-3f;        // process noise variance
const float R = 1e-2f;        // measurement noise variance

/* One-dimensional Kalman update */
static float kalman_update(float z) {
    // Predict
    p_est += Q;
    // Gain
    float K = p_est / (p_est + R);
    // Update estimate
    x_est += K * (z - x_est);
    // Update covariance
    p_est *= (1.0f - K);
    return x_est;
}

static int tune_cmd(const struct shell *shell, size_t argc, char **argv)
{
    if (argc < 2 || argc > 3) {
        shell_print(shell, "Usage:");
        shell_print(shell, "  tune t <E2|A2|D3|G3|B3|E4>");
        shell_print(shell, "  tune r");
        shell_print(shell, "  tune s");
        return -EINVAL;
    }

    const char *mode = argv[1];
    const char *note = argv[2];

    if (strncmp(mode, "t", 1) == 0) {
        if (strncmp(note, "E2", 2) == 0) {
            shell_print(shell, "Sending command over bluetooth to sense for E2 tune...");
            send_messagef("t %s\n", note);
        } else if (strncmp(note, "A2", 2) == 0) {
            shell_print(shell, "Sending command over bluetooth to sense for A2 tune...");
            send_messagef("t %s\n", note);
        } else if (strncmp(note, "D3", 2) == 0) {
            shell_print(shell, "Sending command over bluetooth to sense for D3 tune...");
            send_messagef("t %s\n", note);
        } else if (strncmp(note, "G3", 2) == 0) {
            shell_print(shell, "Sending command over bluetooth to sense for G3 tune...");
            send_messagef("t %s\n", note);
        } else if (strncmp(note, "B3", 2) == 0) {
            shell_print(shell, "Sending command over bluetooth to sense for B3 tune...");
            send_messagef("t %s\n", note);
        } else if (strncmp(note, "E4", 2) == 0) {
            shell_print(shell, "Sending command over bluetooth to sense for E4 tune...");
            send_messagef("t %s\n", note);
        } else {
            shell_print(shell, "Unknown note: %s", note);
            return -EINVAL;
        }
    } else if (argc == 2 && strncmp(mode, "r", 1) == 0) {
        shell_print(shell, "Sending command to get a frequency reading...");
        send_messagef("r\n");
    } else if (argc == 2 && strncmp(mode, "s", 1) == 0) {
        shell_print(shell, "Sending command to stop frequency reading...");
        send_messagef("s\n");
    } else {
        shell_print(shell, "Usage:");
        shell_print(shell, "  tune t <EL|A|D|G|B|EH>");
        shell_print(shell, "  tune r");
        return -EINVAL;
    }
    return 0;
}

SHELL_CMD_REGISTER(tune, NULL, "Sets what needs to be tuned", tune_cmd);

int main(void)
{
    bt_msg_t rx;

    printk("Main starting, launching BT thread\n");
    bluetooth_thread_start();

    while (1) {
        /* Block until next BLE message arrives */
        if (k_msgq_get(&bt_msgq, &rx, K_FOREVER) == 0) {
            /* rx.data contains a NUL-terminated ASCII string */
            float meas = strtof((char *)rx.data, NULL);

            /* Apply Kalman filter */
            float filt = kalman_update(meas);

            /* Print raw and filtered values */
            printk("Meas: %.2f Hz, Filt: %.2f Hz\n", meas, filt);
        }
    }
    return 0;
}
