#include <zephyr/shell/shell.h>
#include <zephyr/sys/util.h>
#include <zephyr/version.h>

#include <kfsw/platform/time.h>
/* Attributes this file's messages, so its level can be raised alone. */
#define KFSW_LOG_MODULE KFSW_LOG_MODULE_APP
#include <kfsw/services/log.h>

static int cmd_status(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(sh, "K-FSW status");
	shell_print(sh, "Role: %s", CONFIG_KFSW_ROLE);
	shell_print(sh, "Name: %s", CONFIG_KFSW_INSTANCE_NAME);
#if CONFIG_KFSW_CSP
	shell_print(sh, "CSP node: %d", CONFIG_KFSW_CSP_ADDRESS);
#endif
	shell_print(sh, "board: %s", CONFIG_BOARD_TARGET);
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

	shell_print(sh, "K-FSW: kfsw-dev");
	shell_print(sh, "Zephyr: %s", KERNEL_VERSION_STRING);
	shell_print(sh, "Board: %s", CONFIG_BOARD_TARGET);

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

SHELL_STATIC_SUBCMD_SET_CREATE(log_commands,
	SHELL_CMD_ARG(test, NULL, "Exercise all K-FSW log levels.", cmd_log_test, 1, 0),
	SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(log, &log_commands, "K-FSW logging commands.", NULL);
SHELL_CMD_ARG_REGISTER(status, NULL, "Show basic K-FSW runtime status.", cmd_status, 1, 0);
SHELL_CMD_ARG_REGISTER(time, NULL, "Show K-FSW monotonic time.", cmd_time, 1, 0);
SHELL_CMD_ARG_REGISTER(version, NULL, "Show K-FSW build information.", cmd_version, 1, 0);
