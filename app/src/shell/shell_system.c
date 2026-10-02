#include <zephyr/shell/shell.h>
#include <zephyr/sys/util.h>
#include <zephyr/version.h>

#include <kfsw/platform/time.h>
#include <kfsw/services/boot.h>
#define KFSW_LOG_MODULE KFSW_LOG_MODULE_APP
#include <kfsw/services/log.h>
#if CONFIG_KFSW_LOG_HISTORY
#include <kfsw/services/log_history.h>
#endif
#if CONFIG_KFSW_COMMAND
#include "shell_command.h"
#endif

static int cmd_status(const struct shell *sh, size_t argc, char **argv)
{
#if CONFIG_KFSW_COMMAND_CSP
	if (argc == 2U) {
		uint16_t node;
		int result = kfsw_shell_parse_node(sh, argv[1], &node);

		return (result != 0) ? result : kfsw_shell_run_command(sh, node, "info", 0U, NULL);
	}
#else
	ARG_UNUSED(argc);
#endif
	ARG_UNUSED(argv);

	shell_print(sh, "K-FSW status");
	shell_print(sh, "Role: %s", CONFIG_KFSW_ROLE);
	shell_print(sh, "Name: %s", CONFIG_KFSW_INSTANCE_NAME);
#if CONFIG_KFSW_CSP
	shell_print(sh, "CSP node: %d", CONFIG_KFSW_CSP_ADDRESS);
#endif
	shell_print(sh, "board: %s", CONFIG_BOARD_TARGET);
	shell_print(sh, "unit: %s", kfsw_boot_get_hardware_id());
	shell_print(sh, "uptime_ms: %llu", (unsigned long long)kfsw_time_monotonic_ms());

	return 0;
}

static int cmd_time(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(sh, "monotonic_ms: %llu", (unsigned long long)kfsw_time_monotonic_ms());
	shell_print(sh, "monotonic_us: %llu", (unsigned long long)kfsw_time_monotonic_us());

	return 0;
}

static int cmd_version(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(sh, "K-FSW: %s", kfsw_boot_get_image_version());
	shell_print(sh, "Revisions: %s", kfsw_boot_get_revisions());
	shell_print(sh, "Zephyr: %s", KERNEL_VERSION_STRING);
	shell_print(sh, "Board: %s", CONFIG_BOARD_TARGET);
	shell_print(sh, "SoC: %s", CONFIG_SOC);
	shell_print(sh, "Unit: %s", kfsw_boot_get_hardware_id());

	return 0;
}

static int cmd_log_test(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	ARG_UNUSED(sh);

	kfsw_log_error("K-FSW shell log test: error");
	kfsw_log_warning("K-FSW shell log test: warning");
	kfsw_log_info("K-FSW shell log test: info");
	kfsw_log_debug("K-FSW shell log test: debug");

	return 0;
}

#if CONFIG_KFSW_LOG_HISTORY
static int cmd_log_history(const struct shell *sh, size_t argc, char **argv)
{
	struct kfsw_log_history_window window;
	struct kfsw_log_record record;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	(void)kfsw_log_history_window(KFSW_LOG_HISTORY_MAX_READ, &window);
	shell_print(sh, "Log history: first=%llu end=%llu overwritten=%llu",
		    (unsigned long long)window.first, (unsigned long long)window.end,
		    (unsigned long long)window.overwritten);
	for (uint64_t sequence = window.first; sequence < window.end; sequence++) {
		int result = kfsw_log_history_get(sequence, &record);

		if (result != 0) {
			shell_error(sh, "Log history changed during read; retry");
			return result;
		}
		shell_print(sh, "%llu t=%llums %s level=%u%s %s",
			    (unsigned long long)record.sequence,
			    (unsigned long long)record.uptime_ms,
			    kfsw_log_module_name((enum kfsw_log_module)record.module),
			    record.severity, record.truncated ? " truncated" : "", record.text);
	}
	return 0;
}
#endif

SHELL_STATIC_SUBCMD_SET_CREATE(log_commands,
#if CONFIG_KFSW_LOG_HISTORY
	SHELL_CMD_ARG(history, NULL, "Read recent K-FSW log messages.", cmd_log_history, 1, 0),
#endif
	SHELL_CMD_ARG(test, NULL, "Exercise all K-FSW log levels.", cmd_log_test, 1, 0),
	SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(log, &log_commands, "K-FSW logging commands.", NULL);
#if CONFIG_KFSW_COMMAND_CSP
SHELL_CMD_ARG_REGISTER(status, NULL, "Show K-FSW status, here or on a node: status [node].",
		       cmd_status, 1, 1);
#else
SHELL_CMD_ARG_REGISTER(status, NULL, "Show basic K-FSW runtime status.", cmd_status, 1, 0);
#endif
SHELL_CMD_ARG_REGISTER(time, NULL, "Show K-FSW monotonic time.", cmd_time, 1, 0);
SHELL_CMD_ARG_REGISTER(version, NULL, "Show K-FSW build information.", cmd_version, 1, 0);

#if CONFIG_KFSW_COMMAND && CONFIG_REBOOT
static int cmd_reboot(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);

	return kfsw_shell_run_command(sh, KFSW_SHELL_THIS_NODE, "reboot", 1U, &argv[1]);
}

SHELL_CMD_ARG_REGISTER(reboot, NULL, "Restart this node: reboot <pin>; csp reboot for another.",
		       cmd_reboot, 2, 0);
#endif
