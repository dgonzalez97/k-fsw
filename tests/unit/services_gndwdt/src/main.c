#include <errno.h>
#include <stdint.h>

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include <kfsw/services/gndwdt.h>

#define TIMEOUT_MS (CONFIG_KFSW_GNDWDT_TIMEOUT_S * 1000ULL)
static uint64_t now_ms;

uint64_t __wrap_kfsw_time_monotonic_ms(void)
{
	return now_ms;
}

static void *setup(void)
{
	const struct kfsw_command_definition_set *sets[] = {&kfsw_gndwdt_command_definitions};

	zassert_ok(kfsw_command_init(sets, ARRAY_SIZE(sets)));
	return NULL;
}

static void before(void *fixture)
{
	ARG_UNUSED(fixture);
	zassert_ok(kfsw_gndwdt_set_timeout_s(CONFIG_KFSW_GNDWDT_TIMEOUT_S));
	kfsw_gndwdt_set_enabled(true);
}

static void after(void *fixture)
{
	ARG_UNUSED(fixture);
	(void)kfsw_gndwdt_stop();
}

static void feed(uint16_t node, const char *word, bool via_csp, enum kfsw_command_status expected)
{
	const struct kfsw_command_arg arg = {
		.type = KFSW_COMMAND_TYPE_TEXT,
		.value.text = word,
	};
	const struct kfsw_command_source source = {.node = node, .via_csp = via_csp};
	struct kfsw_command_result result;

	(void)kfsw_command_invoke_id(KFSW_COMMAND_ID_GROUND_WTD, &arg, 1U, &source, &result);
	zassert_equal(result.status, expected);
}

ZTEST(services_gndwdt, test_stopped_service_rejects_feed)
{
	struct kfsw_gndwdt_status before;
	struct kfsw_gndwdt_status after;

	kfsw_gndwdt_get_status(&before);
	feed(9U, "KFSWWSFK", true, KFSW_COMMAND_UNAVAILABLE);
	now_ms += TIMEOUT_MS;
	zassert_ok(kfsw_gndwdt_evaluate());
	kfsw_gndwdt_get_status(&after);
	zassert_equal(after.contacts, before.contacts);
}

ZTEST(services_gndwdt, test_command_from_any_subsystem_feeds)
{
	struct kfsw_gndwdt_status before;
	struct kfsw_gndwdt_status after;
	const uint16_t nodes[] = {2U, 16U, 16383U};

	zassert_ok(kfsw_gndwdt_start());
	zassert_equal(kfsw_gndwdt_start(), -EALREADY);
	kfsw_gndwdt_get_status(&before);
	for (size_t i = 0; i < ARRAY_SIZE(nodes); i++) {
		now_ms += TIMEOUT_MS / 2U;
		feed(nodes[i], "KFSWWSFK", true, KFSW_COMMAND_OK);
		zassert_ok(kfsw_gndwdt_evaluate());
	}
	kfsw_gndwdt_get_status(&after);
	zassert_equal(after.contacts, before.contacts + ARRAY_SIZE(nodes));
	zassert_equal(after.last_node, 16383U);
	zassert_equal(after.since_contact_s, 0U);
}

ZTEST(services_gndwdt, test_wrong_word_and_local_calls_do_not_feed)
{
	const char *invalid[] = {"", "kfswwsfk", "KFSWWSF", "KFSWWSFKx", " KFSWWSFK"};
	struct kfsw_gndwdt_status before;
	struct kfsw_gndwdt_status after;

	zassert_ok(kfsw_gndwdt_start());
	kfsw_gndwdt_get_status(&before);
	now_ms += TIMEOUT_MS - 1000U;
	for (size_t i = 0; i < ARRAY_SIZE(invalid); i++) {
		feed(7U, invalid[i], true, KFSW_COMMAND_DENIED);
	}
	feed(0U, "KFSWWSFK", false, KFSW_COMMAND_DENIED);
	feed(7U, "KFSWWSFK", false, KFSW_COMMAND_DENIED);
	kfsw_gndwdt_get_status(&after);
	zassert_equal(after.contacts, before.contacts);
	zassert_equal(after.last_node, before.last_node);
	zassert_equal(after.since_contact_s, CONFIG_KFSW_GNDWDT_TIMEOUT_S - 1U);
	now_ms += 1000U;
	zassert_equal(kfsw_gndwdt_evaluate(), -ETIMEDOUT);
}

ZTEST(services_gndwdt, test_wrong_id_count_and_type_do_not_feed)
{
	const struct kfsw_command_source source = {.node = 7U, .via_csp = true};
	struct kfsw_command_arg args[2] = {
		{.type = KFSW_COMMAND_TYPE_TEXT, .value.text = "KFSWWSFK"},
		{.type = KFSW_COMMAND_TYPE_TEXT, .value.text = "KFSWWSFK"},
	};
	struct kfsw_command_result result;

	zassert_ok(kfsw_gndwdt_start());
	now_ms += TIMEOUT_MS;
	zassert_equal(kfsw_command_invoke_id(999U, args, 1U, &source, &result), -ENOENT);
	zassert_equal(result.status, KFSW_COMMAND_UNKNOWN);
	for (size_t count = 0; count <= 2; count += 2) {
		zassert_equal(kfsw_command_invoke_id(KFSW_COMMAND_ID_GROUND_WTD, args, count,
						     &source, &result),
			      -EINVAL);
		zassert_equal(result.status, KFSW_COMMAND_INVALID_ARGUMENT);
	}
	args[0].type = KFSW_COMMAND_TYPE_U32;
	zassert_equal(
		kfsw_command_invoke_id(KFSW_COMMAND_ID_GROUND_WTD, args, 1U, &source, &result),
		-EINVAL);
	zassert_equal(kfsw_gndwdt_evaluate(), -ETIMEDOUT);
}

ZTEST(services_gndwdt, test_disarming_and_configuration_do_not_feed)
{
	struct kfsw_gndwdt_status status;

	zassert_ok(kfsw_gndwdt_start());
	kfsw_gndwdt_set_enabled(false);
	now_ms += TIMEOUT_MS;
	zassert_ok(kfsw_gndwdt_evaluate());
	zassert_ok(kfsw_gndwdt_set_timeout_s(CONFIG_KFSW_GNDWDT_TIMEOUT_S));
	kfsw_gndwdt_set_enabled(true);
	kfsw_gndwdt_get_status(&status);
	zassert_equal(status.since_contact_s, CONFIG_KFSW_GNDWDT_TIMEOUT_S);
	zassert_equal(kfsw_gndwdt_evaluate(), -ETIMEDOUT);
}

ZTEST(services_gndwdt, test_timeout_bounds_are_enforced)
{
	zassert_ok(kfsw_gndwdt_start());
	zassert_equal(kfsw_gndwdt_set_timeout_s(432000U - 1U), -ERANGE);
	zassert_equal(kfsw_gndwdt_set_timeout_s(CONFIG_KFSW_GNDWDT_TIMEOUT_MIN_S - 1U), -ERANGE);
	zassert_equal(kfsw_gndwdt_set_timeout_s(CONFIG_KFSW_GNDWDT_TIMEOUT_MAX_S + 1U), -ERANGE);
	zassert_ok(kfsw_gndwdt_set_timeout_s(CONFIG_KFSW_GNDWDT_TIMEOUT_MAX_S));
}

ZTEST(services_gndwdt, test_elapsed_time_survives_32_bit_wrap)
{
	struct kfsw_gndwdt_status status;

	now_ms = UINT32_MAX - 1000ULL;
	zassert_ok(kfsw_gndwdt_start());
	now_ms += TIMEOUT_MS - 1U;
	zassert_ok(kfsw_gndwdt_evaluate());
	now_ms++;
	zassert_equal(kfsw_gndwdt_evaluate(), -ETIMEDOUT);
	feed(7U, "KFSWWSFK", true, KFSW_COMMAND_OK);
	now_ms += UINT32_MAX + 1ULL;
	kfsw_gndwdt_get_status(&status);
	zassert_equal(status.since_contact_s, (UINT32_MAX + 1ULL) / 1000U);
	zassert_equal(kfsw_gndwdt_evaluate(), -ETIMEDOUT);
}

ZTEST(services_gndwdt, test_stop_and_status_are_safe)
{
	zassert_equal(kfsw_gndwdt_stop(), -EALREADY);
	zassert_ok(kfsw_gndwdt_start());
	zassert_ok(kfsw_gndwdt_stop());
	zassert_equal(kfsw_gndwdt_stop(), -EALREADY);
	kfsw_gndwdt_get_status(NULL);
}

ZTEST_SUITE(services_gndwdt, NULL, setup, before, after, NULL);
