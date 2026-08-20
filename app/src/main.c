#include <zephyr/kernel.h>

#if CONFIG_KFSW_CSP
#include <kfsw/comms/csp.h>
#endif
#include <kfsw/services/boot.h>
#include <kfsw/services/log.h>

int main(void)
{
    kfsw_log_info("K-FSW application starting");

#if CONFIG_KFSW_CSP
    int result = kfsw_csp_init();

    if (result != 0) {
        kfsw_log_error("Failed to initialize CSP: %d", result);
    } else {
        kfsw_log_info("CSP initialized as node %d",
                      CONFIG_KFSW_CSP_ADDRESS);

        result = kfsw_csp_start();
        if (result != 0) {
            kfsw_log_error("Failed to start CSP router: %d", result);
        } else {
            kfsw_log_info("CSP router started");
        }
    }
#endif

    kfsw_boot_service_start();

    for (;;) {
        k_sleep(K_SECONDS(60));
    }

    return 0;
}
