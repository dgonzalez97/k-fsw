#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/shell/shell.h>
#include <zephyr/sys/util.h>

#include <kfsw/services/command.h>

/*
 * Reading the command registry, not running anything in it. Running stays with
 * each service's own shell group, as shell_command.c explains. This exists
 * because a node could not be asked what commands it carries, which is what
 * the interface control document is generated from.
 */

struct listing {
	const struct shell *sh;
	const char *wanted;
	/* Print the arguments and the help of every command, not just one. */
	bool verbose;
	unsigned int shown;
};

static const char *type_name(enum kfsw_command_type type)
{
	switch (type) {
	case KFSW_COMMAND_TYPE_U32:
		return "u32";
	case KFSW_COMMAND_TYPE_I32:
		return "i32";
	case KFSW_COMMAND_TYPE_TEXT:
		return "text";
	default:
		return "?";
	}
}

static void print_arguments(const struct shell *sh, const struct kfsw_command_info *info)
{
	char types[(KFSW_COMMAND_MAX_ARGS * 5U) + 1U];
	size_t used = 0U;

	types[0] = '\0';
	for (uint8_t index = 0U; index < info->arg_count; index++) {
		const char *name =
			(info->arg_types != NULL) ? type_name(info->arg_types[index]) : "?";
		int written = snprintf(&types[used], sizeof(types) - used, "%s%s",
				       (index > 0U) ? "," : "", name);

		if ((written <= 0) || ((size_t)written >= (sizeof(types) - used))) {
			break;
		}
		used += (size_t)written;
	}
	shell_print(sh, "  args: %u %s", info->arg_count, types);
}

static bool print_one(const struct kfsw_command_info *info, void *context)
{
	struct listing *listing = context;

	if ((listing->wanted != NULL) && (strcmp(listing->wanted, info->name) != 0)) {
		return true;
	}
	shell_print(listing->sh, "%u %s flags=0x%08x", info->id, info->name, info->flags);
	if (listing->verbose || (listing->wanted != NULL)) {
		print_arguments(listing->sh, info);
		shell_print(listing->sh, "  help: %s", (info->help != NULL) ? info->help : "none");
	}
	listing->shown++;
	return true;
}

static int cmd_registry_list(const struct shell *sh, size_t argc, char **argv)
{
	struct kfsw_command_stats stats;
	struct listing listing = {.sh = sh, .wanted = NULL, .shown = 0U};

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	kfsw_command_visit(print_one, &listing);
	if (kfsw_command_get_stats(&stats) == 0) {
		/* The count comes from the registry, so a listing cut short by a
		 * full output buffer is visible rather than silent. */
		shell_print(sh, "registered: %u shown: %u", stats.registered, listing.shown);
	}
	return 0;
}

static int cmd_registry_describe(const struct shell *sh, size_t argc, char **argv)
{
	struct listing listing = {.sh = sh, .wanted = NULL, .verbose = true, .shown = 0U};

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	kfsw_command_visit(print_one, &listing);
	shell_print(sh, "shown: %u", listing.shown);
	return 0;
}

static int cmd_registry_show(const struct shell *sh, size_t argc, char **argv)
{
	struct listing listing = {.sh = sh, .wanted = argv[1], .shown = 0U};

	ARG_UNUSED(argc);

	kfsw_command_visit(print_one, &listing);
	if (listing.shown == 0U) {
		shell_error(sh, "no command named %s", argv[1]);
		return -ENOENT;
	}
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(
	registry_commands,
	SHELL_CMD_ARG(list, NULL, "Every command this node carries.", cmd_registry_list, 1, 0),
	SHELL_CMD_ARG(describe, NULL, "Every command with its arguments and its help.",
		      cmd_registry_describe, 1, 0),
	SHELL_CMD_ARG(show, NULL, "One command with its arguments: show <name>.",
		      cmd_registry_show, 2, 0),
	SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(cmd, &registry_commands,
		   "Read the command registry. Running a command goes through its own group.",
		   NULL);
