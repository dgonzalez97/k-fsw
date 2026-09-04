#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#include <zephyr/ztest.h>

#include <kfsw/services/health.h>

/* This target has no watchdog device, so supervision cannot be started here:
 * taking the watchdog over is what makes withholding a feed mean anything, and
 * a service that pretended to supervise without one would be worse than one
 * that refuses. What is tested here is the bookkeeping and the decision, which
 * is where a mistake would quietly keep feeding a system that had stopped
 * working. Actually resetting a part belongs to the hardware acceptance.
 */

#define SHORT_DEADLINE_MS 60U

static void *health_setup(void)
{
	return NULL;
}

/* The table is fixed and registration is permanent until something
 * unregisters, so a test that fills it would starve every test after it. Each
 * one clears up after itself, which also exercises unregistration.
 */
static void health_before(void *fixture)
{
	ARG_UNUSED(fixture);

	for (uint8_t index = 0U; index < KFSW_HEALTH_MAX_COMPONENTS; index++) {
		(void)kfsw_health_unregister(index);
	}
}

/* --------------------------------------------------------------- registration */

ZTEST(services_health, test_registration_rejects_nonsense)
{
	uint8_t handle = 0U;

	zassert_equal(kfsw_health_register(NULL, 100U, &handle), -EINVAL);
	zassert_equal(kfsw_health_register("x", 100U, NULL), -EINVAL);
	zassert_equal(kfsw_health_register("", 100U, &handle), -EINVAL);

	/* A deadline of zero would be missed the instant it was set, so a
	 * component registered that way would reset the board immediately. */
	zassert_equal(kfsw_health_register("zero", 0U, &handle), -EINVAL);
}

ZTEST(services_health, test_a_name_is_registered_once)
{
	uint8_t first = 0U;
	uint8_t second = 0U;

	zassert_ok(kfsw_health_register("radio", 500U, &first));
	zassert_equal(kfsw_health_register("radio", 500U, &second), -EEXIST,
		      "registering twice would let one component satisfy the other's deadline");
}

ZTEST(services_health, test_the_table_is_bounded)
{
	uint8_t handle = 0U;
	char name[8];
	int registered = 0;

	/* The table is sized at build time; filling it must be refused rather
	 * than overrun. Earlier cases have taken some slots, so this counts
	 * what it manages rather than assuming an empty table. */
	for (int index = 0; index < KFSW_HEALTH_MAX_COMPONENTS + 4; index++) {
		(void)snprintk(name, sizeof(name), "c%d", index);
		if (kfsw_health_register(name, 500U, &handle) == 0) {
			registered++;
		}
	}

	zassert_equal(registered, KFSW_HEALTH_MAX_COMPONENTS);
	zassert_equal(kfsw_health_register("overflow", 500U, &handle), -ENOSPC);
}

ZTEST(services_health, test_unregistering_frees_a_slot)
{
	/* A service can be stopped deliberately. One that is no longer running
	 * but still watched would miss its deadline and reset a board that is
	 * working exactly as intended.
	 */
	uint8_t handle = 0U;
	char name[8];

	for (int index = 0; index < KFSW_HEALTH_MAX_COMPONENTS; index++) {
		(void)snprintk(name, sizeof(name), "f%d", index);
		zassert_ok(kfsw_health_register(name, 500U, &handle));
	}
	zassert_equal(kfsw_health_register("one-more", 500U, &handle), -ENOSPC);

	zassert_ok(kfsw_health_unregister(0U));
	zassert_ok(kfsw_health_register("one-more", 500U, &handle));

	/* And an unregistered component can no longer report. */
	zassert_ok(kfsw_health_unregister(handle));
	zassert_equal(kfsw_health_report(handle), -EINVAL);
	zassert_equal(kfsw_health_unregister(handle), -EINVAL);
}

ZTEST(services_health, test_reporting_needs_a_registered_handle)
{
	zassert_equal(kfsw_health_report(KFSW_HEALTH_MAX_COMPONENTS), -EINVAL);
	zassert_equal(kfsw_health_report(200U), -EINVAL);
}

/* ------------------------------------------------------------- the decision */

ZTEST(services_health, test_evaluate_is_refused_before_supervision_starts)
{
	/* Without a watchdog there is nothing to withhold. Reporting healthy
	 * here would be a lie that costs nothing to tell and everything to
	 * believe. */
	zassert_equal(kfsw_health_evaluate(), -EINVAL);
}

ZTEST(services_health, test_supervision_needs_a_watchdog_to_take_over)
{
	/* No watchdog device on this target, so starting must fail rather than
	 * appear to supervise. */
	zassert_equal(kfsw_health_start(), -ENODEV);
}

ZTEST(services_health, test_a_component_is_late_before_it_is_overdue)
{
	struct kfsw_health_component component;
	uint8_t handle = 0U;

	zassert_ok(kfsw_health_register("slow", SHORT_DEADLINE_MS, &handle));
	zassert_ok(kfsw_health_get_component(handle, &component));
	zassert_false(component.overdue, "a component just registered has not missed anything");

	k_sleep(K_MSEC(SHORT_DEADLINE_MS / 2U));
	zassert_ok(kfsw_health_get_component(handle, &component));
	zassert_false(component.overdue, "half a deadline is late, not overdue");

	k_sleep(K_MSEC(SHORT_DEADLINE_MS));
	zassert_ok(kfsw_health_get_component(handle, &component));
	zassert_true(component.overdue, "past its deadline it must be overdue");

	/* And reporting clears it: this is recovery, not a latch. */
	zassert_ok(kfsw_health_report(handle));
	zassert_ok(kfsw_health_get_component(handle, &component));
	zassert_false(component.overdue);
}

ZTEST(services_health, test_reports_are_counted_and_reset_the_clock)
{
	struct kfsw_health_component before;
	struct kfsw_health_component after;
	uint8_t handle = 0U;

	zassert_ok(kfsw_health_register("counted", 5000U, &handle));
	zassert_ok(kfsw_health_get_component(handle, &before));

	k_sleep(K_MSEC(30));
	zassert_ok(kfsw_health_report(handle));
	zassert_ok(kfsw_health_get_component(handle, &after));

	zassert_equal(after.reports, before.reports + 1U);
	zassert_true(after.since_report_ms < before.since_report_ms + 30U,
		     "reporting must move the clock back to now");
}

/* ----------------------------------------------------------------- reporting */

ZTEST(services_health, test_status_starts_stopped_and_counts_components)
{
	struct kfsw_health_status status;
	uint8_t handle = 0U;

	zassert_ok(kfsw_health_get_status(&status));
	zassert_equal(status.state, KFSW_HEALTH_STOPPED,
		      "supervision cannot start without a watchdog on this target");
	zassert_false(status.feeding);

	zassert_equal(status.count, 0U, "the table is cleared before each case");

	zassert_ok(kfsw_health_register("counting", 500U, &handle));
	zassert_ok(kfsw_health_get_status(&status));
	zassert_equal(status.count, 1U);

	zassert_ok(kfsw_health_unregister(handle));
	zassert_ok(kfsw_health_get_status(&status));
	zassert_equal(status.count, 0U);
}

ZTEST(services_health, test_component_entries_are_reported)
{
	struct kfsw_health_component component;
	uint8_t handle = 0U;

	zassert_ok(kfsw_health_register("named", 1234U, &handle));
	zassert_ok(kfsw_health_get_component(handle, &component));

	zassert_str_equal(component.name, "named");
	zassert_equal(component.deadline_ms, 1234U);
	zassert_equal(kfsw_health_get_component(KFSW_HEALTH_MAX_COMPONENTS, &component), -ENOENT);
	zassert_equal(kfsw_health_get_component(0U, NULL), -EINVAL);
}

ZTEST(services_health, test_a_long_name_is_truncated_not_overrun)
{
	struct kfsw_health_component component;
	uint8_t handle = 0U;

	zassert_ok(kfsw_health_register("a-name-far-longer-than-the-space-for-it", 500U, &handle));
	zassert_ok(kfsw_health_get_component(handle, &component));
	zassert_equal(strlen(component.name), KFSW_HEALTH_NAME_SIZE - 1U);
}

ZTEST(services_health, test_get_status_rejects_null)
{
	zassert_equal(kfsw_health_get_status(NULL), -EINVAL);
}

ZTEST(services_health, test_state_names_are_reported)
{
	zassert_str_equal(kfsw_health_state_name(KFSW_HEALTH_OK), "ok");
	zassert_str_equal(kfsw_health_state_name(KFSW_HEALTH_FAULTED), "faulted");
	zassert_str_equal(kfsw_health_state_name(KFSW_HEALTH_STOPPED), "stopped");
	zassert_str_equal(kfsw_health_state_name((enum kfsw_health_state)77), "unknown");
}

ZTEST_SUITE(services_health, NULL, health_setup, health_before, NULL, NULL);
