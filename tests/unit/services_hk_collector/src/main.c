#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#include <zephyr/ztest.h>

#include <kfsw/comms/csp.h>
#include <kfsw/services/hk.h>
#include <kfsw/services/parameter.h>

#define TEST_TABLE 30U

/*
 * The thread that collects on a schedule, and the two gates in front of it.
 *
 * Neither gate is cosmetic. A ring full of samples stamped zero cannot be put
 * in order, and the period keeps running while the clock is missing — so a
 * node that came up without one would overwrite its own history before anybody
 * could ask for it. The enable is the switch an operator has when they want
 * the node to stop filling the ring without losing what is in it.
 *
 * These cases wait in real time because that is what the thread does. The
 * period floor is lowered in prj.conf to keep the waiting short.
 */

static uint16_t counter_u16 = 0x1234U;
static uint32_t counter_u32 = 0xAABBCCDDU;

static const struct kfsw_param_definition test_definitions[] = {
	{
		.offset = 0x00,
		.type = KFSW_PARAM_U16,
		.flags = KFSW_PARAM_FLAG_READ_ONLY,
		.name = "collector_test_u16",
		.value = &counter_u16,
	},
	{
		.offset = 0x02,
		.type = KFSW_PARAM_U32,
		.flags = KFSW_PARAM_FLAG_READ_ONLY,
		.name = "collector_test_u32",
		.value = &counter_u32,
	},
};

static const struct kfsw_param_definition_set test_set = {
	.table = TEST_TABLE,
	.name = "collectortest",
	.definitions = test_definitions,
	.count = ARRAY_SIZE(test_definitions),
};

static void set_clock(bool valid)
{
	/* Seconds zero is what an unset clock reads as, which is exactly how
	 * the service decides it has none.
	 */
	struct kfsw_csp_clock clock = {.seconds = valid ? 1788000000 : 0, .nanoseconds = 0U};

	(void)kfsw_csp_clock_set(&clock);
}

static void define_report(uint8_t report)
{
	const struct kfsw_hk_entry entries[] = {
		{.node = KFSW_HK_NODE_LOCAL, .param_id = KFSW_PARAM_ID(TEST_TABLE, 0x00)},
		{.node = KFSW_HK_NODE_LOCAL, .param_id = KFSW_PARAM_ID(TEST_TABLE, 0x02)},
	};

	zassert_ok(kfsw_hk_define(report, entries, ARRAY_SIZE(entries)), "the report was refused");
}

static uint32_t collections(void)
{
	struct kfsw_hk_stats stats;

	kfsw_hk_get_stats(&stats);
	return stats.collections;
}

static void *collector_setup(void)
{
	static const struct kfsw_param_definition_set *const sets[] = {&test_set};

	zassert_ok(kfsw_param_init(sets, ARRAY_SIZE(sets)), "the parameter table did not start");
	zassert_ok(kfsw_hk_init(), "housekeeping did not start");
	zassert_ok(kfsw_hk_start(), "the collector did not start");
	return NULL;
}

static void collector_before(void *fixture)
{
	ARG_UNUSED(fixture);
	for (uint8_t report = 0U; report < CONFIG_KFSW_HK_REPORTS; report++) {
		(void)kfsw_hk_clear(report);
	}
	kfsw_hk_set_enabled(true);
	set_clock(false);
	/* Long enough for the thread to notice the state it is being left in. */
	k_sleep(K_MSEC(400));
}

ZTEST_SUITE(kfsw_hk_collector, NULL, collector_setup, collector_before, NULL, NULL);

/* Starting twice is what a restarted service does, and it must not start a
 * second thread on the same state.
 */
ZTEST(kfsw_hk_collector, test_starting_again_is_harmless)
{
	zassert_ok(kfsw_hk_start(), "starting an already running collector failed");
}

/*
 * The gate that matters most. Without it the period runs, the ring fills with
 * samples that cannot be ordered, and the history an operator wanted is gone.
 */
ZTEST(kfsw_hk_collector, test_nothing_is_collected_until_the_node_knows_the_time)
{
	uint32_t before;
	uint16_t depth = 0U;

	define_report(0U);
	zassert_ok(kfsw_hk_set_period(0U, CONFIG_KFSW_HK_PERIOD_FLOOR_MS),
		   "the period was refused");

	before = collections();
	k_sleep(K_MSEC(800));

	zassert_equal(collections(), before, "a node with no clock collected on a schedule");
	zassert_ok(kfsw_hk_depth(0U, &depth), "the report could not be read");
	zassert_equal(depth, 0U, "the ring filled while the clock was missing");
}

/* By hand it still works, and the sample says it has no time. */
ZTEST(kfsw_hk_collector, test_collecting_by_hand_works_without_a_clock)
{
	struct kfsw_hk_sample sample;

	define_report(0U);
	zassert_ok(kfsw_hk_collect(0U), "an operator could not collect without a clock");
	zassert_ok(kfsw_hk_get(0U, 0U, &sample), "the sample was not kept");
	zassert_true((sample.flags & KFSW_HK_FLAG_CLOCK_UNSET) != 0U,
		     "a sample taken without a clock did not say so");
	zassert_equal(sample.seconds, 0U, "an unset clock produced a timestamp");
}

/* And once the time arrives, the schedule starts on its own. */
ZTEST(kfsw_hk_collector, test_the_schedule_starts_when_the_clock_arrives)
{
	uint32_t before;

	define_report(0U);
	zassert_ok(kfsw_hk_set_period(0U, CONFIG_KFSW_HK_PERIOD_FLOOR_MS),
		   "the period was refused");

	before = collections();
	k_sleep(K_MSEC(500));
	zassert_equal(collections(), before, "collection began before the clock was set");

	set_clock(true);
	k_sleep(K_MSEC(800));
	zassert_true(collections() > before, "the schedule did not start when the clock arrived");
}

ZTEST(kfsw_hk_collector, test_a_scheduled_sample_carries_the_time)
{
	struct kfsw_hk_sample sample;

	define_report(0U);
	set_clock(true);
	zassert_ok(kfsw_hk_set_period(0U, CONFIG_KFSW_HK_PERIOD_FLOOR_MS),
		   "the period was refused");
	k_sleep(K_MSEC(800));

	zassert_ok(kfsw_hk_get(0U, 0U, &sample), "the schedule produced no sample");
	zassert_true((sample.flags & KFSW_HK_FLAG_CLOCK_UNSET) == 0U,
		     "a sample taken with a clock said it had none");
	zassert_true(sample.seconds > 0U, "a timed sample carries no time");
}

/* A report with no period is collected only when asked, however long it sits. */
ZTEST(kfsw_hk_collector, test_a_report_without_a_period_is_left_alone)
{
	uint16_t depth = 0U;

	define_report(0U);
	set_clock(true);
	k_sleep(K_MSEC(800));

	zassert_ok(kfsw_hk_depth(0U, &depth), "the report could not be read");
	zassert_equal(depth, 0U, "a report with no period was collected anyway");
}

/*
 * The operator's switch. It stops the schedule and keeps the history, which is
 * the whole difference between disabling a report and clearing it.
 */
ZTEST(kfsw_hk_collector, test_disabling_stops_the_schedule_and_keeps_what_was_collected)
{
	uint16_t depth_when_disabled = 0U;
	uint16_t depth_later = 0U;
	uint32_t before;

	define_report(0U);
	set_clock(true);
	zassert_ok(kfsw_hk_set_period(0U, CONFIG_KFSW_HK_PERIOD_FLOOR_MS),
		   "the period was refused");
	k_sleep(K_MSEC(600));
	zassert_ok(kfsw_hk_depth(0U, &depth_when_disabled), "the report could not be read");
	zassert_true(depth_when_disabled > 0U, "the schedule collected nothing to keep");

	kfsw_hk_set_enabled(false);
	k_sleep(K_MSEC(300));
	before = collections();
	k_sleep(K_MSEC(800));

	zassert_equal(collections(), before, "a disabled collector kept collecting");
	zassert_ok(kfsw_hk_depth(0U, &depth_later), "the report could not be read");
	zassert_equal(depth_later, depth_when_disabled,
		      "disabling the collector discarded what it had already collected");

	kfsw_hk_set_enabled(true);
	k_sleep(K_MSEC(800));
	zassert_true(collections() > before, "the collector did not resume when enabled again");
}

/* Two reports on their own schedules, because one is the easy case. */
ZTEST(kfsw_hk_collector, test_reports_keep_their_own_schedules)
{
	uint16_t fast = 0U;
	uint16_t slow = 0U;

	define_report(0U);
	define_report(1U);
	set_clock(true);
	zassert_ok(kfsw_hk_set_period(0U, CONFIG_KFSW_HK_PERIOD_FLOOR_MS),
		   "the period was refused");
	zassert_ok(kfsw_hk_set_period(1U, CONFIG_KFSW_HK_PERIOD_FLOOR_MS * 6U),
		   "the period was refused");
	k_sleep(K_MSEC(900));

	zassert_ok(kfsw_hk_depth(0U, &fast), "the fast report could not be read");
	zassert_ok(kfsw_hk_depth(1U, &slow), "the slow report could not be read");
	zassert_true(fast > slow, "both reports collected at the same rate");
}
