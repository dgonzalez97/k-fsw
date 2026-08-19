#include <zephyr/kernel.h>

#include <kfsw/services/boot.h>

int main(void)
{
    kfsw_boot_service_start();

    for (;;) {
        k_sleep(K_SECONDS(60));
    }

    return 0;
}
