#include <zephyr/shell/shell.h>

#include <kfsw/services/golden.h>

#include "shell_command.h"

static int cmd_golden_status(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(sh, "recovery image: %s", kfsw_golden_present() ? "present" : "absent");
	return 0;
}

#if CONFIG_KFSW_COMMAND
/* The pin is checked on the node that installs it, as with reboot. */
static int cmd_golden_install(const struct shell *sh, size_t argc, char **argv)
{
	uint16_t node = KFSW_SHELL_THIS_NODE;
	size_t pin = 1U;

	if (argc == 3U) {
		int result = kfsw_shell_parse_node(sh, argv[1], &node);

		if (result != 0) {
			return result;
		}
		pin = 2U;
	}
	return kfsw_shell_run_command(sh, node, "golden_install", 1U, &argv[pin]);
}
#endif

SHELL_STATIC_SUBCMD_SET_CREATE(
	golden_commands,
	SHELL_CMD_ARG(status, NULL, "Whether the golden partition holds an image",
		      cmd_golden_status, 1, 0),
#if CONFIG_KFSW_COMMAND
	SHELL_CMD_ARG(install, NULL, "Install it and restart: install [node] <pin>",
		      cmd_golden_install, 2, 1),
#endif
	SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(golden, &golden_commands, "Recovery image in the golden partition.", NULL);
