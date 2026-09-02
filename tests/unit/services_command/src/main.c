#include <errno.h>
#include <string.h>

#include <zephyr/sys/util.h>
#include <zephyr/ztest.h>

#include <kfsw/services/command.h>

/*
 * Registry behaviour. The wire codec is exercised through the integration
 * tests, where a real CSP peer supplies the bytes; these cases cover the
 * validation that must hold before any handler runs.
 */

static int last_arg_count;
static uint16_t last_source_node;
static uint32_t last_u32;

static int handler_ok(const struct kfsw_command_arg *args, size_t arg_count,
		      const struct kfsw_command_source *source, struct kfsw_command_result *result)
{
	last_arg_count = (int)arg_count;
	last_source_node = source->node;
	if ((arg_count > 0U) && (args[0].type == KFSW_COMMAND_TYPE_U32)) {
		last_u32 = args[0].value.u32;
	}
	result->status = KFSW_COMMAND_OK;
	strcpy(result->detail, "done");
	return 0;
}

static int handler_silent_failure(const struct kfsw_command_arg *args, size_t arg_count,
				  const struct kfsw_command_source *source,
				  struct kfsw_command_result *result)
{
	ARG_UNUSED(args);
	ARG_UNUSED(arg_count);
	ARG_UNUSED(source);
	ARG_UNUSED(result);

	/* Returns an error without setting a status; the service must fill one in. */
	return -EIO;
}

static bool count_command(const struct kfsw_command_info *info, void *context)
{
	ARG_UNUSED(info);
	(*(size_t *)context)++;
	return true;
}

static const enum kfsw_command_type one_u32[] = {KFSW_COMMAND_TYPE_U32};
static const enum kfsw_command_type one_text[] = {KFSW_COMMAND_TYPE_TEXT};

static const struct kfsw_command_definition valid_commands[] = {
	{.id = 1U, .name = "noop", .help = "none", .handler = handler_ok},
	{.id = 2U,
	 .name = "takes_u32",
	 .arg_count = 1U,
	 .arg_types = one_u32,
	 .handler = handler_ok},
	{.id = 3U,
	 .name = "takes_text",
	 .arg_count = 1U,
	 .arg_types = one_text,
	 .handler = handler_ok},
	{.id = 4U, .name = "breaks", .handler = handler_silent_failure},
};

static const struct kfsw_command_definition_set valid_set = {
	.commands = valid_commands,
	.count = ARRAY_SIZE(valid_commands),
};

static void *command_setup(void)
{
	const struct kfsw_command_definition_set *const sets[] = {&valid_set};

	zassert_ok(kfsw_command_init(sets, ARRAY_SIZE(sets)));
	return NULL;
}

ZTEST(services_command, test_registry_rejects_duplicates_and_bad_definitions)
{
	static const struct kfsw_command_definition duplicate_id[] = {
		{.id = 1U, .name = "a", .handler = handler_ok},
		{.id = 1U, .name = "b", .handler = handler_ok},
	};
	static const struct kfsw_command_definition duplicate_name[] = {
		{.id = 1U, .name = "a", .handler = handler_ok},
		{.id = 2U, .name = "a", .handler = handler_ok},
	};
	static const struct kfsw_command_definition no_handler[] = {
		{.id = 1U, .name = "a"},
	};
	static const struct kfsw_command_definition too_many_args[] = {
		{.id = 1U,
		 .name = "a",
		 .arg_count = KFSW_COMMAND_MAX_ARGS + 1U,
		 .arg_types = one_u32,
		 .handler = handler_ok},
	};
	static const struct kfsw_command_definition missing_types[] = {
		{.id = 1U, .name = "a", .arg_count = 1U, .handler = handler_ok},
	};
	const struct kfsw_command_definition_set sets[] = {
		{.commands = duplicate_id, .count = ARRAY_SIZE(duplicate_id)},
		{.commands = duplicate_name, .count = ARRAY_SIZE(duplicate_name)},
		{.commands = no_handler, .count = ARRAY_SIZE(no_handler)},
		{.commands = too_many_args, .count = ARRAY_SIZE(too_many_args)},
		{.commands = missing_types, .count = ARRAY_SIZE(missing_types)},
	};
	const int expected[] = {-EEXIST, -EEXIST, -EINVAL, -E2BIG, -EINVAL};

	for (size_t index = 0U; index < ARRAY_SIZE(sets); index++) {
		const struct kfsw_command_definition_set *const one[] = {&sets[index]};

		zassert_equal(kfsw_command_init(one, 1U), expected[index],
			      "set %zu was not rejected as expected", index);
		zassert_false(kfsw_command_is_initialized(),
			      "a rejected set must not leave the registry usable");
	}

	/* Restore a working registry for the remaining cases. */
	(void)command_setup();
}

ZTEST(services_command, test_invoke_by_name_and_id_reach_the_same_handler)
{
	struct kfsw_command_result result;

	last_arg_count = -1;
	zassert_ok(kfsw_command_invoke("noop", NULL, 0U, &result));
	zassert_equal(result.status, KFSW_COMMAND_OK);
	zassert_equal(strcmp(result.detail, "done"), 0);
	zassert_equal(last_arg_count, 0);
	zassert_equal(last_source_node, 0U, "a local call reports node 0");

	const struct kfsw_command_source source = {.node = 42U, .authenticated = false};

	last_arg_count = -1;
	zassert_ok(kfsw_command_invoke_id(1U, NULL, 0U, &source, &result));
	zassert_equal(result.status, KFSW_COMMAND_OK);
	zassert_equal(last_source_node, 42U, "the wire path passes the source node through");
}

ZTEST(services_command, test_argument_validation)
{
	struct kfsw_command_arg args[1] = {
		{.type = KFSW_COMMAND_TYPE_U32, .value.u32 = 0U},
	};
	struct kfsw_command_result result;

	/* Wrong count. */
	zassert_equal(kfsw_command_invoke("noop", args, 1U, &result), -EINVAL);
	zassert_equal(result.status, KFSW_COMMAND_INVALID_ARGUMENT);

	/* Right count, wrong type. */
	args[0].type = KFSW_COMMAND_TYPE_I32;
	args[0].value.i32 = -1;
	zassert_equal(kfsw_command_invoke("takes_u32", args, 1U, &result), -EINVAL);
	zassert_equal(result.status, KFSW_COMMAND_INVALID_ARGUMENT);

	/* Text argument declared but not supplied. */
	args[0].type = KFSW_COMMAND_TYPE_TEXT;
	args[0].value.text = NULL;
	zassert_equal(kfsw_command_invoke("takes_text", args, 1U, &result), -EINVAL);
	zassert_equal(result.status, KFSW_COMMAND_INVALID_ARGUMENT);

	/* Correct call reaches the handler. */
	args[0].type = KFSW_COMMAND_TYPE_U32;
	args[0].value.u32 = 0xA5A5A5A5U;
	zassert_ok(kfsw_command_invoke("takes_u32", args, 1U, &result));
	zassert_equal(last_u32, 0xA5A5A5A5U);
}

ZTEST(services_command, test_unknown_command_and_silent_handler_failure)
{
	struct kfsw_command_result result;

	zassert_equal(kfsw_command_invoke("absent", NULL, 0U, &result), -ENOENT);
	zassert_equal(result.status, KFSW_COMMAND_UNKNOWN);

	zassert_equal(kfsw_command_invoke_id(9999U, NULL, 0U, NULL, &result), -ENOENT);
	zassert_equal(result.status, KFSW_COMMAND_UNKNOWN);

	/* A handler that fails without a status must still report one. */
	zassert_equal(kfsw_command_invoke("breaks", NULL, 0U, &result), -EIO);
	zassert_equal(result.status, KFSW_COMMAND_FAILED);
}

ZTEST(services_command, test_lookup_and_enumeration)
{
	struct kfsw_command_info info;
	size_t seen = 0U;

	zassert_ok(kfsw_command_find("takes_u32", &info));
	zassert_equal(info.id, 2U);
	zassert_equal(info.arg_count, 1U);
	zassert_equal(info.arg_types[0], KFSW_COMMAND_TYPE_U32);
	zassert_equal(kfsw_command_find("absent", &info), -ENOENT);

	kfsw_command_visit(count_command, &seen);
	zassert_equal(seen, ARRAY_SIZE(valid_commands));
}

ZTEST(services_command, test_null_arguments_are_rejected)
{
	struct kfsw_command_result result;

	zassert_equal(kfsw_command_invoke(NULL, NULL, 0U, &result), -EINVAL);
	zassert_equal(kfsw_command_invoke("noop", NULL, 0U, NULL), -EINVAL);
	zassert_equal(kfsw_command_find(NULL, NULL), -EINVAL);
}

ZTEST_SUITE(services_command, NULL, command_setup, NULL, NULL, NULL);
