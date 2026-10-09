#include <errno.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/util.h>
#include <zephyr/ztest.h>

#include <kfsw/services/remexec.h>

/*
 * A composition that never opted in. It has a shell command, and the service
 * must still refuse it, because nothing is marked.
 *
 * The refusals kfsw_remexec_init() itself applies are checked in the suite
 * setup: a rejected allowlist leaves the service uninitialised, so they have
 * to run before the empty list is adopted, and a test order is not something
 * to rely on.
 */

static int cmd_quiet(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(sh);
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	return 0;
}

SHELL_CMD_ARG_REGISTER(quiet, NULL, "Print nothing and succeed.", cmd_quiet, 1, 0);

static struct kfsw_remexec_reply reply;

static void check_init_refusals(void)
{
	static struct kfsw_remexec_entry too_many[CONFIG_KFSW_REMEXEC_MAX_ENTRIES + 1];
	static char over_long[KFSW_REMEXEC_COMMAND_MAX + 2U];
	static struct kfsw_remexec_entry long_entry = {.help = "too long"};
	static const struct kfsw_remexec_entry nameless[] = {{.command = NULL, .help = "none"}};
	static const struct kfsw_remexec_entry helpless[] = {{.command = "quiet", .help = NULL}};
	static const struct kfsw_remexec_entry empty_name[] = {{.command = "", .help = "h"}};
	static const struct kfsw_remexec_entry control[] = {
		{.command = "quiet\tdeeper", .help = "h"},
	};
	static const struct kfsw_remexec_entry duplicate[] = {
		{.command = "quiet", .help = "first"},
		{.command = "quiet", .help = "second"},
	};
	struct kfsw_remexec_allowlist list = {.count = 1U};

	for (size_t index = 0U; index < ARRAY_SIZE(too_many); index++) {
		too_many[index].command = "quiet";
		too_many[index].help = "one too many";
	}
	(void)memset(over_long, 'q', sizeof(over_long) - 1U);
	over_long[sizeof(over_long) - 1U] = '\0';
	long_entry.command = over_long;

	list.entries = too_many;
	list.count = ARRAY_SIZE(too_many);
	zassert_equal(kfsw_remexec_init(&list), -E2BIG);

	list.count = 1U;
	list.entries = &long_entry;
	zassert_equal(kfsw_remexec_init(&list), -ENAMETOOLONG);

	list.entries = nameless;
	zassert_equal(kfsw_remexec_init(&list), -EINVAL);

	list.entries = helpless;
	zassert_equal(kfsw_remexec_init(&list), -EINVAL);

	list.entries = empty_name;
	zassert_equal(kfsw_remexec_init(&list), -EINVAL);

	list.entries = control;
	zassert_equal(kfsw_remexec_init(&list), -EINVAL);

	list.entries = duplicate;
	list.count = ARRAY_SIZE(duplicate);
	zassert_equal(kfsw_remexec_init(&list), -EINVAL);

	list.entries = NULL;
	list.count = 1U;
	zassert_equal(kfsw_remexec_init(&list), -EINVAL);

	/* Every one of those left the service unarmed rather than half armed. */
	zassert_false(kfsw_remexec_is_initialized());
	kfsw_remexec_run_local("quiet", &reply);
	zassert_equal(reply.status, KFSW_REMEXEC_UNAVAILABLE);
}

static void *adopt_nothing(void)
{
	const struct kfsw_remexec_allowlist nothing = {.entries = NULL, .count = 0U};

	check_init_refusals();
	zassert_ok(kfsw_remexec_init(&nothing), "an empty allowlist must be a valid composition");
	return NULL;
}

ZTEST(services_remexec_empty, test_an_empty_allowlist_offers_nothing)
{
	struct kfsw_remexec_stats stats;

	kfsw_remexec_get_stats(&stats);
	zassert_equal(stats.entries, 0U);

	kfsw_remexec_list_local("", &reply);
	zassert_equal(reply.status, KFSW_REMEXEC_OK, "an empty listing is not an error");
	zassert_equal(reply.size, 0U);
	zassert_equal(reply.produced, 0U);
	zassert_false(reply.truncated);
}

ZTEST(services_remexec_empty, test_an_empty_allowlist_refuses_a_command_that_exists)
{
	struct kfsw_remexec_stats before;
	struct kfsw_remexec_stats after;

	kfsw_remexec_get_stats(&before);

	/* The command is registered with the shell; it is simply not offered. */
	kfsw_remexec_run_local("quiet", &reply);
	zassert_equal(reply.status, KFSW_REMEXEC_NOT_OFFERED);
	zassert_not_null(strstr(reply.text, "'quiet' is not offered for remote execution"),
			 "reason was: %s", reply.text);

	kfsw_remexec_list_local("quiet", &reply);
	zassert_equal(reply.status, KFSW_REMEXEC_NOT_OFFERED);

	kfsw_remexec_get_stats(&after);
	zassert_equal(after.accepted, before.accepted,
		      "nothing may run on a node that marked nothing");
	zassert_equal(after.refused, before.refused + 2U);
}

ZTEST_SUITE(services_remexec_empty, NULL, adopt_nothing, NULL, NULL, NULL);
