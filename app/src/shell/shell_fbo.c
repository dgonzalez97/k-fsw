#include <errno.h>
#include <stdint.h>

#include <zephyr/shell/shell.h>
#include <zephyr/sys/util.h>

#include <kfsw/services/fbo.h>

/* Thin, like every adapter here: parse, call the service, print. */

static int cmd_fbo_run(const struct shell *sh, size_t argc, char **argv)
{
	int result;

	ARG_UNUSED(argc);

	result = kfsw_fbo_run(argv[1]);
	if (result != 0) {
		shell_error(sh, "run %s: %d", argv[1], result);
		return result;
	}
	/* Started, not finished: the procedure runs on the service's own thread
	 * because a wait line blocks, so the answer here is that it was
	 * accepted. Watch it with `fbo status` or in the event record.
	 */
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
	shell_print(sh, "runs: %u", status.runs);
	shell_print(sh, "lines run: %u", status.lines_run);
	shell_print(sh, "lines failed: %u", status.lines_failed);
	shell_print(sh, "lines skipped: %u", status.lines_skipped);
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(
	fbo_commands,
	SHELL_CMD_ARG(run, NULL, "Carry out a procedure: run <name>.", cmd_fbo_run, 2, 0),
	SHELL_CMD_ARG(stop, NULL, "Ask the running procedure to stop.", cmd_fbo_stop, 1, 0),
	SHELL_CMD_ARG(status, NULL, "Show what a procedure has done.", cmd_fbo_status, 1, 0),
	SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(fbo, &fbo_commands, "File based operations: run a sequence from a file.", NULL);
