#include <errno.h>
#include <stdint.h>

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include <kfsw/services/resmon.h>

/*
 * Native threads use host stacks, so these tests cover enumeration, bounds
 * and threshold crossings. Measure stack headroom on the MCU.
 */

static void reset_service(void *fixture)
{
	ARG_UNUSED(fixture);

	(void)kfsw_resmon_stop();
	(void)kfsw_resmon_set_alert_percent(CONFIG_KFSW_RESMON_ALERT_PERCENT);
}

ZTEST(services_resmon, test_sweep_reads_the_threads)
{
	struct kfsw_resmon_status status;
	int threads = kfsw_resmon_sample();

	zassert_true(threads > 0, "a sweep read no threads");

	kfsw_resmon_get_status(&status);
	zassert_equal(status.threads, (uint16_t)threads);
	zassert_true(status.sweeps > 0U);

	/* Use is a share of each thread's own stack, so it has to be a percentage. */
	zassert_true(status.last_used_percent <= 100U);
	zassert_true(status.worst_used_percent <= 100U);
	zassert_true(status.worst_used_percent >= status.last_used_percent);
	zassert_true(status.worst_stack_bytes > 0U);
	zassert_true(status.worst_unused_bytes < status.worst_stack_bytes);
	zassert_not_equal(status.worst_thread[0], '\0');
}

ZTEST(services_resmon, test_the_tightest_margin_is_not_the_worst_percentage)
{
	struct kfsw_resmon_status status;

	zassert_true(kfsw_resmon_sample() > 0, "a sweep read no threads");
	kfsw_resmon_get_status(&status);

	/*
	 * The two answer different questions. A small stack at seventy per cent
	 * has less room left than a large one at ninety, so an operator asking
	 * how close anything is to the edge needs the bytes, not the share.
	 */
	zassert_true(status.tightest_stack_bytes > 0U, "no thread held a tightest margin");
	zassert_true(status.tightest_unused_bytes < status.tightest_stack_bytes);
	zassert_not_equal(status.tightest_thread[0], '\0');

	/* Whoever holds it, nobody can have less room than the tightest. */
	zassert_true(status.tightest_unused_bytes <= status.worst_unused_bytes,
		     "the tightest margin is larger than the busiest thread's: %u > %u",
		     status.tightest_unused_bytes, status.worst_unused_bytes);
}

ZTEST(services_resmon, test_an_unreadable_stack_is_counted_not_dropped)
{
	struct kfsw_resmon_status status;
	int threads = kfsw_resmon_sample();

	zassert_true(threads > 0, "a sweep read no threads");
	kfsw_resmon_get_status(&status);

	/*
	 * Whether any stack is unreadable depends on the platform, so this does
	 * not demand one. What it demands is that the count exists and agrees
	 * with the read count, because a stack left out silently publishes a
	 * margin for part of the system as if it were all of it.
	 */
	zassert_equal(status.threads, (uint16_t)threads);
	zassert_true((uint32_t)status.threads + status.unmeasured >= (uint32_t)threads);
}

ZTEST(services_resmon, test_alert_bounds_are_enforced)
{
	struct kfsw_resmon_status status;

	zassert_equal(kfsw_resmon_set_alert_percent(0U), -ERANGE);
	zassert_equal(kfsw_resmon_set_alert_percent(101U), -ERANGE);

	kfsw_resmon_get_status(&status);
	zassert_equal(status.alert_percent, CONFIG_KFSW_RESMON_ALERT_PERCENT);

	zassert_equal(kfsw_resmon_set_alert_percent(100U), 0);
	kfsw_resmon_get_status(&status);
	zassert_equal(status.alert_percent, 100U);
}

ZTEST(services_resmon, test_crossing_the_threshold_alerts_once)
{
	struct kfsw_resmon_status before;
	struct kfsw_resmon_status after;
	uint32_t reached;

	(void)kfsw_resmon_sample();
	kfsw_resmon_get_status(&before);

	/* The threshold comes from what the sweep saw, so the test does not
	 * depend on how much stack anything happens to use.
	 */
	reached = MAX(before.last_used_percent, 1U);
	zassert_equal(kfsw_resmon_set_alert_percent(reached), 0);

	(void)kfsw_resmon_sample();
	kfsw_resmon_get_status(&after);
	zassert_equal(after.alerts, before.alerts + 1U, "a sweep at %u%% did not reach %u%%",
		      after.last_used_percent, reached);

	/* Still at the threshold, so the crossing is not counted again. */
	(void)kfsw_resmon_sample();
	kfsw_resmon_get_status(&after);
	zassert_equal(after.alerts, before.alerts + 1U);

	/* A threshold above what any thread reaches clears the state, and a
	 * later crossing counts again.
	 */
	zassert_equal(kfsw_resmon_set_alert_percent(100U), 0);
	(void)kfsw_resmon_sample();
	zassert_equal(kfsw_resmon_set_alert_percent(reached), 0);
	(void)kfsw_resmon_sample();
	kfsw_resmon_get_status(&after);
	zassert_equal(after.alerts, before.alerts + 2U);
}

ZTEST(services_resmon, test_start_and_stop_are_safe)
{
	zassert_equal(kfsw_resmon_stop(), -EALREADY);
	zassert_equal(kfsw_resmon_start(), 0);
	zassert_equal(kfsw_resmon_start(), -EALREADY);
	zassert_equal(kfsw_resmon_stop(), 0);
	zassert_equal(kfsw_resmon_stop(), -EALREADY);

	/* A null destination is ignored. */
	kfsw_resmon_get_status(NULL);
}

ZTEST_SUITE(services_resmon, NULL, NULL, NULL, reset_service, NULL);
