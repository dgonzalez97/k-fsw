#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/ztest.h>

#include <kfsw/services/gndwdt.h>
#if CONFIG_KFSW_EVENT
#include <kfsw/services/event.h>
#endif

#define TIMEOUT_MS (CONFIG_KFSW_GNDWDT_TIMEOUT_S * 1000ULL)
static uint64_t now_ms;

uint64_t __wrap_kfsw_time_monotonic_ms(void)
{
	return now_ms;
}

static atomic_t reboots;
static int reboot_type = -1;
K_SEM_DEFINE(reboot_seen, 0, 1);

/* The real call never returns, so this one parks the thread it runs on instead
 * of returning into a caller that has no code after the call. Only the reset
 * work uses that queue here, and the tests that run after it call the service
 * directly.
 */
FUNC_NORETURN void __wrap_sys_reboot(int type)
{
	reboot_type = type;
	atomic_inc(&reboots);
	k_sem_give(&reboot_seen);
	k_sleep(K_FOREVER);
	CODE_UNREACHABLE;
}

static void *setup(void)
{
	const struct kfsw_command_definition_set *sets[] = {&kfsw_gndwdt_command_definitions};

	zassert_ok(kfsw_command_init(sets, ARRAY_SIZE(sets)));
#if CONFIG_KFSW_PARAM
	const struct kfsw_param_definition_set *params[] = {&kfsw_gndwdt_param_definitions};

	zassert_ok(kfsw_param_init(params, ARRAY_SIZE(params)));
#endif
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
	if (expected == KFSW_COMMAND_OK) {
#if CONFIG_KFSW_PARAM
		struct kfsw_gndwdt_status status;
		char reply[KFSW_COMMAND_MAX_DETAIL_SIZE];

		kfsw_gndwdt_get_status(&status);
		(void)snprintf(reply, sizeof(reply), "ground_wtd_cnt=%u ground_wtd_timeout=%u",
			       status.remaining_s, status.timeout_s);
		zassert_equal(strcmp(result.detail, reply), 0);
#else
		zassert_equal(strcmp(result.detail, "ground_wtd restarted"), 0);
#endif
	}
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
	zassert_equal(kfsw_gndwdt_set_timeout_s(7200U - 1U), -ERANGE);
	zassert_equal(kfsw_gndwdt_set_timeout_s(CONFIG_KFSW_GNDWDT_TIMEOUT_MIN_S - 1U), -ERANGE);
	zassert_equal(kfsw_gndwdt_set_timeout_s(CONFIG_KFSW_GNDWDT_TIMEOUT_MAX_S + 1U), -ERANGE);
	zassert_ok(kfsw_gndwdt_set_timeout_s(7200U));
	zassert_ok(kfsw_gndwdt_set_timeout_s(432000U));
	zassert_equal(kfsw_gndwdt_set_timeout_s(432001U), -ERANGE);
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

ZTEST(services_gndwdt, test_get_counts_down_without_feeding)
{
	const struct kfsw_command_arg arg = {.type = KFSW_COMMAND_TYPE_TEXT, .value.text = "get"};
	const struct kfsw_command_source source = {.node = 16U, .via_csp = true};
	struct kfsw_command_result result;
	struct kfsw_gndwdt_status before;
	struct kfsw_gndwdt_status after;

	zassert_ok(kfsw_gndwdt_set_timeout_s(7200U));
	zassert_ok(kfsw_gndwdt_start());
	kfsw_gndwdt_get_status(&before);
	now_ms += 1000U;
	zassert_ok(kfsw_command_invoke("ground_wtd", &arg, 1U, &result));
	zassert_equal(strcmp(result.detail, "ground_wtd_cnt=7199 ground_wtd_timeout=7200"), 0);
	now_ms += 7199U * 1000U;
	zassert_ok(kfsw_command_invoke_id(KFSW_COMMAND_ID_GROUND_WTD, &arg, 1U, &source, &result));
	zassert_equal(strcmp(result.detail, "ground_wtd_cnt=0 ground_wtd_timeout=7200"), 0);
	kfsw_gndwdt_get_status(&after);
	zassert_equal(after.contacts, before.contacts);
	zassert_equal(after.last_node, before.last_node);
	zassert_equal(kfsw_gndwdt_evaluate(), -ETIMEDOUT);
}

#if CONFIG_KFSW_PARAM
ZTEST(services_gndwdt, test_parameters_match_reply_and_validate_writes)
{
	struct kfsw_param_value value = {.type = KFSW_PARAM_U32, .size = sizeof(uint32_t)};
	const uint32_t invalid[] = {0U, 7199U, 432001U, UINT32_MAX};

	zassert_ok(kfsw_gndwdt_start());
	for (size_t i = 0; i < ARRAY_SIZE(invalid); i++) {
		value.scalar.u32 = invalid[i];
		zassert_equal(kfsw_param_set("ground_wtd_timeout", &value), -ERANGE);
	}
	value.scalar.u32 = 7200U;
	zassert_ok(kfsw_param_set("ground_wtd_timeout", &value));
	feed(2U, "KFSWWSFK", true, KFSW_COMMAND_OK);
	zassert_ok(kfsw_param_get("ground_wtd_timeout", &value));
	zassert_equal(value.scalar.u32, 7200U);
	zassert_ok(kfsw_param_get("ground_wtd_cnt", &value));
	zassert_equal(value.scalar.u32, 7200U);
	zassert_equal(kfsw_param_set("ground_wtd_cnt", &value), -EACCES);
	now_ms += 123000U;
	zassert_ok(kfsw_param_get("ground_wtd_cnt", &value));
	zassert_equal(value.scalar.u32, 7077U);
	value.scalar.u32 = 432000U;
	zassert_ok(kfsw_param_set("ground_wtd_timeout", &value));
	zassert_ok(kfsw_param_get_by_id((35U << 8) | 0x04U, &value));
	zassert_equal(value.scalar.u32, 432000U);
	zassert_ok(kfsw_param_get_by_id((35U << 8) | 0x18U, &value));
	zassert_equal(value.scalar.u32, 432000U - 123U);
}
#endif

ZTEST(services_gndwdt, test_expiry_resets_the_node_once)
{
	struct kfsw_gndwdt_status before;
	struct kfsw_gndwdt_status after;

	zassert_ok(kfsw_gndwdt_start());
	kfsw_gndwdt_get_status(&before);
	now_ms += TIMEOUT_MS;

	/* The service's own worker reaches the decision. Nothing else in the
	 * suite runs it, so the whole reset path hangs off this sleep.
	 */
	k_sleep(K_MSEC(CONFIG_KFSW_GNDWDT_CHECK_MS + CONFIG_KFSW_GNDWDT_RESET_DELAY_MS));
	kfsw_gndwdt_get_status(&after);
	zassert_equal(after.expiries, before.expiries + 1U);
	zassert_equal(after.remaining_s, 0U);

#if CONFIG_KFSW_EVENT
	{
		struct kfsw_event_record record;

		/* The operator learns the reason from the ring, not the console. */
		zassert_ok(kfsw_event_get(0U, &record));
		zassert_equal(record.source, KFSW_EVENT_SOURCE_GNDWDT);
		zassert_equal(record.id, KFSW_EVENT_GNDWDT_EXPIRED);
		zassert_equal(record.severity, KFSW_EVENT_CRITICAL);
		zassert_equal(record.payload_size, 4U);
		zassert_equal(sys_get_be32(record.payload), CONFIG_KFSW_GNDWDT_TIMEOUT_S);
	}
#endif

	/* A feed arriving after the decision does not call it off, and the
	 * decision is not counted a second time.
	 */
	feed(16U, "KFSWWSFK", true, KFSW_COMMAND_UNAVAILABLE);
	zassert_ok(kfsw_gndwdt_evaluate());
	kfsw_gndwdt_get_status(&after);
	zassert_equal(after.expiries, before.expiries + 1U);
	zassert_equal(after.contacts, before.contacts);

	zassert_ok(k_sem_take(&reboot_seen, K_SECONDS(5)));
	zassert_equal(reboot_type, SYS_REBOOT_COLD);
	zassert_equal(atomic_get(&reboots), 1);
}

ZTEST_SUITE(services_gndwdt, NULL, setup, before, after, NULL);
