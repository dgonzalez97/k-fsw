#include <errno.h>
#include <stdint.h>

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include <kfsw/services/resmon.h>

/*
 * These tests run on the POSIX architecture, where a thread runs on a host
 * stack and the declared one is ignored, so the percentages a sweep reports
 * are not real headroom. What is checked here is the contract: the sweep reads
 * the thread list, the numbers stay inside their bounds, the threshold is
 * enforced, and a crossing is counted once. The measurement itself is only
 * meaningful on an MCU target.
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
