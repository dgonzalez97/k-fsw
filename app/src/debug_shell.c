#include <zephyr/shell/shell.h>
#include <zephyr/sys/util.h>
#include <zephyr/version.h>

#include <kfsw/platform/time.h>
#include <kfsw/services/log.h>

static int cmd_kfsw_status(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(sh, "K-FSW status");
	shell_print(sh, "board: %s", CONFIG_BOARD_TARGET);
	shell_print(sh, "uptime_ms: %llu",
		    (unsigned long long)kfsw_time_monotonic_ms());

	return 0;
}

static int cmd_kfsw_time(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(sh, "monotonic_ms: %llu",
		    (unsigned long long)kfsw_time_monotonic_ms());
	shell_print(sh, "monotonic_us: %llu",
		    (unsigned long long)kfsw_time_monotonic_us());

	return 0;
}

static int cmd_kfsw_version(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(sh, "K-FSW: kfsw-dev");
	shell_print(sh, "Zephyr: %s", KERNEL_VERSION_STRING);
	shell_print(sh, "Board: %s", CONFIG_BOARD_TARGET);

	return 0;
}

static int cmd_kfsw_log_test(const struct shell *sh, size_t argc, char **argv)
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

SHELL_STATIC_SUBCMD_SET_CREATE(kfsw_log_commands,
	SHELL_CMD_ARG(test, NULL, "Exercise all K-FSW log levels.",
		      cmd_kfsw_log_test, 1, 0),
	SHELL_SUBCMD_SET_END
);

SHELL_STATIC_SUBCMD_SET_CREATE(kfsw_commands,
	SHELL_CMD(log, &kfsw_log_commands, "K-FSW logging commands.", NULL),
	SHELL_CMD_ARG(status, NULL, "Show basic K-FSW runtime status.",
		      cmd_kfsw_status, 1, 0),
	SHELL_CMD_ARG(time, NULL, "Show K-FSW monotonic time.",
		      cmd_kfsw_time, 1, 0),
	SHELL_CMD_ARG(version, NULL, "Show K-FSW build information.",
		      cmd_kfsw_version, 1, 0),
	SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(kfsw, &kfsw_commands,
		   "K-FSW developer/debug commands.", NULL);
