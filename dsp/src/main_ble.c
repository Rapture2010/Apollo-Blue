#include <zephyr/kernel.h>
#include "bluetooth.h"

int main(void)
{
    init_bluetooth();

    while (1) {
        k_sleep(K_SECONDS(1));
    }
    return 0;
}