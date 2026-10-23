#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/shell/shell.h>
#include <zephyr/shell/shell_string_conv.h>

#include <kfsw/comms/csp.h>
#include <kfsw/services/command.h>

#include "shell_command.h"
#if CONFIG_KFSW_COMMAND_CSP
#include "shell_remote.h"
#endif

/*
 * Shell side of the command registry. Each shell group that reaches a command,
 * here or on another node, goes through kfsw_shell_run_command(); there is no
 * generic "cmd" front end.
 */

static bool all_fields(const char *text)
{
	bool in_word = false;
	bool has_equals = false;

	for (const char *cursor = text; *cursor != '\0'; cursor++) {
		if (*cursor == ' ') {
			if (in_word && !has_equals) {
				return false;
			}
			in_word = false;
			has_equals = false;
			continue;
		}
		in_word = true;
		has_equals = has_equals || (*cursor == '=');
	}
	return !in_word || has_equals;
}

void kfsw_shell_print_fields(const struct shell *sh, const char *text)
{
	char fields[KFSW_COMMAND_MAX_DETAIL_SIZE];
	char *saved = NULL;

	if ((text[0] == '\0') || !all_fields(text)) {
		if (text[0] != '\0') {
			shell_print(sh, "%s", text);
		}
		return;
	}
	(void)strncpy(fields, text, sizeof(fields) - 1U);
	fields[sizeof(fields) - 1U] = '\0';
	for (char *field = strtok_r(fields, " ", &saved); field != NULL;
	     field = strtok_r(NULL, " ", &saved)) {
		char *value = strchr(field, '=');

		/* A text longer than the copy loses the end of its last field. */
		if (value == NULL) {
			shell_print(sh, "%s", field);
			continue;
		}
		*value = '\0';
		shell_print(sh, "%s: %s", field, value + 1);
	}
}

/* The service owns the conversion, so the shell and the CSP path cannot
   disagree about what a command argument means. This only says why. */
static int parse_argument(const struct shell *sh, const char *text, enum kfsw_command_type type,
			  struct kfsw_command_arg *arg)
{
	int outcome = kfsw_command_parse_arg(text, type, arg);

	switch (outcome) {
	case 0:
		break;
	case -ENAMETOOLONG:
		shell_error(sh, "argument is longer than %u bytes", KFSW_COMMAND_MAX_TEXT_SIZE);
		break;
	case -ENOTSUP:
		shell_error(sh, "argument type %d is not supported here", (int)type);
		break;
	default:
		shell_error(sh, "invalid argument '%s'", text);
		break;
	}
	return outcome;
}

/* Zero once the node answered, whatever it answered; the status is in result. */
static int invoke_remote(const struct shell *sh, uint16_t node, const char *name,
			 const struct kfsw_command_arg *args, size_t count, bool retry,
			 struct kfsw_command_result *result)
{
#if CONFIG_KFSW_COMMAND_CSP
	int outcome = retry ? kfsw_command_invoke_remote_retry(node, name, args, count, result)
			    : kfsw_command_invoke_remote(node, name, args, count, result);

	if (outcome != 0) {
		(void)kfsw_shell_remote_failed(sh, name, node, outcome);
	}
	return outcome;
#else
	ARG_UNUSED(node);
	ARG_UNUSED(name);
	ARG_UNUSED(args);
	ARG_UNUSED(count);
	ARG_UNUSED(retry);
	ARG_UNUSED(result);
	shell_error(sh, "Reaching another node needs CONFIG_KFSW_COMMAND_CSP");
	return -ENOTSUP;
#endif
}

int kfsw_shell_run_command(const struct shell *sh, uint16_t node, const char *name, size_t argc,
			   char **argv)
{
	struct kfsw_command_arg args[KFSW_COMMAND_MAX_ARGS];
	struct kfsw_command_result result = {0};
	struct kfsw_command_info info;
	bool retry = false;
	int outcome;

	if (!kfsw_command_is_initialized()) {
		shell_error(sh, "Command registry is not initialized");
		return -EACCES;
	}
	if ((node != KFSW_SHELL_THIS_NODE) && (argc > 0U) &&
	    (strcmp(argv[argc - 1U], "--retry") == 0)) {
		retry = true;
		argc--;
	}
	/* Both ends use the IDs of this build, so the name must be known here. */
	if (kfsw_command_find(name, &info) != 0) {
		shell_error(sh, "%s is not in this build", name);
		return -ENOENT;
	}
	if (argc != info.arg_count) {
		shell_error(sh, "%s takes %u argument(s), got %u", name, info.arg_count,
			    (unsigned int)argc);
		return -EINVAL;
	}
	for (size_t index = 0U; index < argc; index++) {
		outcome = parse_argument(sh, argv[index], info.arg_types[index], &args[index]);
		if (outcome != 0) {
			return outcome;
		}
	}

	if (node == KFSW_SHELL_THIS_NODE) {
		/* A failed handler reports through the result status. */
		(void)kfsw_command_invoke(name, args, argc, &result);
	} else {
		outcome = invoke_remote(sh, node, name, args, argc, retry, &result);
		if (outcome != 0) {
			return outcome;
		}
	}
	if (result.status != KFSW_COMMAND_OK) {
		shell_error(sh, "%s: %s%s%s", name, kfsw_command_status_name(result.status),
			    (result.detail[0] != '\0') ? ", " : "", result.detail);
		return -EIO;
	}
	if (node != KFSW_SHELL_THIS_NODE) {
		shell_print(sh, "node: %u", node);
	}
	kfsw_shell_print_fields(sh, result.detail);
	return 0;
}
