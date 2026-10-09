#include <errno.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/util.h>
#include <zephyr/ztest.h>

#include <kfsw/services/remexec.h>

/*
 * The allowlist is the security boundary, so these tests check what it lets
 * through, what it refuses and with what reason, and that the output
 * accounting is exact at the cap and past it. Execution goes through the real
 * capture shell; the commands below exist only in this image.
 *
 * The refusals kfsw_remexec_init() itself applies are in
 * tests/unit/services_remexec_empty, because an adopted allowlist cannot be
 * replaced.
 */

/** Bytes per write, so one command can overrun the cap on purpose. */
#define CHATTY_CHUNK 16U

static int cmd_chatty(const struct shell *sh, size_t argc, char **argv)
{
	char chunk[CHATTY_CHUNK + 1U];
	unsigned long remaining;
	int parse_error = 0;

	ARG_UNUSED(argc);

	remaining = shell_strtoul(argv[1], 10, &parse_error);
	if (parse_error != 0) {
		return -EINVAL;
	}
	(void)memset(chunk, 'x', sizeof(chunk) - 1U);
	chunk[sizeof(chunk) - 1U] = '\0';
	while (remaining >= CHATTY_CHUNK) {
		shell_fprintf(sh, SHELL_NORMAL, "%s", chunk);
		remaining -= CHATTY_CHUNK;
	}
	if (remaining != 0U) {
		chunk[remaining] = '\0';
		shell_fprintf(sh, SHELL_NORMAL, "%s", chunk);
	}
	return 0;
}

static int cmd_grumpy(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_fprintf(sh, SHELL_NORMAL, "no");
	return -EIO;
}

static int cmd_quiet(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(sh);
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	return 0;
}

SHELL_CMD_ARG_REGISTER(chatty, NULL, "Print a given number of bytes.", cmd_chatty, 2, 0);
SHELL_CMD_ARG_REGISTER(grumpy, NULL, "Print and fail.", cmd_grumpy, 1, 0);
SHELL_CMD_ARG_REGISTER(quiet, NULL, "Print nothing and succeed.", cmd_quiet, 1, 0);
/* Takes anything and ignores it, so a command line can be padded to a length. */
SHELL_CMD_ARG_REGISTER(pad, NULL, "Ignore arguments and succeed.", cmd_quiet, 1,
		       SHELL_OPT_ARG_CHECK_SKIP);
SHELL_CMD_ARG_REGISTER(unmarked, NULL, "Exists but is not offered.", cmd_quiet, 1, 0);

static const struct kfsw_remexec_entry entries[] = {
	{.command = "chatty", .help = "Print a given number of bytes"},
	{.command = "grumpy", .help = "Print and fail"},
	{.command = "pad", .help = "Ignore arguments"},
	{.command = "quiet", .help = "Print nothing"},
	{.command = "quiet deeper", .help = "A subcommand of quiet"},
};

static const struct kfsw_remexec_allowlist allowlist = {
	.entries = entries,
	.count = ARRAY_SIZE(entries),
};

static struct kfsw_remexec_reply reply;

static void *adopt_allowlist(void)
{
	zassert_ok(kfsw_remexec_init(&allowlist), "the allowlist was refused");
	return NULL;
}

/** Fill @p line with @p command and pad it with zeros to @p length bytes. */
static void padded_line(char *line, size_t length, const char *command)
{
	(void)memset(line, '0', length);
	line[length] = '\0';
	(void)memcpy(line, command, strlen(command));
}

ZTEST(services_remexec, test_an_adopted_allowlist_cannot_be_replaced)
{
	static const struct kfsw_remexec_entry anything[] = {
		{.command = "unmarked", .help = "a command the first list left out"},
	};
	const struct kfsw_remexec_allowlist second = {.entries = anything, .count = 1U};

	zassert_true(kfsw_remexec_is_initialized());
	zassert_equal(kfsw_remexec_init(&second), -EALREADY);

	kfsw_remexec_run_local("unmarked", &reply);
	zassert_equal(reply.status, KFSW_REMEXEC_NOT_OFFERED);
}

ZTEST(services_remexec, test_listing_reports_exactly_the_marked_commands)
{
	kfsw_remexec_list_local("", &reply);
	zassert_equal(reply.status, KFSW_REMEXEC_OK);
	zassert_false(reply.truncated, "the listing did not fit: %u dropped", reply.dropped);
	zassert_not_null(strstr(reply.text, "chatty\n"));
	zassert_not_null(strstr(reply.text, "quiet deeper\n"));
	/* Registered with the shell but not marked, so discovery must not show it. */
	zassert_is_null(strstr(reply.text, "unmarked"));
}

ZTEST(services_remexec, test_listing_a_command_carries_its_subcommands_and_help)
{
	kfsw_remexec_list_local("quiet", &reply);
	zassert_equal(reply.status, KFSW_REMEXEC_OK);
	zassert_not_null(strstr(reply.text, "quiet\tPrint nothing\n"));
	zassert_not_null(strstr(reply.text, "quiet deeper\tA subcommand of quiet\n"));
	zassert_is_null(strstr(reply.text, "chatty"));
}

ZTEST(services_remexec, test_listing_an_unmarked_command_is_refused_by_name)
{
	kfsw_remexec_list_local("unmarked", &reply);
	zassert_equal(reply.status, KFSW_REMEXEC_NOT_OFFERED);
	zassert_not_null(strstr(reply.text, "'unmarked'"), "the reason omits the command: %s",
			 reply.text);
	zassert_not_null(strstr(reply.text, "not offered for remote execution"));
}

ZTEST(services_remexec, test_an_unmarked_command_is_refused_with_its_reason)
{
	struct kfsw_remexec_stats before;
	struct kfsw_remexec_stats after;

	kfsw_remexec_get_stats(&before);
	kfsw_remexec_run_local("unmarked", &reply);
	kfsw_remexec_get_stats(&after);

	zassert_equal(reply.status, KFSW_REMEXEC_NOT_OFFERED);
	zassert_not_null(strstr(reply.text, "'unmarked' is not offered for remote execution"),
			 "reason was: %s", reply.text);
	zassert_equal(after.refused, before.refused + 1U);
	zassert_equal(after.accepted, before.accepted, "a refused command must not be run");
	zassert_str_equal(kfsw_remexec_last_refusal(), reply.text);
}

ZTEST(services_remexec, test_a_marked_command_runs_and_reports_its_own_outcome)
{
	kfsw_remexec_run_local("quiet", &reply);
	zassert_equal(reply.status, KFSW_REMEXEC_OK);
	zassert_equal(reply.command_result, 0);
	zassert_equal(reply.produced, 0U);
	zassert_false(reply.truncated);

	/* The command failed; its output still fitted. The two are separate. */
	kfsw_remexec_run_local("grumpy", &reply);
	zassert_equal(reply.status, KFSW_REMEXEC_FAILED);
	zassert_equal(reply.command_result, -EIO);
	zassert_str_equal(reply.text, "no");
	zassert_false(reply.truncated);
}

ZTEST(services_remexec, test_arguments_reach_a_marked_command)
{
	kfsw_remexec_run_local("chatty 32", &reply);
	zassert_equal(reply.status, KFSW_REMEXEC_OK);
	zassert_equal(reply.produced, 32U);
	zassert_equal(reply.size, 32U);
	zassert_false(reply.truncated);
}

ZTEST(services_remexec, test_output_at_the_cap_is_whole)
{
	struct kfsw_remexec_stats before;
	struct kfsw_remexec_stats after;
	char line[KFSW_REMEXEC_COMMAND_MAX + 1U];

	(void)snprintk(line, sizeof(line), "chatty %u", (unsigned int)KFSW_REMEXEC_OUTPUT_MAX);
	kfsw_remexec_get_stats(&before);
	kfsw_remexec_run_local(line, &reply);
	kfsw_remexec_get_stats(&after);

	zassert_equal(reply.status, KFSW_REMEXEC_OK);
	zassert_equal(reply.produced, KFSW_REMEXEC_OUTPUT_MAX);
	zassert_equal(reply.size, KFSW_REMEXEC_OUTPUT_MAX);
	zassert_equal(reply.dropped, 0U);
	zassert_false(reply.truncated, "output exactly at the cap was called truncated");
	zassert_equal(after.truncated, before.truncated);
	zassert_equal(strlen(reply.text), KFSW_REMEXEC_OUTPUT_MAX);
}

ZTEST(services_remexec, test_output_past_the_cap_reports_the_dropped_count)
{
	struct kfsw_remexec_stats before;
	struct kfsw_remexec_stats after;
	char line[KFSW_REMEXEC_COMMAND_MAX + 1U];
	const size_t produced = KFSW_REMEXEC_OUTPUT_MAX + 64U;

	(void)snprintk(line, sizeof(line), "chatty %u", (unsigned int)produced);
	kfsw_remexec_get_stats(&before);
	kfsw_remexec_run_local(line, &reply);
	kfsw_remexec_get_stats(&after);

	zassert_equal(reply.status, KFSW_REMEXEC_OK, "truncation is not a command failure");
	zassert_true(reply.truncated);
	zassert_equal(reply.produced, produced);
	zassert_equal(reply.size, KFSW_REMEXEC_OUTPUT_MAX);
	zassert_equal(reply.dropped, 64U, "the dropped count must be exact");
	zassert_equal(after.truncated, before.truncated + 1U);
}

ZTEST(services_remexec, test_a_command_at_the_length_limit_is_accepted)
{
	char line[KFSW_REMEXEC_COMMAND_MAX + 1U];

	padded_line(line, KFSW_REMEXEC_COMMAND_MAX, "pad ");
	zassert_equal(strlen(line), KFSW_REMEXEC_COMMAND_MAX);

	kfsw_remexec_run_local(line, &reply);
	zassert_equal(reply.status, KFSW_REMEXEC_OK, "a command at the limit was refused: %s",
		      reply.text);
	zassert_equal(reply.produced, 0U);
}

ZTEST(services_remexec, test_a_command_past_the_length_limit_is_refused)
{
	struct kfsw_remexec_stats before;
	struct kfsw_remexec_stats after;
	char line[KFSW_REMEXEC_COMMAND_MAX + 2U];

	padded_line(line, KFSW_REMEXEC_COMMAND_MAX + 1U, "pad ");
	zassert_equal(strlen(line), KFSW_REMEXEC_COMMAND_MAX + 1U);

	kfsw_remexec_get_stats(&before);
	kfsw_remexec_run_local(line, &reply);
	kfsw_remexec_get_stats(&after);

	zassert_equal(reply.status, KFSW_REMEXEC_TOO_LONG);
	zassert_not_null(strstr(reply.text, "at most"), "reason was: %s", reply.text);
	zassert_equal(after.refused, before.refused + 1U);
	zassert_equal(after.accepted, before.accepted);
}

ZTEST(services_remexec, test_a_control_byte_and_an_empty_command_are_refused)
{
	kfsw_remexec_run_local("quiet\nunmarked", &reply);
	zassert_equal(reply.status, KFSW_REMEXEC_INVALID);
	zassert_not_null(strstr(reply.text, "not printable"));

	kfsw_remexec_run_local("", &reply);
	zassert_equal(reply.status, KFSW_REMEXEC_INVALID);

	kfsw_remexec_run_local(NULL, &reply);
	zassert_equal(reply.status, KFSW_REMEXEC_INVALID);
}

ZTEST(services_remexec, test_a_longer_name_sharing_a_marked_prefix_is_not_a_match)
{
	/* "quietly" starts with "quiet" but is a different command. */
	kfsw_remexec_run_local("quietly", &reply);
	zassert_equal(reply.status, KFSW_REMEXEC_NOT_OFFERED);
	zassert_not_null(strstr(reply.text, "'quietly'"));
}

ZTEST_SUITE(services_remexec, NULL, adopt_allowlist, NULL, NULL, NULL);
