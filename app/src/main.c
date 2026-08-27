#include <errno.h>

#include <zephyr/kernel.h>

#if CONFIG_KFSW_CSP
#include <kfsw/comms/csp.h>
#endif
#if CONFIG_KFSW_CSP_KISS_UART
#include <kfsw/comms/uart.h>
#endif
#if CONFIG_KFSW_STORAGE
#include <kfsw/platform/storage.h>
#endif
#include <kfsw/services/boot.h>
#include <kfsw/services/log.h>
#if CONFIG_KFSW_PARAM
#include <kfsw/services/parameter.h>
#endif

int main(void)
{
	int result;

	kfsw_log_info("K-FSW application starting");

#if CONFIG_KFSW_STORAGE
	result = kfsw_storage_init();
	if (result == 0) {
		result = kfsw_storage_mount();
	}
	if (result != 0) {
		kfsw_log_error("Failed to mount storage: %d", result);
	} else {
		kfsw_log_info("Storage mounted at %s", KFSW_STORAGE_MOUNT_POINT);
	}
#endif

#if CONFIG_KFSW_PARAM
	result = kfsw_param_init();

	if (result != 0) {
		kfsw_log_error("Failed to initialize parameters: %d", result);
	} else {
		kfsw_log_info("Parameter table initialized");
#if CONFIG_KFSW_PARAM_PERSISTENCE
		result = kfsw_param_persist_load();
		if (result == -ENOENT) {
			kfsw_log_info("No parameter snapshot; using compiled defaults");
		} else if (result != 0) {
			kfsw_log_error("Parameter snapshot restore failed (%d); using defaults",
				       result);
		} else {
			kfsw_log_info("Persistent parameters restored");
		}
#endif
	}
#endif

#if CONFIG_KFSW_CSP
	result = kfsw_csp_init();

	if (result != 0) {
		kfsw_log_error("Failed to initialize CSP: %d", result);
	} else {
		kfsw_log_info("CSP initialized as node %d", CONFIG_KFSW_CSP_ADDRESS);

#if CONFIG_KFSW_CSP_KISS_UART
		struct kfsw_uart_info uart_info;

		kfsw_uart_get_info(&uart_info);
		kfsw_log_info("CSP KISS UART initialized on %s at %u baud", uart_info.device_name,
			      uart_info.baudrate);
#endif

#if CONFIG_KFSW_PARAM
		result = kfsw_param_server_start();
		if (result != 0) {
			kfsw_log_error("Failed to start parameter server: %d", result);
		} else {
			kfsw_log_info("Parameter server started on CSP port %d",
				      CONFIG_KFSW_PARAM_PORT);
		}
#endif

		result = kfsw_csp_start();
		if (result != 0) {
			kfsw_log_error("Failed to start CSP router: %d", result);
		} else {
			kfsw_log_info("CSP router started");
#if CONFIG_KFSW_CSP_KISS_UART
			kfsw_log_info("CSP UART interface started");
#endif
		}
	}
#endif

	kfsw_boot_service_start();

	for (;;) {
		k_sleep(K_SECONDS(60));
	}

	return 0;
}
