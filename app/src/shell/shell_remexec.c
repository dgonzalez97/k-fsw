#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/shell/shell.h>
#include <zephyr/shell/shell_string_conv.h>
#include <zephyr/sys/util.h>

#include <kfsw/comms/csp.h>
#include <kfsw/services/remexec.h>

#include "shell_remote.h"

/*
 * Thin front end: parse, call the service, print. The allowlist decision, the
 * output cap and the wire format all belong to the serving node, so a refusal
 * printed here is the serving node's own words.
 */

/** Words a command line may be typed as, within CONFIG_SHELL_ARGC_MAX. */
#define REMEXEC_WORDS_MAX 8

static struct kfsw_remexec_reply reply;

static int parse_node(const struct shell *sh, const char *text, uint16_t *node)
{
	unsigned long parsed;
	int parse_error = 0;

	parsed = shell_strtoul(text, 10, &parse_error);
	if ((parse_error != 0) || (parsed == 0U) || (parsed >= KFSW_CSP_BROADCAST_ADDRESS)) {
		shell_error(sh, "Node must be 1..%u: %s", KFSW_CSP_BROADCAST_ADDRESS - 1U, text);
		return -EINVAL;
	}
	*node = (uint16_t)parsed;
	return 0;
}

/** Join the remaining words into one command line, refusing an over-long one. */
static int join_command(const struct shell *sh, size_t argc, char **argv, char *line,
			size_t capacity)
{
	size_t used = 0U;

	line[0] = '\0';
	for (size_t index = 0U; index < argc; index++) {
		size_t word = strlen(argv[index]);

		if ((used + word + ((index == 0U) ? 0U : 1U)) >= capacity) {
			shell_error(sh, "A command is at most %u bytes", KFSW_REMEXEC_COMMAND_MAX);
			return -ENAMETOOLONG;
		}
		if (index != 0U) {
			line[used] = ' ';
			used++;
		}
		memcpy(&line[used], argv[index], word);
		used += word;
		line[used] = '\0';
	}
	return 0;
}

/** Print the reply text a line at a time, splitting a name from its help. */
static void print_lines(const struct shell *sh, const char *text)
{
	const char *cursor = text;

	while (*cursor != '\0') {
		const char *end = strchr(cursor, '\n');
		size_t length = (end != NULL) ? (size_t)(end - cursor) : strlen(cursor);
		const char *tab = memchr(cursor, '\t', length);

		if (tab != NULL) {
			shell_print(sh, "%-24.*s %.*s", (int)(tab - cursor), cursor,
				    (int)(length - (size_t)(tab - cursor) - 1U), tab + 1);
		} else if (length != 0U) {
			shell_print(sh, "%.*s", (int)length, cursor);
		}
		if (end == NULL) {
			break;
		}
		cursor = end + 1;
	}
}

static int failed(const struct shell *sh, const char *what, uint16_t node, int result)
{
	if (result == -EINVAL) {
		shell_error(sh, "%s: node %u cannot be asked to run its own commands", what, node);
		return result;
	}
	return kfsw_shell_remote_failed(sh, what, node, result);
}

static int get_offered(const struct shell *sh, uint16_t node, size_t argc, char **argv)
{
	char prefix[KFSW_REMEXEC_COMMAND_MAX + 1U];
	int result = join_command(sh, argc, argv, prefix, sizeof(prefix));

	if (result != 0) {
		return result;
	}
	result = kfsw_remexec_list_remote(node, prefix, &reply);
	if (result != 0) {
		return failed(sh, "remexec get", node, result);
	}

	shell_print(sh, "node: %u", node);
	if (reply.status != KFSW_REMEXEC_OK) {
		shell_error(sh, "remexec: %s", reply.text);
		return -EACCES;
	}
	if (reply.size == 0U) {
		shell_print(sh, "no commands are offered for remote execution");
		return 0;
	}
	print_lines(sh, reply.text);
	if (reply.truncated) {
		shell_warn(sh, "listing truncated: %u byte(s) dropped; ask for a command",
			   (unsigned int)reply.dropped);
	}
	return 0;
}

static int run_offered(const struct shell *sh, uint16_t node, size_t argc, char **argv)
{
	char line[KFSW_REMEXEC_COMMAND_MAX + 1U];
	int result = join_command(sh, argc, argv, line, sizeof(line));

	if (result != 0) {
		return result;
	}
	if (line[0] == '\0') {
		shell_error(sh, "Usage: remexec <node> run \"command args\"");
		return -EINVAL;
	}
	result = kfsw_remexec_run_remote(node, line, &reply);
	if (result != 0) {
		return failed(sh, "remexec run", node, result);
	}

	shell_print(sh, "node: %u", node);
	shell_print(sh, "command: %s", line);
	if ((reply.status != KFSW_REMEXEC_OK) && (reply.status != KFSW_REMEXEC_FAILED)) {
		shell_error(sh, "remexec: %s", reply.text);
		return -EACCES;
	}

	/* The command's own outcome and whether its output fitted are reported
	 * separately: a command can succeed and still have output cut.
	 */
	if (reply.truncated) {
		shell_print(sh, "output: %u of %u bytes, truncated, %u dropped",
			    (unsigned int)reply.size, (unsigned int)reply.produced,
			    (unsigned int)reply.dropped);
	} else {
		shell_print(sh, "output: %u bytes", (unsigned int)reply.size);
	}
	print_lines(sh, reply.text);
	if (reply.status != KFSW_REMEXEC_OK) {
		shell_error(sh, "remexec: %s, the command returned %d",
			    kfsw_remexec_status_name(reply.status), reply.command_result);
		return -EIO;
	}
	shell_print(sh, "status: %s", kfsw_remexec_status_name(reply.status));
	return 0;
}

static int cmd_remexec(const struct shell *sh, size_t argc, char **argv)
{
	uint16_t node;
	int result = parse_node(sh, argv[1], &node);

	if (result != 0) {
		return result;
	}
	if (strcmp(argv[2], "get") == 0) {
		return get_offered(sh, node, argc - 3U, &argv[3]);
	}
	if (strcmp(argv[2], "run") == 0) {
		return run_offered(sh, node, argc - 3U, &argv[3]);
	}
	shell_error(sh, "Usage: remexec <node> get [command] | remexec <node> run \"command\"");
	return -EINVAL;
}

SHELL_CMD_ARG_REGISTER(remexec, NULL,
		       "Read and run what another node offers: remexec <node> get [command], "
		       "remexec <node> run \"command args\".",
		       cmd_remexec, 3, REMEXEC_WORDS_MAX);
