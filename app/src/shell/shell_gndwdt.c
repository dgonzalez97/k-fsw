#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/shell/shell.h>

#include <kfsw/services/gndwdt.h>

#include "shell_command.h"

static const char *state_name(const struct kfsw_gndwdt_status *status)
{
	if (!status->running) {
		return "stopped";
	}
	return status->enabled ? "armed" : "disarmed";
}

#if CONFIG_KFSW_COMMAND_CSP
static int remote(const struct shell *sh, const char *node_text, bool feed)
{
	struct kfsw_command_result result;
	uint16_t node;
	int outcome;

	outcome = kfsw_shell_parse_node(sh, node_text, &node);
	if (outcome != 0) {
		return outcome;
	}
	outcome = kfsw_gndwdt_remote(node, feed, &result);
	if (outcome == -EINVAL) {
		shell_error(sh, "Node %u is this node; its watchdog is fed from elsewhere", node);
		return outcome;
	}
	if (outcome != 0) {
		shell_error(sh, "Node %u did not answer (%d)", node, outcome);
		return outcome;
	}
	if (result.status != KFSW_COMMAND_OK) {
		shell_error(sh, "Node %u refused: %s", node,
			    kfsw_command_status_name(result.status));
		return -EIO;
	}
	shell_print(sh, "node: %u", node);
	if (feed) {
		shell_print(sh, "fed: yes");
	}
	kfsw_shell_print_fields(sh, result.detail);
	return 0;
}

static int cmd_gndwdt_feed(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);

	return remote(sh, argv[1], true);
}
#endif

static int cmd_gndwdt_show(const struct shell *sh, size_t argc, char **argv)
{
	struct kfsw_gndwdt_status status;

#if CONFIG_KFSW_COMMAND_CSP
	if (argc == 2U) {
		return remote(sh, argv[1], false);
	}
#else
	ARG_UNUSED(argc);
#endif
	ARG_UNUSED(argv);

	kfsw_gndwdt_get_status(&status);
	shell_print(sh, "state: %s", state_name(&status));
	shell_print(sh, "ground_wtd_timeout: %u", status.timeout_s);
	shell_print(sh, "ground_wtd_cnt: %u", status.remaining_s);
	shell_print(sh, "since_contact_s: %u", status.since_contact_s);
	shell_print(sh, "contacts: %u last_node: %u", status.contacts, status.last_node);
	shell_print(sh, "expiries: %u", status.expiries);
	return 0;
}

static int cmd_gndwdt_arm(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);

	kfsw_gndwdt_set_enabled(strcmp(argv[0], "on") == 0);
	shell_print(sh, "Ground watchdog %s", argv[0]);
	return 0;
}

static int cmd_gndwdt_timeout(const struct shell *sh, size_t argc, char **argv)
{
	unsigned long seconds;
	char *end;
	int result;

	ARG_UNUSED(argc);

	seconds = strtoul(argv[1], &end, 0);
	if ((end == argv[1]) || (*end != '\0') || (seconds > UINT32_MAX)) {
		shell_error(sh, "Invalid timeout: %s", argv[1]);
		return -EINVAL;
	}

	result = kfsw_gndwdt_set_timeout_s((uint32_t)seconds);
	if (result != 0) {
		shell_error(sh, "Timeout refused: %d", result);
		return result;
	}

	shell_print(sh, "Ground watchdog timeout_s: %lu", seconds);
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(gndwdt_commands,
#if CONFIG_KFSW_COMMAND_CSP
	SHELL_CMD_ARG(feed, NULL, "Feed another node's ground watchdog: feed <node>.",
		      cmd_gndwdt_feed, 2, 0),
#endif
	SHELL_CMD_ARG(off, NULL, "Disarm this node's countdown.", cmd_gndwdt_arm, 1, 0),
	SHELL_CMD_ARG(on, NULL, "Arm this node's countdown.", cmd_gndwdt_arm, 1, 0),
#if CONFIG_KFSW_COMMAND_CSP
	SHELL_CMD_ARG(show, NULL, "Show the countdown, here or on another node: show [node].",
		      cmd_gndwdt_show, 1, 1),
#else
	SHELL_CMD_ARG(show, NULL, "Show the countdown and its counters.", cmd_gndwdt_show, 1, 0),
#endif
	SHELL_CMD_ARG(timeout, NULL, "Set the silence allowed: timeout <seconds>.",
		      cmd_gndwdt_timeout, 2, 0),
	SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(gndwdt, &gndwdt_commands, "K-FSW ground watchdog.", NULL);
