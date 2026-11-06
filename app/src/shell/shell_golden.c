#include <zephyr/shell/shell.h>

#if CONFIG_KFSW_GOLDEN
#include <kfsw/services/golden.h>
#endif
#if CONFIG_KFSW_BOOT_STORE && CONFIG_KFSW_PARAM
#include <kfsw/services/parameter.h>
#endif

#include "shell_command.h"

#if CONFIG_KFSW_GOLDEN
static int cmd_golden_status(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(sh, "recovery image: %s", kfsw_golden_present() ? "present" : "absent");
	return 0;
}
#endif

#if CONFIG_KFSW_GOLDEN && CONFIG_KFSW_COMMAND
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

#if CONFIG_KFSW_BOOT_STORE && CONFIG_KFSW_PARAM
/* Writes boot_attempts_left and nothing else; the limit is a parameter write. */
static int cmd_golden_attempts(const struct shell *sh, size_t argc, char **argv)
{
	struct kfsw_param_value value = {.type = KFSW_PARAM_U8, .size = sizeof(uint8_t)};
	struct kfsw_param_value limit = {.type = KFSW_PARAM_U8, .size = sizeof(uint8_t)};
	int result;

	if (argc == 2U) {
		result = kfsw_param_parse_text(argv[1], &value);
		if (result == 0) {
			result = kfsw_param_set("boot_attempts_left", &value);
		}
	} else {
		result = kfsw_param_get("boot_attempts_left", &value);
	}
	if (result != 0) {
		shell_error(sh, "boot_attempts_left: %d", result);
		return result;
	}
	result = kfsw_param_get("boot_attempts_max", &limit);
	if (result != 0) {
		shell_error(sh, "boot_attempts_max: %d", result);
		return result;
	}
	shell_print(sh, "attempts left: %u of %u", value.scalar.u8, limit.scalar.u8);
	return 0;
}
#endif

SHELL_STATIC_SUBCMD_SET_CREATE(
	golden_commands,
#if CONFIG_KFSW_GOLDEN
	SHELL_CMD_ARG(status, NULL, "Whether the golden partition holds an image",
		      cmd_golden_status, 1, 0),
#endif
#if CONFIG_KFSW_GOLDEN && CONFIG_KFSW_COMMAND
	SHELL_CMD_ARG(install, NULL, "Install it and restart: install [node] <pin>",
		      cmd_golden_install, 2, 1),
#endif
#if CONFIG_KFSW_BOOT_STORE && CONFIG_KFSW_PARAM
	SHELL_CMD_ARG(attempts, NULL, "Startups left before recovery: attempts [count]",
		      cmd_golden_attempts, 1, 1),
#endif
	SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(golden, &golden_commands, "Recovery image and the startups left before it.",
		   NULL);
