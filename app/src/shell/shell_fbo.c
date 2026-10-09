#include <errno.h>
#include <stdint.h>
#include <stdlib.h>

#include <zephyr/shell/shell.h>
#include <zephyr/sys/util.h>

#include <kfsw/services/command.h>
#include <kfsw/services/fbo.h>

static const char *entry_status(uint8_t status)
{
	switch (status) {
	case KFSW_FBO_ENTRY_SCHEDULED:
		return "scheduled";
	case KFSW_FBO_ENTRY_RUNNING:
		return "running";
	case KFSW_FBO_ENTRY_COMPLETED:
		return "completed";
	case KFSW_FBO_ENTRY_FAILED:
		return "failed";
	case KFSW_FBO_ENTRY_OVERDUE:
		return "overdue";
	case KFSW_FBO_ENTRY_CANCELLED:
		return "cancelled";
	default:
		return "unknown";
	}
}

/*
 * Adding and cancelling go through the registered command, not through the
 * service, so the one place that reads a deadline is the command handler the
 * ground reaches over CSP. The shell only supplies the arguments.
 */
static int invoke(const struct shell *sh, const char *name, const struct kfsw_command_arg *args,
		  size_t count)
{
	struct kfsw_command_result result = {0};
	int outcome = kfsw_command_invoke(name, args, count, &result);

	if (result.detail[0] != '\0') {
		shell_print(sh, "%s: %s (%s)", name, kfsw_command_status_name(result.status),
			    result.detail);
	} else {
		shell_print(sh, "%s: %s", name, kfsw_command_status_name(result.status));
	}
	return outcome;
}

static int cmd_fbo_sched_add(const struct shell *sh, size_t argc, char **argv)
{
	struct kfsw_command_arg args[3];
	unsigned long node;
	char *end;

	ARG_UNUSED(argc);

	node = strtoul(argv[2], &end, 10);
	if ((*end != '\0') || (node > UINT16_MAX)) {
		shell_error(sh, "node must be a number, 0 for this one");
		return -EINVAL;
	}
	args[0].type = KFSW_COMMAND_TYPE_TEXT;
	args[0].value.text = argv[1];
	args[1].type = KFSW_COMMAND_TYPE_U32;
	args[1].value.u32 = (uint32_t)node;
	args[2].type = KFSW_COMMAND_TYPE_TEXT;
	args[2].value.text = argv[3];
	return invoke(sh, "fbo_sched_add", args, ARRAY_SIZE(args));
}

static int cmd_fbo_sched_cancel(const struct shell *sh, size_t argc, char **argv)
{
	struct kfsw_command_arg args[1];
	unsigned long index;
	char *end;

	ARG_UNUSED(argc);

	index = strtoul(argv[1], &end, 10);
	if ((*end != '\0') || (index > UINT16_MAX)) {
		shell_error(sh, "index must be a number");
		return -EINVAL;
	}
	args[0].type = KFSW_COMMAND_TYPE_U32;
	args[0].value.u32 = (uint32_t)index;
	return invoke(sh, "fbo_sched_cancel", args, ARRAY_SIZE(args));
}

static int cmd_fbo_sched_clear(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	return invoke(sh, "fbo_sched_clear", NULL, 0U);
}

static int cmd_fbo_sched_list(const struct shell *sh, size_t argc, char **argv)
{
	struct kfsw_fbo_schedule_status status;
	unsigned int shown = 0U;
	int result;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	result = kfsw_fbo_schedule_get_status(&status);
	if (result != 0) {
		shell_error(sh, "queue unavailable (%d)", result);
		return result;
	}
	for (uint16_t index = 0U; index < status.capacity; index++) {
		struct kfsw_fbo_schedule_entry entry;

		if (kfsw_fbo_schedule_get_entry(index, &entry) != 0) {
			continue;
		}
		if (entry.kind == KFSW_FBO_TIME_ABSOLUTE) {
			shell_print(sh, "%u @%lld node=%u %s %s", entry.index,
				    (long long)entry.due_utc, entry.node,
				    entry_status(entry.status), entry.line);
		} else {
			shell_print(sh, "%u +%us in=%us node=%u %s %s", entry.index, entry.delay_s,
				    entry.remaining_s, entry.node, entry_status(entry.status),
				    entry.line);
		}
		shown++;
	}
	if (shown == 0U) {
		shell_print(sh, "the queue is empty");
	}
	return 0;
}

static int cmd_fbo_sched_status(const struct shell *sh, size_t argc, char **argv)
{
	struct kfsw_fbo_schedule_status status;
	int result;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	result = kfsw_fbo_schedule_get_status(&status);
	if (result != 0) {
		shell_error(sh, "queue unavailable (%d)", result);
		return result;
	}
	shell_print(sh, "entries: %u/%u", status.entries, status.capacity);
	shell_print(sh, "scheduled: %u", status.scheduled);
	shell_print(sh, "running: %u", status.running);
	shell_print(sh, "completed: %u", status.completed);
	shell_print(sh, "failed: %u", status.failed);
	shell_print(sh, "overdue: %u", status.overdue);
	shell_print(sh, "cancelled: %u", status.cancelled);
	shell_print(sh, "clock set: %s", status.clock_set ? "yes" : "no");
	shell_print(sh, "latency: %u s", status.latency_s);
	shell_print(sh, "next relative: %u s", status.next_due_s);
	shell_print(sh, "next absolute: %lld", (long long)status.next_due_utc);
	shell_print(sh, "releases: %u", status.releases);
	shell_print(sh, "refusals: %u", status.refusals);
	shell_print(sh, "overdues: %u", status.overdues);
	shell_print(sh, "clock steps: %u", status.clock_steps);
	shell_print(sh, "queue: 0x%08x", status.hash);
	shell_print(sh, "last error: %d", status.last_error);
	return 0;
}

static int cmd_fbo_run(const struct shell *sh, size_t argc, char **argv)
{
	int result;

	ARG_UNUSED(argc);

	result = kfsw_fbo_run(argv[1]);
	if (result != 0) {
		shell_error(sh, "run %s: %d", argv[1], result);
		return result;
	}
	/* The procedure runs on the service thread; this only starts it. */
	shell_print(sh, "%s started", argv[1]);
	return 0;
}

static int cmd_fbo_stop(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	(void)kfsw_fbo_stop();
	shell_print(sh, "stop requested");
	return 0;
}

static int cmd_fbo_status(const struct shell *sh, size_t argc, char **argv)
{
	struct kfsw_fbo_status status;
	int result;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	result = kfsw_fbo_get_status(&status);
	if (result != 0) {
		shell_error(sh, "status unavailable (%d)", result);
		return result;
	}
	shell_print(sh, "procedure: %s", (status.name[0] != '\0') ? status.name : "none");
	shell_print(sh, "running: %s", status.running ? "yes" : "no");
	shell_print(sh, "line: %u", status.line);
	shell_print(sh, "last result: %d", status.last_result);
	shell_print(sh, "runs: %u", status.runs);
	shell_print(sh, "lines run: %u", status.lines_run);
	shell_print(sh, "lines failed: %u", status.lines_failed);
	shell_print(sh, "lines skipped: %u", status.lines_skipped);
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(
	fbo_sched_commands,
	SHELL_CMD_ARG(add, NULL, "Release a command later: add <+s|@utc> <node> \"<command>\".",
		      cmd_fbo_sched_add, 4, 0),
	SHELL_CMD_ARG(cancel, NULL, "Cancel a scheduled entry: cancel <index>.",
		      cmd_fbo_sched_cancel, 2, 0),
	SHELL_CMD_ARG(clear, NULL, "Empty the queue; a release in flight finishes.",
		      cmd_fbo_sched_clear, 1, 0),
	SHELL_CMD_ARG(list, NULL, "Show every queued entry.", cmd_fbo_sched_list, 1, 0),
	SHELL_CMD_ARG(status, NULL, "Show the queue counters.", cmd_fbo_sched_status, 1, 0),
	SHELL_SUBCMD_SET_END);

SHELL_STATIC_SUBCMD_SET_CREATE(
	fbo_commands,
	SHELL_CMD(sched, &fbo_sched_commands, "Time-tagged queue: release a command later.",
		  NULL),
	SHELL_CMD_ARG(run, NULL, "Carry out a procedure: run <name>.", cmd_fbo_run, 2, 0),
	SHELL_CMD_ARG(stop, NULL, "Ask the running procedure to stop.", cmd_fbo_stop, 1, 0),
	SHELL_CMD_ARG(status, NULL, "Show what a procedure has done.", cmd_fbo_status, 1, 0),
	SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(fbo, &fbo_commands, "File based operations: run a sequence from a file.", NULL);
