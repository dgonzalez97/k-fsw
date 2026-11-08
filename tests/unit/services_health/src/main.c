#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#include <zephyr/ztest.h>

#include <kfsw/services/health.h>

/* No watchdog device on this target, so supervision can't start. These cases
 * test the bookkeeping and the decision; resets are tested on hardware.
 */

#define SHORT_DEADLINE_MS 60U

static uint32_t configured_timeout;

uint32_t __wrap_kfsw_platform_watchdog_configured_timeout_ms(void)
{
	return configured_timeout;
}

static void *health_setup(void)
{
#if CONFIG_KFSW_HEALTH_SLOTS
	const struct kfsw_param_definition_set *sets[] = {&kfsw_health_param_definitions};

	zassert_ok(kfsw_param_init(sets, ARRAY_SIZE(sets)));
#endif
	return NULL;
}

/* The table is fixed, so each test unregisters what it registers. */
static void health_before(void *fixture)
{
	ARG_UNUSED(fixture);
	configured_timeout = 0U;
	zassert_ok(kfsw_health_set_interval_ms(CONFIG_KFSW_HEALTH_INTERVAL_MS));

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

	/* A zero deadline is refused. */
	zassert_equal(kfsw_health_register("zero", 0U, &handle), -EINVAL);
}

ZTEST(services_health, test_interval_is_bounded_before_watchdog_initialization)
{
	configured_timeout = 8000U;
	zassert_ok(kfsw_health_set_interval_ms(2666U));
	zassert_equal(kfsw_health_set_interval_ms(2667U), -ERANGE);
	zassert_equal(kfsw_health_get_interval_ms(), 2666U);
	zassert_equal(kfsw_health_check_interval_ms(0U), -EINVAL);
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

	/* A full table refuses more; earlier cases may have used some slots. */
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
	/* A stopped service must be able to unregister. */
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
	/* Without a watchdog there is nothing to withhold, so this must not report healthy. */
	zassert_equal(kfsw_health_evaluate(), -EINVAL);
}

ZTEST(services_health, test_supervision_needs_a_watchdog_to_take_over)
{
	/* No watchdog device, so starting fails. */
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

#if CONFIG_KFSW_HEALTH_SLOTS
/* --------------------------------------------------------- per-component slots */

/* health_test_slots.h names alpha at index 0, gamma at 1 and beta at 3. */
#define ALPHA_STATE_OFFSET 0x20U
#define ALPHA_SINCE_OFFSET 0x24U
#define GAMMA_STATE_OFFSET 0x28U
#define BETA_STATE_OFFSET 0x38U

static uint8_t read_u8(const char *name)
{
	struct kfsw_param_value value;

	zassert_ok(kfsw_param_get(name, &value), "%s", name);
	zassert_equal(value.type, KFSW_PARAM_U8, "%s", name);
	return value.scalar.u8;
}

static uint32_t read_u32(const char *name)
{
	struct kfsw_param_value value;

	zassert_ok(kfsw_param_get(name, &value), "%s", name);
	zassert_equal(value.type, KFSW_PARAM_U32, "%s", name);
	return value.scalar.u32;
}

static uint8_t offset_of(const char *name)
{
	struct kfsw_param_info info;

	zassert_ok(kfsw_param_get_info(name, &info), "%s", name);
	zassert_equal(info.table, KFSW_HEALTH_PARAM_TABLE_ID, "%s", name);
	return info.offset;
}

ZTEST(services_health, test_a_registered_slot_reads_its_state_and_age)
{
	struct kfsw_param_value by_id;
	uint32_t since;
	uint8_t handle = 0U;

	zassert_ok(kfsw_health_register("alpha", 5000U, &handle));
	zassert_equal(read_u8("health_alpha_state"), KFSW_HEALTH_SLOT_OK);
	since = read_u32("health_alpha_since_ms");
	zassert_true(since < 5000U, "just registered, so well inside its deadline");

	k_sleep(K_MSEC(30));
	zassert_true(read_u32("health_alpha_since_ms") >= since + 30U,
		     "the age is sampled on read, not latched at registration");

	/* The ground addresses it by table and offset. */
	zassert_equal(offset_of("health_alpha_state"), ALPHA_STATE_OFFSET);
	zassert_equal(offset_of("health_alpha_since_ms"), ALPHA_SINCE_OFFSET);
	zassert_ok(kfsw_param_get_by_id(
		KFSW_PARAM_ID(KFSW_HEALTH_PARAM_TABLE_ID, ALPHA_STATE_OFFSET), &by_id));
	zassert_equal(by_id.scalar.u8, KFSW_HEALTH_SLOT_OK);

	zassert_ok(kfsw_health_report(handle));
	zassert_true(read_u32("health_alpha_since_ms") < 30U, "a report moves the age back");
}

ZTEST(services_health, test_an_overdue_slot_reads_overdue)
{
	uint8_t handle = 0U;

	zassert_ok(kfsw_health_register("alpha", SHORT_DEADLINE_MS, &handle));
	k_sleep(K_MSEC(SHORT_DEADLINE_MS + (SHORT_DEADLINE_MS / 2U)));

	zassert_equal(read_u8("health_alpha_state"), KFSW_HEALTH_SLOT_OVERDUE);
	zassert_true(read_u32("health_alpha_since_ms") > SHORT_DEADLINE_MS);

	/* Observation only: the service state and its decision are untouched. */
	zassert_equal(read_u8("health_state"), KFSW_HEALTH_STOPPED);
	zassert_equal(kfsw_health_evaluate(), -EINVAL);

	/* A report clears it: the slot follows the component, it does not latch. */
	zassert_ok(kfsw_health_report(handle));
	zassert_equal(read_u8("health_alpha_state"), KFSW_HEALTH_SLOT_OK);
}

ZTEST(services_health, test_an_unregistered_slot_reads_absent)
{
	uint8_t handle = 0U;

	zassert_equal(read_u8("health_beta_state"), KFSW_HEALTH_SLOT_ABSENT);
	zassert_equal(read_u32("health_beta_since_ms"), KFSW_HEALTH_SLOT_SINCE_ABSENT);
	zassert_not_equal(KFSW_HEALTH_SLOT_ABSENT, KFSW_HEALTH_SLOT_OK);

	/* A component that stops being watched goes back to absent. */
	zassert_ok(kfsw_health_register("alpha", 5000U, &handle));
	zassert_equal(read_u8("health_alpha_state"), KFSW_HEALTH_SLOT_OK);
	zassert_ok(kfsw_health_unregister(handle));
	zassert_equal(read_u8("health_alpha_state"), KFSW_HEALTH_SLOT_ABSENT);
	zassert_equal(read_u32("health_alpha_since_ms"), KFSW_HEALTH_SLOT_SINCE_ABSENT);
}

ZTEST(services_health, test_registration_order_does_not_pick_the_slot)
{
	struct kfsw_param_info info;
	uint8_t other = 0U;
	uint8_t beta = 0U;

	/* beta takes the second registration entry but keeps index 3's offset. */
	zassert_ok(kfsw_health_register("zeta", 5000U, &other));
	zassert_ok(kfsw_health_register("beta", 5000U, &beta));
	zassert_not_equal(beta, 3U);

	zassert_equal(offset_of("health_beta_state"), BETA_STATE_OFFSET);
	zassert_equal(read_u8("health_beta_state"), KFSW_HEALTH_SLOT_OK);
	zassert_equal(offset_of("health_gamma_state"), GAMMA_STATE_OFFSET);
	zassert_equal(read_u8("health_gamma_state"), KFSW_HEALTH_SLOT_ABSENT);

	/* A component the composition does not name has no slot. */
	zassert_equal(kfsw_param_get_info("health_zeta_state", &info), -ENOENT);
}

ZTEST(services_health, test_slots_leave_the_aggregate_offsets_alone)
{
	struct kfsw_param_value value = {.type = KFSW_PARAM_U8, .size = sizeof(uint8_t)};

	zassert_equal(offset_of("health_faulted_by"), 0x00U);
	zassert_equal(offset_of("health_faults"), 0x10U);
	zassert_equal(offset_of("health_feeds"), 0x14U);
	zassert_equal(offset_of("health_interval_ms"), 0x18U);
	zassert_equal(offset_of("health_state"), 0x1aU);
	zassert_equal(offset_of("health_feeding"), 0x1bU);
	zassert_equal(offset_of("health_components"), 0x1cU);

	/* Written only by the service, never by an operator. */
	value.scalar.u8 = KFSW_HEALTH_SLOT_OK;
	zassert_equal(kfsw_param_set("health_alpha_state", &value), -EACCES);
}
#endif

ZTEST_SUITE(services_health, NULL, health_setup, health_before, NULL, NULL);
