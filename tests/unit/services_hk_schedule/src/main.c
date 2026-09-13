#include <zephyr/ztest.h>
#include <kfsw/services/hk.h>
#include <kfsw/services/parameter.h>
#include "hk_internal.h"

static uint32_t value;
static const struct kfsw_param_definition parameter = {
	.offset = 0,
	.type = KFSW_PARAM_U32,
	.name = "counter",
	.value = &value,
};
static const struct kfsw_param_definition_set table = {
	.table = 20,
	.name = "schedule",
	.definitions = &parameter,
	.count = 1,
};
static const struct kfsw_hk_entry entry = {.node = 0, .param_id = KFSW_PARAM_ID(20, 0)};

static void *setup(void)
{
	const struct kfsw_param_definition_set *sets[] = {&table};

	zassert_ok(kfsw_param_init(sets, 1));
	zassert_ok(kfsw_hk_init());
	return NULL;
}

static void before(void *unused)
{
	ARG_UNUSED(unused);
	zassert_ok(kfsw_hk_clear(0));
	zassert_ok(kfsw_hk_define(0, &entry, 1));
	zassert_ok(kfsw_hk_set_period(0, 1000));
	kfsw_hk_set_enabled(true);
	kfsw_hk_lock();
	kfsw_hk_report_at(0)->next_uptime_ms = 1000;
	kfsw_hk_unlock();
}

ZTEST(hk_schedule, test_completion_keeps_phase_and_counts_elapsed_slots)
{
	const int64_t finished[] = {1250, 3500, 3600, 2000};
	const int64_t expected[] = {2000, 4000, 4000, 3000};
	const uint32_t missed[] = {0, 2, 2, 1};
	struct kfsw_hk_stats before_stats, after;
	struct kfsw_hk_due due;

	for (size_t i = 0; i < ARRAY_SIZE(finished); i++) {
		before(NULL);
		kfsw_hk_get_stats(&before_stats);
		zassert_false(kfsw_hk_schedule_take(0, 999, &due));
		zassert_true(kfsw_hk_schedule_take(0, i == 2 ? 3500 : 1000, &due));
		kfsw_hk_schedule_finish(0, &due, finished[i]);
		kfsw_hk_get_stats(&after);
		zassert_equal(kfsw_hk_report_at(0)->next_uptime_ms, expected[i]);
		zassert_equal(after.missed_slots - before_stats.missed_slots, missed[i]);
		zassert_equal(after.scheduled_attempts - before_stats.scheduled_attempts, 1);
		zassert_false(kfsw_hk_schedule_take(0, finished[i], &due));
	}
}

ZTEST(hk_schedule, test_period_change_survives_old_completion)
{
	struct kfsw_hk_due due;

	zassert_true(kfsw_hk_schedule_take(0, 1000, &due));
	zassert_ok(kfsw_hk_set_period(0, 5000));
	int64_t changed = kfsw_hk_report_at(0)->next_uptime_ms;

	kfsw_hk_schedule_finish(0, &due, 1500);
	zassert_equal(kfsw_hk_report_at(0)->next_uptime_ms, changed);
}

ZTEST(hk_schedule, test_disable_and_redefinition_invalidate_inflight_schedule)
{
	struct kfsw_hk_due due;

	zassert_true(kfsw_hk_schedule_take(0, 1000, &due));
	kfsw_hk_set_enabled(false);
	kfsw_hk_schedule_finish(0, &due, 3500);
	zassert_equal(kfsw_hk_report_at(0)->next_uptime_ms, 1000);
	zassert_false(kfsw_hk_schedule_take(0, 4000, &due));
	kfsw_hk_set_enabled(true);
	zassert_ok(kfsw_hk_define(0, &entry, 1));
	int64_t changed = kfsw_hk_report_at(0)->next_uptime_ms;

	kfsw_hk_schedule_finish(0, &due, 3500);
	zassert_equal(kfsw_hk_report_at(0)->next_uptime_ms, changed);
}

ZTEST_SUITE(hk_schedule, NULL, setup, before, NULL, NULL);
