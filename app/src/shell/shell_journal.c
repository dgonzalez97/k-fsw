#include <errno.h>
#include <stdint.h>

#include <zephyr/shell/shell.h>
#include <zephyr/shell/shell_string_conv.h>

#if CONFIG_KFSW_LOG_REMOTE
#include <kfsw/services/event.h>
#include <kfsw/services/log_remote.h>

#include "shell_remote.h"
#endif
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

#if CONFIG_KFSW_LOG_REMOTE
static bool print_event(const struct kfsw_journal_record *record, void *context)
{
	const struct shell *sh = context;
	char data[(KFSW_EVENT_MAX_PAYLOAD_SIZE * 2U) + 1U];

	for (size_t i = 0U; i < record->event.payload_size; i++) {
		(void)snprintk(&data[i * 2U], 3U, "%02x", record->event.payload[i]);
	}
	data[record->event.payload_size * 2U] = '\0';
	shell_print(sh, "seq=%llu boot=%llu utc=%lld%s src=%s id=%u sev=%u data=%s",
		    (unsigned long long)record->sequence, (unsigned long long)record->boot,
		    (long long)record->utc_seconds, record->utc_valid ? "" : "?",
		    kfsw_event_source_name((enum kfsw_event_source)record->event.source),
		    record->event.id, record->event.severity, data);
	return true;
}

static int cmd_journal_remote(const struct shell *sh, size_t argc, char **argv)
{
	static const struct kfsw_log_remote_visitor visitor = {.event = print_event};
	unsigned long count = KFSW_LOG_HISTORY_MAX_READ;
	int parse_error = 0;
	uint16_t node;
	int result = kfsw_shell_parse_node(sh, argv[1], &node);

	if (result != 0) {
		return result;
	}
	if (argc > 2U) {
		count = shell_strtoul(argv[2], 10, &parse_error);
	}
	if ((parse_error != 0) || (count == 0U) || (count > KFSW_LOG_HISTORY_MAX_READ)) {
		shell_error(sh, "Usage: journal remote <node> [count 1..%u]",
			    KFSW_LOG_HISTORY_MAX_READ);
		return -EINVAL;
	}
	shell_print(sh, "node: %u", node);
	result = kfsw_log_remote_read(node, KFSW_LOG_REMOTE_JOURNAL, (uint16_t)count, 0U, &visitor,
				      (void *)sh);
	if (result == -ENOTSUP) {
		shell_error(sh, "journal remote: node %u keeps no journal", node);
		return result;
	}
	if (result == -EIO) {
		shell_error(sh, "journal remote: node %u cut the read short", node);
		return result;
	}
	return (result == 0) ? 0 : kfsw_shell_remote_failed(sh, "journal remote", node, result);
}
#endif

#if CONFIG_KFSW_COMMAND_CSP
#define NODE_ARGS 1
#else
#define NODE_ARGS 0
#endif

SHELL_STATIC_SUBCMD_SET_CREATE(
	journal_commands,
#if CONFIG_KFSW_LOG_REMOTE
	SHELL_CMD_ARG(remote, NULL, "Download another node's newest records: remote <node> [count].",
		      cmd_journal_remote, 2, 1),
#endif
	SHELL_CMD_ARG(stats, NULL, "Journal state, here or on a node: stats [node].",
		      cmd_journal_stats, 1, NODE_ARGS),
	SHELL_CMD_ARG(tail, NULL, "One saved event by age, newest 0: tail [node] <age>.",
		      cmd_journal_tail, 2, NODE_ARGS),
	SHELL_CMD_ARG(time, NULL, "When a saved event happened: time [node] <age>.",
		      cmd_journal_time, 2, NODE_ARGS),
	SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(journal, &journal_commands, "Persistent event journal.", NULL);
