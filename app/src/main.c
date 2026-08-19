#include <zephyr/kernel.h>

#include <kfsw/services/boot.h>
#include <kfsw/services/log.h>

int main(void)
{
    kfsw_log_info("K-FSW application starting");
    kfsw_boot_service_start();

    for (;;) {
        k_sleep(K_SECONDS(60));
    }

    return 0;
}
