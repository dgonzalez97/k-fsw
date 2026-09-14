#include <errno.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/sys/util.h>
#include <zephyr/ztest.h>

#include <kfsw/services/parameter.h>

/* String parameters need their own configuration, since the service initializes once. */

static char string_storage[8];
static char refuse_storage[8];
static int refusals;

/* Accepts only the empty default. */
static int refuse_anything_written(const char *text)
{
	if (text[0] == '\0') {
		return 0;
	}
	refusals++;
	return -EINVAL;
}

static uint8_t array_storage[4];
static const uint8_t array_default[4] = {1U, 2U, 3U, 4U};
static int array_refusals;

static int refuse_large_elements(const uint8_t *data, size_t size)
{
	for (size_t index = 0U; index < size; index++) {
		if (data[index] > 10U) {
			array_refusals++;
			return -ERANGE;
		}
	}
	return 0;
}

static const struct kfsw_param_definition string_definitions[] = {
	{
		.offset = 0x10U,
		.type = KFSW_PARAM_DATA,
		.capacity = ARRAY_SIZE(array_storage),
		.flags = KFSW_PARAM_FLAG_CONFIGURATION,
		.name = "levels",
		.value = array_storage,
		.default_data = array_default,
		.validate_data = refuse_large_elements,
	},
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

	/* The service initializes once, so the cases share this table. */
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

	/* Seven characters and a terminator fill the eight bytes. */
	(void)strcpy(value.text, "1234567");
	value.size = 8U;
	zassert_ok(kfsw_param_set("text", &value));
	zassert_ok(kfsw_param_get("text", &value));
	zassert_str_equal(value.text, "1234567");

	/* One more is refused, not truncated. */
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
	zassert_equal(refusals, before + 1, "the validator must decide");
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

/* ----------------------------------------------------------------- arrays */

ZTEST(services_param_strings, test_an_array_is_written_whole_or_not_at_all)
{
	struct kfsw_param_value value = {0};

	zassert_ok(kfsw_param_get("levels", &value));
	zassert_equal(value.type, KFSW_PARAM_DATA);
	zassert_equal(value.size, ARRAY_SIZE(array_storage), "a read returns every element");
	zassert_equal(value.bytes[0], 1U, "the compiled default reached the store");

	value.bytes[2] = 7U;
	zassert_ok(kfsw_param_set("levels", &value));
	zassert_ok(kfsw_param_get("levels", &value));
	zassert_equal(value.bytes[2], 7U);

	/* A short array write is refused. */
	value.size = ARRAY_SIZE(array_storage) - 1U;
	zassert_equal(kfsw_param_set("levels", &value), -EMSGSIZE);

	value.size = ARRAY_SIZE(array_storage) + 1U;
	zassert_equal(kfsw_param_set("levels", &value), -EMSGSIZE);

	value.size = ARRAY_SIZE(array_storage);
	zassert_ok(kfsw_param_get("levels", &value));
	zassert_equal(value.bytes[2], 7U, "a refused write must change nothing");
}

ZTEST(services_param_strings, test_an_owner_judges_the_array_as_a_whole)
{
	struct kfsw_param_value value = {0};
	const int before = array_refusals;

	/* The whole array is validated at once. */
	zassert_ok(kfsw_param_get("levels", &value));
	value.bytes[1] = 99U;
	zassert_equal(kfsw_param_set("levels", &value), -ERANGE);
	zassert_equal(array_refusals, before + 1);

	zassert_ok(kfsw_param_get("levels", &value));
	zassert_not_equal(value.bytes[1], 99U);
}

ZTEST_SUITE(services_param_strings, NULL, strings_setup, NULL, NULL, NULL);
