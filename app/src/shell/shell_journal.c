#include <errno.h>
#include <stdint.h>

#include <zephyr/shell/shell.h>

#include "shell_command.h"

/*
 * The persistent journal, read through its commands so the same reply comes
 * back here and from another node.
 */

/* argv is "<name> [node]" or "<name> [node] <age>"; extra is the age count. */
static int run(const struct shell *sh, const char *command, size_t argc, char **argv, size_t extra)
{
	uint16_t node = KFSW_SHELL_THIS_NODE;

	if (argc == (2U + extra)) {
		int result = kfsw_shell_parse_node(sh, argv[1], &node);

		if (result != 0) {
			return result;
		}
	}
	return kfsw_shell_run_command(sh, node, command, extra, &argv[argc - extra]);
}

static int cmd_journal_stats(const struct shell *sh, size_t argc, char **argv)
{
	return run(sh, "journal_stats", argc, argv, 0U);
}

static int cmd_journal_tail(const struct shell *sh, size_t argc, char **argv)
{
	return run(sh, "journal_tail", argc, argv, 1U);
}

static int cmd_journal_time(const struct shell *sh, size_t argc, char **argv)
{
	return run(sh, "journal_time", argc, argv, 1U);
}

#if CONFIG_KFSW_COMMAND_CSP
#define NODE_ARGS 1
#else
#define NODE_ARGS 0
#endif

SHELL_STATIC_SUBCMD_SET_CREATE(
	journal_commands,
	SHELL_CMD_ARG(stats, NULL, "Journal state, here or on a node: stats [node].",
		      cmd_journal_stats, 1, NODE_ARGS),
	SHELL_CMD_ARG(tail, NULL, "One saved event by age, newest 0: tail [node] <age>.",
		      cmd_journal_tail, 2, NODE_ARGS),
	SHELL_CMD_ARG(time, NULL, "When a saved event happened: time [node] <age>.",
		      cmd_journal_time, 2, NODE_ARGS),
	SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(journal, &journal_commands, "Persistent event journal.", NULL);
