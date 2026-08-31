#include <errno.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

#if CONFIG_KFSW_CSP
#include <kfsw/comms/csp.h>
#endif
#if CONFIG_KFSW_CSP_KISS_UART
#include <kfsw/comms/uart.h>
#endif
#if CONFIG_KFSW_STORAGE
#include <kfsw/platform/storage.h>
#endif
#if CONFIG_KFSW_RADIO_UHF
#include <kfsw/modules/radio_uhf.h>
#endif
#if CONFIG_KFSW_BOTON_TEST
#include <kfsw/modules/boton_test.h>
#endif
#include <kfsw/services/boot.h>
#if CONFIG_KFSW_FTP
#include <kfsw/services/ftp.h>
#endif
#include <kfsw/services/log.h>
#if CONFIG_KFSW_PARAM
#include <kfsw/services/parameter.h>
#if CONFIG_KFSW_PARAM_TEST_DEFINITIONS
#include <kfsw/testing/parameter_definitions.h>
#endif

#include "parameter_definitions.h"
#endif

int main(void)
{
#if CONFIG_KFSW_STORAGE || CONFIG_KFSW_PARAM || CONFIG_KFSW_CSP || CONFIG_KFSW_RADIO_UHF || \
	CONFIG_KFSW_BOTON_TEST
	int result;
#endif

	kfsw_log_info("K-FSW application starting");

#if CONFIG_KFSW_RADIO_UHF
	struct kfsw_radio_uhf_info radio_info;

	result = kfsw_radio_uhf_get_info(&radio_info);
	if (result != 0) {
		kfsw_log_error("Failed to read UHF radio composition: %d", result);
	} else {
		kfsw_log_info("UHF radio selected: %s; expected serial %u baud",
			      radio_info.implementation, radio_info.expected_serial_baud);
	}
#endif

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
	const struct kfsw_param_definition_set *const parameter_sets[] = {
		&kfsw_app_param_definitions,
		&kfsw_log_param_definitions,
#if CONFIG_KFSW_BOTON_TEST
		&kfsw_boton_test_param_definitions,
#endif
#if CONFIG_KFSW_PARAM_TEST_DEFINITIONS
		&kfsw_test_param_definitions,
#endif
	};

	result = kfsw_param_init(parameter_sets, ARRAY_SIZE(parameter_sets));

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

#if CONFIG_KFSW_BOTON_TEST
	result = kfsw_boton_test_init();
	if (result != 0) {
		kfsw_log_error("Failed to initialize boton_test: %d", result);
	} else {
		kfsw_log_info("boton_test initialized");
	}
#endif

#if CONFIG_KFSW_CSP
	bool csp_started = false;

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

#if CONFIG_KFSW_PARAM_CSP
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
			csp_started = true;
			kfsw_log_info("CSP router started");
#if CONFIG_KFSW_CSP_KISS_UART
			kfsw_log_info("CSP UART interface started");
#endif
		}
	}

#if CONFIG_KFSW_FTP
	if (csp_started) {
		result = kfsw_ftp_init();
		if (result == 0) {
			result = kfsw_ftp_start();
		}
		if (result != 0) {
			kfsw_log_error("Failed to start FTP service: %d", result);
		} else {
			kfsw_log_info("FTP service started on CSP port %d",
				      CONFIG_KFSW_FTP_CSP_PORT);
		}
	}
#endif
#endif

	kfsw_boot_service_start();

	for (;;) {
		k_sleep(K_SECONDS(60));
	}

	return 0;
}
