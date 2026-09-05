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
#if CONFIG_KFSW_WATCHDOG
#include <kfsw/platform/watchdog.h>
#endif
#if CONFIG_KFSW_RADIO_UHF
#include <kfsw/modules/radio_uhf.h>
#endif
#if CONFIG_KFSW_BOTON_TEST
#include <kfsw/modules/boton_test.h>
#endif
#include <kfsw/services/boot.h>
#if CONFIG_KFSW_COMMAND
#include <kfsw/services/command.h>

#include "commands/command_definitions.h"
#endif
#if CONFIG_KFSW_EVENT
#include <kfsw/services/event.h>
#endif
#if CONFIG_KFSW_FTP
#include <kfsw/services/ftp.h>
#endif
#if CONFIG_KFSW_FWU
#include <kfsw/services/fwu.h>
#endif
#if CONFIG_KFSW_FWU_LITE_CSP
#include <kfsw/services/fwu_lite.h>
#endif
#if CONFIG_KFSW_HEALTH
#include <kfsw/services/health.h>
#endif
/* Attributes this file's messages, so its level can be raised alone. */
#define KFSW_LOG_MODULE KFSW_LOG_MODULE_APP
#include <kfsw/services/log.h>
#if CONFIG_KFSW_PARAM
#include <kfsw/services/parameter.h>
#if CONFIG_KFSW_PARAM_TEST_DEFINITIONS
#include <kfsw/testing/parameter_definitions.h>
#endif

#include "parameters/tables.h"
#if CONFIG_KFSW_DEBUG_SHELL
#include "shell/shell_prompt.h"
#if CONFIG_KFSW_COMMAND
#include "shell/shell_echo.h"
#endif
#endif
#endif

int main(void)
{
#if CONFIG_KFSW_STORAGE || CONFIG_KFSW_PARAM || CONFIG_KFSW_CSP || CONFIG_KFSW_RADIO_UHF ||        \
	CONFIG_KFSW_BOTON_TEST || CONFIG_KFSW_COMMAND || CONFIG_KFSW_WATCHDOG
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
	/* Core tables first, then services, then modules: the same order the
	 * identifier bands are allocated in, so a listing reads in one
	 * direction whatever the composition is.
	 */
	const struct kfsw_param_definition_set *const parameter_sets[] = {
		&kfsw_board_param_definitions,      &kfsw_system_param_definitions,
		&kfsw_telemetry_param_definitions,
#if CONFIG_KFSW_CSP
		&kfsw_csp_param_definitions,
#endif
#if CONFIG_KFSW_STORAGE
		&kfsw_storage_param_definitions,
#endif
#if CONFIG_KFSW_WATCHDOG
		&kfsw_watchdog_param_definitions,
#endif
#if CONFIG_KFSW_PARAM_TEST_DEFINITIONS
		&kfsw_test_param_definitions,
#endif
		&kfsw_log_param_definitions,        &kfsw_param_param_definitions,
		&kfsw_boot_param_definitions,
#if CONFIG_KFSW_EVENT
		&kfsw_event_param_definitions,
#endif
#if CONFIG_KFSW_COMMAND
		&kfsw_command_param_definitions,
#endif
#if CONFIG_KFSW_FTP
		&kfsw_ftp_param_definitions,
#endif
#if CONFIG_KFSW_FWU
		&kfsw_fwu_param_definitions,
#endif
#if CONFIG_KFSW_HEALTH
		&kfsw_health_param_definitions,
#endif
#if CONFIG_KFSW_RADIO_UHF
		&kfsw_radio_uhf_param_definitions,
#endif
#if CONFIG_KFSW_BOTON_TEST
		&kfsw_boton_test_param_definitions,
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
		/* Applied here because this is the first point at which the
		 * stored value is known: storage is mounted, the snapshot has
		 * been restored, and nothing that talks to the outside has
		 * started yet.
		 */
		/* After the snapshot, so the count continues from what was
		 * stored rather than restarting at zero every boot. */
		kfsw_boot_count_restart();

		if (kfsw_system_boot_delay_ms() != 0U) {
			kfsw_log_info("Delaying service start by %u ms",
				      kfsw_system_boot_delay_ms());
			k_sleep(K_MSEC(kfsw_system_boot_delay_ms()));
		}
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

#if CONFIG_KFSW_COMMAND
	const struct kfsw_command_definition_set *const command_sets[] = {
		&kfsw_app_command_definitions,
	};

	result = kfsw_command_init(command_sets, ARRAY_SIZE(command_sets));
	if (result != 0) {
		kfsw_log_error("Failed to initialize commands: %d", result);
	} else {
		kfsw_log_info("Command registry initialized");
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

#if CONFIG_KFSW_FWU_LITE_CSP
	if (csp_started) {
		result = kfsw_fwu_lite_server_start();
		if (result != 0) {
			kfsw_log_error("Failed to start the firmware upload server: %d", result);
		} else {
			kfsw_log_info("Firmware upload server started on CSP port %d",
				      CONFIG_KFSW_FWU_LITE_CSP_PORT);
		}
	}
#endif

#if CONFIG_KFSW_COMMAND_CSP
	if (csp_started) {
		result = kfsw_command_server_start();
		if (result != 0) {
			kfsw_log_error("Failed to start command server: %d", result);
		} else {
			kfsw_log_info("Command server started on CSP port %d",
				      CONFIG_KFSW_COMMAND_CSP_PORT);
		}
	}
#endif
#endif

#if CONFIG_KFSW_WATCHDOG
	/* Armed last, once every service that could stall during start-up has
	 * finished. A watchdog that can reset the board before the shell comes
	 * up would make a slow boot indistinguishable from a hang, and would
	 * take away the console needed to diagnose it.
	 */
	result = kfsw_platform_watchdog_init();
	if (result == -ENODEV) {
		kfsw_log_info("No watchdog device bound; running unguarded");
	} else if (result != 0) {
		kfsw_log_error("Failed to initialize the watchdog: %d", result);
	} else if (IS_ENABLED(CONFIG_KFSW_WATCHDOG_AUTO_START)) {
		result = kfsw_platform_watchdog_start();
		if (result != 0) {
			kfsw_log_error("Failed to start the watchdog: %d", result);
		} else {
			kfsw_log_info("Watchdog armed with a %d ms timeout",
				      CONFIG_KFSW_WATCHDOG_TIMEOUT_MS);
		}
	}
#endif

#if CONFIG_KFSW_HEALTH
	/* The application thread watches itself first. It is the thread that
	 * would stop if the system seized, and watching one real thing is worth
	 * more than watching several that only look supervised. Services join
	 * as they gain something meaningful to report.
	 */
	uint8_t health_handle = 0U;
	bool health_watching = false;

	result = kfsw_health_register("app", CONFIG_KFSW_APP_HEALTH_DEADLINE_MS, &health_handle);
	if (result != 0) {
		kfsw_log_error("Failed to register the application for health: %d", result);
	} else {
		health_watching = true;
	}

	/* Started after the watchdog is armed: this takes the feeding over, and
	 * there is nothing to take over before then.
	 */
	result = kfsw_health_start();
	if (result != 0) {
		kfsw_log_error("Failed to start health monitoring: %d", result);
	}
#endif

#if CONFIG_KFSW_DEBUG_SHELL
	kfsw_shell_prompt_apply();
#if CONFIG_KFSW_COMMAND
	kfsw_shell_echo_apply();
#endif
#endif

	kfsw_boot_service_start();

	for (;;) {
#if CONFIG_KFSW_HEALTH
		if (health_watching) {
			(void)kfsw_health_report(health_handle);
		}
		/* Read every cycle so a change takes effect on the next one.
		 * The parameter is validated to stay well inside the deadline,
		 * so an ordinary scheduling delay is never mistaken for the
		 * thread having stopped.
		 */
#if CONFIG_KFSW_PARAM
		k_sleep(K_MSEC(kfsw_system_app_report_ms()));
#else
		k_sleep(K_MSEC(CONFIG_KFSW_APP_HEALTH_DEADLINE_MS / 4U));
#endif
#else
		k_sleep(K_SECONDS(60));
#endif
	}

	return 0;
}
