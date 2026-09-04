#include <errno.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/sys/util.h>
#include <zephyr/ztest.h>

#include <kfsw/services/parameter.h>

/* The parameter service initializes once per image, so a suite that needs its
 * own set of definitions needs its own configuration. String handling is worth
 * that: it is the one type whose length is part of the value, and every layer
 * that carries it has to agree on where the terminator went.
 */

static char string_storage[8];
static char refuse_storage[8];
static int refusals;

/* Accepts only the empty default. A validator that refused its own compiled
 * default would be caught at registration, which is the service refusing to
 * build a table its owner never sanctioned.
 */
static int refuse_anything_written(const char *text)
{
	if (text[0] == '\0') {
		return 0;
	}
	refusals++;
	return -EINVAL;
}

static const struct kfsw_param_definition string_definitions[] = {
	{
		.offset = 0x00U,
		.type = KFSW_PARAM_STRING,
		.capacity = sizeof(string_storage),
		.flags = KFSW_PARAM_FLAG_CONFIGURATION,
		.name = "text",
		.value = string_storage,
		.default_text = "abc",
	},
	{
		.offset = 0x08U,
		.type = KFSW_PARAM_STRING,
		.capacity = sizeof(refuse_storage),
		.flags = KFSW_PARAM_FLAG_CONFIGURATION,
		.name = "guarded",
		.value = refuse_storage,
		.default_text = "",
		.validate_text = refuse_anything_written,
	},
};

static const struct kfsw_param_definition_set string_set = {
	.table = KFSW_PARAM_TABLE_CORE_FIRST,
	.name = "strings",
	.definitions = string_definitions,
	.count = ARRAY_SIZE(string_definitions),
};

static void *strings_setup(void)
{
	const struct kfsw_param_definition_set *const sets[] = {&string_set};

	/* The service initializes once per image, so every case here shares
	 * this one table rather than each building its own. */
	zassert_ok(kfsw_param_init(sets, ARRAY_SIZE(sets)));
	return NULL;
}

ZTEST(services_param_strings, test_a_string_round_trips_and_is_bounded)
{
	struct kfsw_param_value value = {0};

	zassert_ok(kfsw_param_get("text", &value));
	zassert_equal(value.type, KFSW_PARAM_STRING);
	zassert_str_equal(value.text, "abc");
	zassert_equal(value.size, 4U, "size carries the terminator");

	/* Exactly filling the declared capacity is accepted: seven characters
	 * and a terminator in eight bytes. */
	(void)strcpy(value.text, "1234567");
	value.size = 8U;
	zassert_ok(kfsw_param_set("text", &value));
	zassert_ok(kfsw_param_get("text", &value));
	zassert_str_equal(value.text, "1234567");

	/* One more is refused rather than truncated. A truncated value is a
	 * different value, and the operator is never told. */
	(void)strcpy(value.text, "12345678");
	value.size = 9U;
	zassert_equal(kfsw_param_set("text", &value), -EMSGSIZE);
	zassert_ok(kfsw_param_get("text", &value));
	zassert_str_equal(value.text, "1234567", "a refused write must change nothing");

	/* An empty string is a value, not an absence. */
	value.text[0] = '\0';
	value.size = 1U;
	zassert_ok(kfsw_param_set("text", &value));
	zassert_ok(kfsw_param_get("text", &value));
	zassert_equal(strlen(value.text), 0U);

	/* A size of zero cannot describe even an empty string. */
	value.size = 0U;
	zassert_equal(kfsw_param_set("text", &value), -EMSGSIZE);
}

ZTEST(services_param_strings, test_an_owner_can_refuse_a_string)
{
	struct kfsw_param_value value = {0};
	const int before = refusals;

	zassert_ok(kfsw_param_get("guarded", &value));
	(void)strcpy(value.text, "no");
	value.size = 3U;

	zassert_equal(kfsw_param_set("guarded", &value), -EINVAL);
	zassert_equal(refusals, before + 1, "the owner's validator has to be the one deciding");
	zassert_ok(kfsw_param_get("guarded", &value));
	zassert_equal(strlen(value.text), 0U);
}

ZTEST(services_param_strings, test_a_string_of_the_wrong_type_is_refused)
{
	struct kfsw_param_value value = {0};

	zassert_ok(kfsw_param_get("text", &value));
	value.type = KFSW_PARAM_U32;
	zassert_equal(kfsw_param_set("text", &value), -EMSGSIZE);
}

ZTEST_SUITE(services_param_strings, NULL, strings_setup, NULL, NULL, NULL);
