#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/shell/shell.h>
#include <stdio.h>
#include <stdlib.h>
#include "bluetooth.h"
#include "msgq.h"

#define CMD_BUFF_LEN 20
char* note = NULL;

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
        shell_print(shell, "  tune t <EL|A|D|G|B|EH>");
        shell_print(shell, "  tune r");
        shell_print(shell, "  tune s");
        return -EINVAL;
    }

    const char *mode = argv[1];
    const char *note = argv[2];

    if (strncmp(mode, "t", 1) == 0) {
        if (strncmp(note, "EL", 2) == 0) {
            shell_print(shell, "Sending command over bluetooth to sense for EL tune...");
            send_messagef("t %s\n", note);
        } else if (strncmp(note, "A", 2) == 0) {
            shell_print(shell, "Sending command over bluetooth to sense for A tune...");
            send_messagef("t %s\n", note);
        } else if (strncmp(note, "D", 2) == 0) {
            shell_print(shell, "Sending command over bluetooth to sense for D tune...");
            send_messagef("t %s\n", note);
        } else if (strncmp(note, "G", 2) == 0) {
            shell_print(shell, "Sending command over bluetooth to sense for G tune...");
            send_messagef("t %s\n", note);
        } else if (strncmp(note, "B", 2) == 0) {
            shell_print(shell, "Sending command over bluetooth to sense for B tune...");
            send_messagef("t %s\n", note);
        } else if (strncmp(note, "EH", 2) == 0) {
            shell_print(shell, "Sending command over bluetooth to sense for EH tune...");
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
    bt_msg_t rx;                       /* message popped from k_msgq      */
    float     freq     = 0.0f;         /* numeric field                   */
    char      note[3]  = "";           /* two-char string + NUL           */

    printk("Main starting, launching BT thread\n");
    bluetooth_thread_start();

    while (1) {
        if (k_msgq_get(&bt_msgq, &rx, K_FOREVER) == 0) {
            char *ptr   = (char *)rx.data;
            char *endp;
            freq = strtof(ptr, &endp);                  /* converts “440.00”   */
                                                       /* returns ptr to space */

            while (isspace((unsigned char)*endp)) { ++endp; }

            size_t i = 0;
            while (i < 2 && *endp && !isspace((unsigned char)*endp)) {
                note[i++] = *endp++;
            }
            note[i] = '\0';
            if (i == 0) {
                printk("Malformed frame: '%.*s'\n", rx.len, rx.data);
                continue;
            }
            float filt = kalman_update(freq);
            printk("%.2f %s\n", filt, note);
        }
    }
    return 0;
}
