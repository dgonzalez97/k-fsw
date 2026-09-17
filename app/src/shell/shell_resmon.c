#include <errno.h>
#include <stdint.h>
#include <stdlib.h>

#include <zephyr/shell/shell.h>

#include <kfsw/services/resmon.h>

static void print_status(const struct shell *sh, const struct kfsw_resmon_status *status)
{
	shell_print(sh, "state: %s", status->running ? "sweeping" : "stopped");
	shell_print(sh, "threads: %u", status->threads);
	shell_print(sh, "last_used: %u%%", status->last_used_percent);
	shell_print(sh, "worst_used: %u%% on %s", status->worst_used_percent, status->worst_thread);
	shell_print(sh, "worst_free: %u of %u bytes", status->worst_unused_bytes,
		    status->worst_stack_bytes);
	shell_print(sh, "alert_at: %u%%", status->alert_percent);
	shell_print(sh, "sweeps: %u alerts: %u", status->sweeps, status->alerts);
}

static int cmd_resmon_show(const struct shell *sh, size_t argc, char **argv)
{
	struct kfsw_resmon_status status;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	kfsw_resmon_get_status(&status);
	print_status(sh, &status);
	return 0;
}

static int cmd_resmon_sample(const struct shell *sh, size_t argc, char **argv)
{
	struct kfsw_resmon_status status;
	int threads;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	threads = kfsw_resmon_sample();
	if (threads < 0) {
		shell_error(sh, "Sweep failed: %d", threads);
		return threads;
	}

	kfsw_resmon_get_status(&status);
	print_status(sh, &status);
	return 0;
}

static int cmd_resmon_alert(const struct shell *sh, size_t argc, char **argv)
{
	unsigned long percent;
	char *end;
	int result;

	ARG_UNUSED(argc);

	percent = strtoul(argv[1], &end, 0);
	if ((end == argv[1]) || (*end != '\0') || (percent > UINT32_MAX)) {
		shell_error(sh, "Invalid percentage: %s", argv[1]);
		return -EINVAL;
	}

	result = kfsw_resmon_set_alert_percent((uint32_t)percent);
	if (result != 0) {
		shell_error(sh, "Percentage refused: %d", result);
		return result;
	}

	shell_print(sh, "Resource monitor alert: %lu%%", percent);
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(resmon_commands,
	SHELL_CMD_ARG(alert, NULL, "Set the stack use that raises an event: alert <percent>.",
		      cmd_resmon_alert, 2, 0),
	SHELL_CMD_ARG(sample, NULL, "Sweep the threads now.", cmd_resmon_sample, 1, 0),
	SHELL_CMD_ARG(show, NULL, "Show what the last sweep found.", cmd_resmon_show, 1, 0),
	SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(resmon, &resmon_commands, "K-FSW resource monitor.", NULL);
