#include <errno.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/shell/shell.h>
#include <zephyr/shell/shell_string_conv.h>
#include <zephyr/sys/util.h>

#include <kfsw/services/command.h>

/*
 * Local front end onto the command registry. It parses text, converts it to
 * the argument types the definition declares, and calls the same entry point
 * the remote front end uses. It implements no command itself, so a shell
 * caller cannot bypass the validation a ground caller passes through.
 *
 * Registered command names appear as shell subcommands, so completion and
 * help work on them the way they do for any other command.
 */

struct command_index_search {
	size_t wanted;
	size_t seen;
	const struct kfsw_command_info *found;
	struct kfsw_command_info storage;
};

static bool select_by_index(const struct kfsw_command_info *info, void *context)
{
	struct command_index_search *search = context;

	if (search->seen == search->wanted) {
		search->storage = *info;
		search->found = &search->storage;
		return false;
	}
	search->seen++;
	return true;
}

static const struct kfsw_command_info *command_at(size_t index, struct command_index_search *search)
{
	search->wanted = index;
	search->seen = 0U;
	search->found = NULL;
	kfsw_command_visit(select_by_index, search);
	return search->found;
}

static bool print_command(const struct kfsw_command_info *info, void *context)
{
	const struct shell *sh = context;

	shell_print(sh, "%3u %-12s %u arg%s %s%s", info->id, info->name, info->arg_count,
		    (info->arg_count == 1U) ? " " : "s",
		    (info->flags & KFSW_COMMAND_FLAG_MUTATING) ? "[mutating] " : "",
		    (info->help != NULL) ? info->help : "");
	return true;
}

static int cmd_command_list(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	if (!kfsw_command_is_initialized()) {
		shell_error(sh, "Command registry is not initialized");
		return -EACCES;
	}
	shell_print(sh, " ID NAME         ARGS   DESCRIPTION");
	kfsw_command_visit(print_command, (void *)sh);
	return 0;
}

static int parse_argument(const struct shell *sh, const char *text, enum kfsw_command_type type,
			  struct kfsw_command_arg *arg)
{
	int parse_error = 0;

	arg->type = type;
	switch (type) {
	case KFSW_COMMAND_TYPE_U32:
		arg->value.u32 = (uint32_t)shell_strtoul(text, 0, &parse_error);
		break;
	case KFSW_COMMAND_TYPE_I32:
		arg->value.i32 = (int32_t)shell_strtol(text, 0, &parse_error);
		break;
	case KFSW_COMMAND_TYPE_TEXT:
		if (strnlen(text, KFSW_COMMAND_MAX_TEXT_SIZE + 1U) > KFSW_COMMAND_MAX_TEXT_SIZE) {
			shell_error(sh, "argument is longer than %u bytes",
				    KFSW_COMMAND_MAX_TEXT_SIZE);
			return -ENAMETOOLONG;
		}
		arg->value.text = text;
		break;
	default:
		return -ENOTSUP;
	}
	if (parse_error != 0) {
		shell_error(sh, "invalid numeric argument '%s'", text);
		return -EINVAL;
	}
	return 0;
}

static void print_result(const struct shell *sh, uint16_t node, const char *name,
			 const struct kfsw_command_result *result)
{
	const char *status = kfsw_command_status_name(result->status);

	if (result->status == KFSW_COMMAND_OK) {
		if (result->detail[0] != '\0') {
			shell_print(sh, "%s node=%u: OK %s", name, node, result->detail);
		} else {
			shell_print(sh, "%s node=%u: OK", name, node);
		}
		return;
	}
	if (result->detail[0] != '\0') {
		shell_error(sh, "%s node=%u: %s (%s)", name, node, status, result->detail);
	} else {
		shell_error(sh, "%s node=%u: %s", name, node, status);
	}
}

/* One path for both front-end forms; only the destination node differs. */
static int run_command(const struct shell *sh, uint16_t node, const char *name, size_t text_count,
		       char **text_args)
{
	struct kfsw_command_arg args[KFSW_COMMAND_MAX_ARGS];
	struct kfsw_command_result result;
	struct kfsw_command_info info;
	int outcome;

	if (!kfsw_command_is_initialized()) {
		shell_error(sh, "Command registry is not initialized");
		return -EACCES;
	}
	outcome = kfsw_command_find(name, &info);
	if (outcome != 0) {
		/* Asked anyway when it is meant for this node, so the service
		 * records the attempt. Refusing here without telling it left
		 * cmd_unknown counting only the mistakes that arrived over a
		 * link, which is the smaller half of them.
		 */
		if (node == 0U) {
			(void)kfsw_command_invoke(name, NULL, 0U, &result);
		}
		shell_error(sh, "unknown command '%s'; try 'cmd list'", name);
		return outcome;
	}
	if (text_count != info.arg_count) {
		shell_error(sh, "%s expects %u argument(s), got %u", name, info.arg_count,
			    (unsigned int)text_count);
		return -EINVAL;
	}
	for (size_t index = 0U; index < text_count; index++) {
		outcome = parse_argument(sh, text_args[index], info.arg_types[index], &args[index]);
		if (outcome != 0) {
			return outcome;
		}
	}

	if (node == 0U) {
		outcome = kfsw_command_invoke(name, args, text_count, &result);
	} else {
#if CONFIG_KFSW_COMMAND_CSP
		outcome = kfsw_command_invoke_remote(node, name, args, text_count, &result);
		if (outcome != 0) {
			shell_error(sh, "%s node=%u: transport failed (%d)", name, node, outcome);
			return outcome;
		}
#else
		shell_error(sh, "Remote commands need CONFIG_KFSW_COMMAND_CSP");
		return -ENOTSUP;
#endif
	}
	print_result(sh, node, name, &result);
	return (result.status == KFSW_COMMAND_OK) ? 0 : -EIO;
}

/* Reached when the command name matched a registered subcommand. */
static int cmd_command_named(const struct shell *sh, size_t argc, char **argv)
{
	return run_command(sh, 0U, argv[0], argc - 1U, &argv[1]);
}

/*
 * A leading numeric token selects a remote node, matching the existing
 * `param get [node] <name>` and `ftp <node> <op>` forms.
 */
static bool is_node_token(const char *text, uint16_t *node)
{
	unsigned long parsed;
	int parse_error = 0;

	if ((text[0] < '0') || (text[0] > '9')) {
		return false;
	}
	parsed = shell_strtoul(text, 10, &parse_error);
	if ((parse_error != 0) || (parsed > 16383U)) {
		return false;
	}
	*node = (uint16_t)parsed;
	return true;
}

/* Reached for the remote form and for anything that matched no subcommand. */
static int cmd_command_root(const struct shell *sh, size_t argc, char **argv)
{
	uint16_t node = 0U;

	if (!is_node_token(argv[1], &node)) {
		return run_command(sh, 0U, argv[1], argc - 2U, &argv[2]);
	}
	if (argc < 3U) {
		shell_error(sh, "Usage: cmd <node> <name> [arguments]");
		return -EINVAL;
	}
	return run_command(sh, node, argv[2], argc - 3U, &argv[3]);
}

/*
 * Entry 0 is `list`; the rest are the registered command names, so completion
 * and help offer exactly what the registry holds.
 */
static void command_name_get(size_t idx, struct shell_static_entry *entry)
{
	static struct command_index_search search;
	const struct kfsw_command_info *info;

	entry->handler = NULL;
	entry->subcmd = NULL;
	entry->syntax = NULL;
	entry->help = NULL;
	/* The name itself counts as one argument. */
	entry->args.mandatory = 1U;
	entry->args.optional = 0U;

	if (idx == 0U) {
		entry->syntax = "list";
		entry->help = "List registered commands.";
		entry->handler = cmd_command_list;
		return;
	}
	info = command_at(idx - 1U, &search);
	if (info == NULL) {
		return;
	}
	entry->syntax = info->name;
	entry->help = info->help;
	entry->handler = cmd_command_named;
	entry->args.mandatory = 1U + info->arg_count;
}

SHELL_DYNAMIC_CMD_CREATE(command_names, command_name_get);

SHELL_CMD_ARG_REGISTER(cmd, &command_names,
		       "Run a K-FSW command: cmd <name> [args] or cmd <node> <name> [args].",
		       cmd_command_root, 2, KFSW_COMMAND_MAX_ARGS + 1);
