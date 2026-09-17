#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/shell/shell.h>

#include <kfsw/services/gndwdt.h>

static const char *state_name(const struct kfsw_gndwdt_status *status)
{
	if (!status->running) {
		return "stopped";
	}
	return status->enabled ? "armed" : "disarmed";
}

static int cmd_gndwdt_show(const struct shell *sh, size_t argc, char **argv)
{
	struct kfsw_gndwdt_status status;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	kfsw_gndwdt_get_status(&status);
	shell_print(sh, "state: %s", state_name(&status));
	shell_print(sh, "timeout_s: %u", status.timeout_s);
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

/* Records contact without a packet, so a bench can hold the countdown open. */
static int cmd_gndwdt_contact(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	kfsw_gndwdt_contact(0U);
	shell_print(sh, "Ground watchdog contact recorded");
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(gndwdt_commands,
	SHELL_CMD_ARG(contact, NULL, "Record contact now.", cmd_gndwdt_contact, 1, 0),
	SHELL_CMD_ARG(off, NULL, "Disarm the countdown.", cmd_gndwdt_arm, 1, 0),
	SHELL_CMD_ARG(on, NULL, "Arm the countdown.", cmd_gndwdt_arm, 1, 0),
	SHELL_CMD_ARG(show, NULL, "Show the countdown and its counters.", cmd_gndwdt_show, 1, 0),
	SHELL_CMD_ARG(timeout, NULL, "Set the silence allowed: timeout <seconds>.",
		      cmd_gndwdt_timeout, 2, 0),
	SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(gndwdt, &gndwdt_commands, "K-FSW ground watchdog.", NULL);
