#include <errno.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#include <zephyr/ztest.h>

#include <kfsw/modules/gpredict.h>
#include <kfsw/services/parameter.h>

#include "gpredict_internal.h"

/* The travel the suite is built with, not the module's defaults. */
#define GP_AZ_MIN_MDEG (CONFIG_KFSW_GPREDICT_AZIMUTH_MIN_DEG * 1000)
#define GP_AZ_MAX_MDEG (CONFIG_KFSW_GPREDICT_AZIMUTH_MAX_DEG * 1000)
#define GP_EL_MIN_MDEG (CONFIG_KFSW_GPREDICT_ELEVATION_MIN_DEG * 1000)
#define GP_EL_MAX_MDEG (CONFIG_KFSW_GPREDICT_ELEVATION_MAX_DEG * 1000)
#define GP_PARK_AZ_MDEG (CONFIG_KFSW_GPREDICT_PARK_AZIMUTH_DEG * 1000)
#define GP_PARK_EL_MDEG (CONFIG_KFSW_GPREDICT_PARK_ELEVATION_DEG * 1000)
#define GP_GRACE_MS ((uint32_t)CONFIG_KFSW_GPREDICT_GRACE_MS)

#define GP_STATE_COUNT 5
#define GP_EVENT_COUNT 6

/*
 * The module's clock, offset so a test can jump it without sleeping and still
 * see real elapsed time when it does sleep.
 */
static int64_t clock_offset_ms;

uint64_t __wrap_kfsw_time_monotonic_ms(void)
{
	return (uint64_t)(k_uptime_get() + clock_offset_ms);
}

static const struct kfsw_gpredict_bearing mid_bearing = {
	.azimuth_mdeg = (GP_AZ_MIN_MDEG + GP_AZ_MAX_MDEG) / 2,
	.elevation_mdeg = (GP_EL_MIN_MDEG + GP_EL_MAX_MDEG) / 2,
};

static struct kfsw_gpredict_status read_status(void)
{
	struct kfsw_gpredict_status status;

	zassert_ok(kfsw_gpredict_get_status(&status));
	return status;
}

static uint8_t current_state(void)
{
	return read_status().state;
}

/* Reach a state through the live entry points, as flight would. */
static void enter(enum kfsw_gpredict_state target)
{
	gpredict_state_reset();

	switch (target) {
	case KFSW_GPREDICT_PARKED:
		break;
	case KFSW_GPREDICT_TRACKING:
		zassert_ok(kfsw_gpredict_bearing(&mid_bearing));
		break;
	case KFSW_GPREDICT_HOLDING:
		zassert_ok(kfsw_gpredict_bearing(&mid_bearing));
		gpredict_apply_event(KFSW_GPREDICT_EVENT_SILENCE);
		break;
	case KFSW_GPREDICT_PARKING:
		zassert_ok(kfsw_gpredict_bearing(&mid_bearing));
		gpredict_apply_event(KFSW_GPREDICT_EVENT_SILENCE);
		gpredict_apply_event(KFSW_GPREDICT_EVENT_SILENCE);
		break;
	case KFSW_GPREDICT_FAULT:
		gpredict_apply_event(KFSW_GPREDICT_EVENT_FAULT);
		break;
	}

	zassert_equal(current_state(), target, "could not reach %s",
		      kfsw_gpredict_state_name(target));
}

struct transition {
	enum kfsw_gpredict_state from;
	enum kfsw_gpredict_event event;
	enum kfsw_gpredict_state to;
};

static const struct transition transitions[] = {
	{KFSW_GPREDICT_PARKED, KFSW_GPREDICT_EVENT_BEARING, KFSW_GPREDICT_TRACKING},
	{KFSW_GPREDICT_PARKED, KFSW_GPREDICT_EVENT_SILENCE, KFSW_GPREDICT_PARKED},
	{KFSW_GPREDICT_PARKED, KFSW_GPREDICT_EVENT_PARKED, KFSW_GPREDICT_PARKED},
	{KFSW_GPREDICT_PARKED, KFSW_GPREDICT_EVENT_PARK_COMMANDED, KFSW_GPREDICT_PARKING},
	{KFSW_GPREDICT_PARKED, KFSW_GPREDICT_EVENT_FAULT, KFSW_GPREDICT_FAULT},
	{KFSW_GPREDICT_PARKED, KFSW_GPREDICT_EVENT_CLEARED, KFSW_GPREDICT_PARKED},

	{KFSW_GPREDICT_TRACKING, KFSW_GPREDICT_EVENT_BEARING, KFSW_GPREDICT_TRACKING},
	{KFSW_GPREDICT_TRACKING, KFSW_GPREDICT_EVENT_SILENCE, KFSW_GPREDICT_HOLDING},
	{KFSW_GPREDICT_TRACKING, KFSW_GPREDICT_EVENT_PARKED, KFSW_GPREDICT_TRACKING},
	{KFSW_GPREDICT_TRACKING, KFSW_GPREDICT_EVENT_PARK_COMMANDED, KFSW_GPREDICT_PARKING},
	{KFSW_GPREDICT_TRACKING, KFSW_GPREDICT_EVENT_FAULT, KFSW_GPREDICT_FAULT},
	{KFSW_GPREDICT_TRACKING, KFSW_GPREDICT_EVENT_CLEARED, KFSW_GPREDICT_TRACKING},

	{KFSW_GPREDICT_HOLDING, KFSW_GPREDICT_EVENT_BEARING, KFSW_GPREDICT_TRACKING},
	{KFSW_GPREDICT_HOLDING, KFSW_GPREDICT_EVENT_SILENCE, KFSW_GPREDICT_PARKING},
	{KFSW_GPREDICT_HOLDING, KFSW_GPREDICT_EVENT_PARKED, KFSW_GPREDICT_HOLDING},
	{KFSW_GPREDICT_HOLDING, KFSW_GPREDICT_EVENT_PARK_COMMANDED, KFSW_GPREDICT_PARKING},
	{KFSW_GPREDICT_HOLDING, KFSW_GPREDICT_EVENT_FAULT, KFSW_GPREDICT_FAULT},
	{KFSW_GPREDICT_HOLDING, KFSW_GPREDICT_EVENT_CLEARED, KFSW_GPREDICT_HOLDING},

	{KFSW_GPREDICT_PARKING, KFSW_GPREDICT_EVENT_BEARING, KFSW_GPREDICT_PARKING},
	{KFSW_GPREDICT_PARKING, KFSW_GPREDICT_EVENT_SILENCE, KFSW_GPREDICT_PARKING},
	{KFSW_GPREDICT_PARKING, KFSW_GPREDICT_EVENT_PARKED, KFSW_GPREDICT_PARKED},
	{KFSW_GPREDICT_PARKING, KFSW_GPREDICT_EVENT_PARK_COMMANDED, KFSW_GPREDICT_PARKING},
	{KFSW_GPREDICT_PARKING, KFSW_GPREDICT_EVENT_FAULT, KFSW_GPREDICT_FAULT},
	{KFSW_GPREDICT_PARKING, KFSW_GPREDICT_EVENT_CLEARED, KFSW_GPREDICT_PARKING},

	{KFSW_GPREDICT_FAULT, KFSW_GPREDICT_EVENT_BEARING, KFSW_GPREDICT_FAULT},
	{KFSW_GPREDICT_FAULT, KFSW_GPREDICT_EVENT_SILENCE, KFSW_GPREDICT_FAULT},
	{KFSW_GPREDICT_FAULT, KFSW_GPREDICT_EVENT_PARKED, KFSW_GPREDICT_FAULT},
	{KFSW_GPREDICT_FAULT, KFSW_GPREDICT_EVENT_PARK_COMMANDED, KFSW_GPREDICT_FAULT},
	{KFSW_GPREDICT_FAULT, KFSW_GPREDICT_EVENT_FAULT, KFSW_GPREDICT_FAULT},
	{KFSW_GPREDICT_FAULT, KFSW_GPREDICT_EVENT_CLEARED, KFSW_GPREDICT_PARKED},
};

static const struct kfsw_param_definition *find_definition(const char *name)
{
	for (size_t index = 0U; index < kfsw_gpredict_param_definitions.count; index++) {
		const struct kfsw_param_definition *definition =
			&kfsw_gpredict_param_definitions.definitions[index];

		if (strcmp(definition->name, name) == 0) {
			return definition;
		}
	}

	return NULL;
}

ZTEST(modules_gpredict, test_transition_matrix_is_total)
{
	bool covered[GP_STATE_COUNT][GP_EVENT_COUNT] = {0};

	zassert_equal(ARRAY_SIZE(transitions), GP_STATE_COUNT * GP_EVENT_COUNT);

	for (size_t index = 0U; index < ARRAY_SIZE(transitions); index++) {
		const struct transition *expected = &transitions[index];
		enum kfsw_gpredict_state next;

		next = kfsw_gpredict_next(expected->from, expected->event);
		zassert_equal(next, expected->to, "%s + event %d gave %s, not %s",
			      kfsw_gpredict_state_name(expected->from), (int)expected->event,
			      kfsw_gpredict_state_name(next),
			      kfsw_gpredict_state_name(expected->to));
		/* Pure: the same pair twice gives the same answer. */
		zassert_equal(kfsw_gpredict_next(expected->from, expected->event), next);
		zassert_false(covered[expected->from][expected->event], "pair listed twice");
		covered[expected->from][expected->event] = true;
	}

	for (int from = 0; from < GP_STATE_COUNT; from++) {
		for (int event = 0; event < GP_EVENT_COUNT; event++) {
			zassert_true(covered[from][event], "state %d event %d has no answer", from,
				     event);
		}
	}
}

ZTEST(modules_gpredict, test_parked_bearing_starts_tracking)
{
	struct kfsw_gpredict_status status;

	zassert_equal(current_state(), KFSW_GPREDICT_PARKED);
	zassert_ok(kfsw_gpredict_bearing(&mid_bearing));

	status = read_status();
	zassert_equal(status.state, KFSW_GPREDICT_TRACKING);
	zassert_equal(status.azimuth_mdeg, mid_bearing.azimuth_mdeg);
	zassert_equal(status.elevation_mdeg, mid_bearing.elevation_mdeg);
	zassert_equal(status.bearings, 1U);
	zassert_equal(status.refusals, 0U);
}

ZTEST(modules_gpredict, test_tracking_silence_holds)
{
	struct kfsw_gpredict_status status;

	enter(KFSW_GPREDICT_TRACKING);
	gpredict_apply_event(KFSW_GPREDICT_EVENT_SILENCE);

	status = read_status();
	zassert_equal(status.state, KFSW_GPREDICT_HOLDING);
	/* A predictor that stalled is not a pass that ended. */
	zassert_equal(status.passes_ended, 0U);
	zassert_equal(status.azimuth_mdeg, mid_bearing.azimuth_mdeg);
}

ZTEST(modules_gpredict, test_holding_bearing_resumes)
{
	struct kfsw_gpredict_bearing later = {
		.azimuth_mdeg = GP_AZ_MIN_MDEG,
		.elevation_mdeg = GP_EL_MAX_MDEG,
	};
	struct kfsw_gpredict_status status;

	enter(KFSW_GPREDICT_HOLDING);
	zassert_ok(kfsw_gpredict_bearing(&later));

	status = read_status();
	zassert_equal(status.state, KFSW_GPREDICT_TRACKING);
	zassert_equal(status.azimuth_mdeg, later.azimuth_mdeg);
	zassert_equal(status.elevation_mdeg, later.elevation_mdeg);
	zassert_equal(status.passes_ended, 0U);
}

ZTEST(modules_gpredict, test_holding_silence_ends_the_pass)
{
	struct kfsw_gpredict_status status;

	enter(KFSW_GPREDICT_HOLDING);
	gpredict_apply_event(KFSW_GPREDICT_EVENT_SILENCE);

	status = read_status();
	zassert_equal(status.state, KFSW_GPREDICT_PARKING);
	zassert_equal(status.passes_ended, 1U);

	/* A commanded park is not a pass that ended. */
	enter(KFSW_GPREDICT_TRACKING);
	zassert_ok(kfsw_gpredict_park());
	status = read_status();
	zassert_equal(status.state, KFSW_GPREDICT_PARKING);
	zassert_equal(status.passes_ended, 0U);
}

ZTEST(modules_gpredict, test_parking_bearing_stays_parking)
{
	struct kfsw_gpredict_status status;

	enter(KFSW_GPREDICT_PARKING);
	zassert_ok(kfsw_gpredict_bearing(&mid_bearing));

	status = read_status();
	zassert_equal(status.state, KFSW_GPREDICT_PARKING);

	/* The park finishes first, and the bearing after it starts the next pass. */
	gpredict_apply_event(KFSW_GPREDICT_EVENT_PARKED);
	zassert_equal(current_state(), KFSW_GPREDICT_PARKED);
	zassert_ok(kfsw_gpredict_bearing(&mid_bearing));
	zassert_equal(current_state(), KFSW_GPREDICT_TRACKING);
}

ZTEST(modules_gpredict, test_parking_parked_returns_to_parked)
{
	struct kfsw_gpredict_status status;

	enter(KFSW_GPREDICT_PARKING);
	gpredict_apply_event(KFSW_GPREDICT_EVENT_PARKED);

	status = read_status();
	zassert_equal(status.state, KFSW_GPREDICT_PARKED);
	/* "I reached park" says where the rotator is. */
	zassert_equal(status.actual_azimuth_mdeg, GP_PARK_AZ_MDEG);
	zassert_equal(status.actual_elevation_mdeg, GP_PARK_EL_MDEG);
}

ZTEST(modules_gpredict, test_park_commanded_from_each_state)
{
	const enum kfsw_gpredict_state reachable[] = {
		KFSW_GPREDICT_PARKED,
		KFSW_GPREDICT_TRACKING,
		KFSW_GPREDICT_HOLDING,
		KFSW_GPREDICT_PARKING,
	};

	for (size_t index = 0U; index < ARRAY_SIZE(reachable); index++) {
		enter(reachable[index]);
		zassert_ok(kfsw_gpredict_park(), "park refused from %s",
			   kfsw_gpredict_state_name(reachable[index]));
		zassert_equal(current_state(), KFSW_GPREDICT_PARKING);
	}
}

ZTEST(modules_gpredict, test_fault_from_each_state)
{
	const enum kfsw_gpredict_state every[] = {
		KFSW_GPREDICT_PARKED,  KFSW_GPREDICT_TRACKING, KFSW_GPREDICT_HOLDING,
		KFSW_GPREDICT_PARKING, KFSW_GPREDICT_FAULT,
	};

	for (size_t index = 0U; index < ARRAY_SIZE(every); index++) {
		const bool already_faulted = every[index] == KFSW_GPREDICT_FAULT;
		uint32_t before;

		enter(every[index]);
		before = read_status().faults;
		gpredict_apply_event(KFSW_GPREDICT_EVENT_FAULT);

		zassert_equal(current_state(), KFSW_GPREDICT_FAULT, "fault not held from %s",
			      kfsw_gpredict_state_name(every[index]));
		/* Counted on entry, not for every report that follows. */
		zassert_equal(read_status().faults, already_faulted ? before : before + 1U);
		zassert_equal(read_status().last_error, -EIO);
	}
}

ZTEST(modules_gpredict, test_fault_does_not_clear_itself)
{
	/* Every event but the operator clearing it leaves the fault in place. */
	for (int event = 0; event < GP_EVENT_COUNT; event++) {
		if (event == KFSW_GPREDICT_EVENT_CLEARED) {
			continue;
		}
		enter(KFSW_GPREDICT_FAULT);
		gpredict_apply_event((enum kfsw_gpredict_event)event);
		zassert_equal(current_state(), KFSW_GPREDICT_FAULT, "event %d left fault", event);
	}

	/* A bearing is the one an operator would expect to resume tracking. */
	enter(KFSW_GPREDICT_FAULT);
	zassert_equal(kfsw_gpredict_bearing(&mid_bearing), -EPERM);
	zassert_equal(current_state(), KFSW_GPREDICT_FAULT);
}

ZTEST(modules_gpredict, test_clear_from_fault_parks)
{
	enter(KFSW_GPREDICT_FAULT);
	zassert_ok(kfsw_gpredict_clear());
	zassert_equal(current_state(), KFSW_GPREDICT_PARKED);

	/* Tracking resumes only with the next bearing. */
	zassert_ok(kfsw_gpredict_bearing(&mid_bearing));
	zassert_equal(current_state(), KFSW_GPREDICT_TRACKING);
}

ZTEST(modules_gpredict, test_parked_silence_and_parked_stay_parked)
{
	zassert_equal(current_state(), KFSW_GPREDICT_PARKED);

	gpredict_apply_event(KFSW_GPREDICT_EVENT_SILENCE);
	zassert_equal(current_state(), KFSW_GPREDICT_PARKED);
	gpredict_apply_event(KFSW_GPREDICT_EVENT_PARKED);
	zassert_equal(current_state(), KFSW_GPREDICT_PARKED);
	zassert_equal(read_status().passes_ended, 0U);
}

ZTEST(modules_gpredict, test_state_name_covers_every_state)
{
	zassert_str_equal(kfsw_gpredict_state_name(KFSW_GPREDICT_PARKED), "parked");
	zassert_str_equal(kfsw_gpredict_state_name(KFSW_GPREDICT_TRACKING), "tracking");
	zassert_str_equal(kfsw_gpredict_state_name(KFSW_GPREDICT_HOLDING), "holding");
	zassert_str_equal(kfsw_gpredict_state_name(KFSW_GPREDICT_PARKING), "parking");
	zassert_str_equal(kfsw_gpredict_state_name(KFSW_GPREDICT_FAULT), "fault");
	zassert_str_equal(kfsw_gpredict_state_name((enum kfsw_gpredict_state)99), "unknown");
}

ZTEST(modules_gpredict, test_bearing_below_horizon_refused)
{
	const struct kfsw_gpredict_bearing below = {
		.azimuth_mdeg = mid_bearing.azimuth_mdeg,
		.elevation_mdeg = -1,
	};
	struct kfsw_gpredict_status status;

	enter(KFSW_GPREDICT_TRACKING);
	zassert_equal(kfsw_gpredict_bearing(&below), -EINVAL);

	status = read_status();
	zassert_equal(status.state, KFSW_GPREDICT_TRACKING);
	zassert_equal(status.refusals, 1U);
	zassert_equal(status.last_error, -EINVAL);
	/* The refused bearing did not displace the one being followed. */
	zassert_equal(status.elevation_mdeg, mid_bearing.elevation_mdeg);
	/* Zero is the horizon however the travel is configured. */
	zassert_true(CONFIG_KFSW_GPREDICT_ELEVATION_MIN_DEG >= 0);
}

ZTEST(modules_gpredict, test_bearing_outside_travel_refused)
{
	const struct kfsw_gpredict_bearing accepted[] = {
		{.azimuth_mdeg = GP_AZ_MIN_MDEG, .elevation_mdeg = GP_EL_MIN_MDEG},
		{.azimuth_mdeg = GP_AZ_MAX_MDEG, .elevation_mdeg = GP_EL_MAX_MDEG},
	};
	const struct kfsw_gpredict_bearing refused[] = {
		{.azimuth_mdeg = GP_AZ_MIN_MDEG - 1, .elevation_mdeg = GP_EL_MIN_MDEG},
		{.azimuth_mdeg = GP_AZ_MAX_MDEG + 1, .elevation_mdeg = GP_EL_MIN_MDEG},
		{.azimuth_mdeg = GP_AZ_MIN_MDEG, .elevation_mdeg = GP_EL_MIN_MDEG - 1},
		{.azimuth_mdeg = GP_AZ_MIN_MDEG, .elevation_mdeg = GP_EL_MAX_MDEG + 1},
	};

	/* The bounds themselves are inside the travel. */
	for (size_t index = 0U; index < ARRAY_SIZE(accepted); index++) {
		zassert_ok(kfsw_gpredict_bearing(&accepted[index]),
			   "the travel bound at az %d el %d was refused",
			   accepted[index].azimuth_mdeg, accepted[index].elevation_mdeg);
	}
	zassert_equal(read_status().bearings, ARRAY_SIZE(accepted));

	for (size_t index = 0U; index < ARRAY_SIZE(refused); index++) {
		zassert_equal(kfsw_gpredict_bearing(&refused[index]), -EINVAL,
			      "az %d el %d was accepted outside the travel",
			      refused[index].azimuth_mdeg, refused[index].elevation_mdeg);
	}
	zassert_equal(read_status().refusals, ARRAY_SIZE(refused));
	zassert_equal(read_status().bearings, ARRAY_SIZE(accepted));
}

ZTEST(modules_gpredict, test_bearing_null_refused)
{
	struct kfsw_gpredict_status status;

	enter(KFSW_GPREDICT_TRACKING);
	zassert_equal(kfsw_gpredict_bearing(NULL), -EINVAL);

	status = read_status();
	zassert_equal(status.state, KFSW_GPREDICT_TRACKING);
	/* Nothing was offered, so nothing was refused. */
	zassert_equal(status.refusals, 0U);
	zassert_equal(status.bearings, 1U);
}

ZTEST(modules_gpredict, test_bearing_in_fault_refused)
{
	struct kfsw_gpredict_status status;

	enter(KFSW_GPREDICT_FAULT);
	zassert_equal(kfsw_gpredict_bearing(&mid_bearing), -EPERM);

	status = read_status();
	zassert_equal(status.state, KFSW_GPREDICT_FAULT);
	zassert_equal(status.refusals, 1U);
	zassert_equal(status.last_error, -EPERM);
	zassert_equal(status.bearings, 0U);
}

ZTEST(modules_gpredict, test_park_in_fault_refused)
{
	enter(KFSW_GPREDICT_FAULT);
	/* Returning 0 would report a park the faulted rotator never made. */
	zassert_equal(kfsw_gpredict_park(), -EPERM);
	zassert_equal(current_state(), KFSW_GPREDICT_FAULT);
}

ZTEST(modules_gpredict, test_frequency_zero_refused_nonzero_stored)
{
	struct kfsw_gpredict_status status;

	zassert_equal(kfsw_gpredict_frequency(0U), -EINVAL);
	status = read_status();
	zassert_equal(status.frequency_hz, 0U);
	zassert_equal(status.last_error, -EINVAL);

	zassert_ok(kfsw_gpredict_frequency(437505000ULL));
	status = read_status();
	zassert_equal(status.frequency_hz, 437505000ULL);
	/* A frequency is tuning, not pointing: the pass does not start on it. */
	zassert_equal(status.state, KFSW_GPREDICT_PARKED);
}

ZTEST(modules_gpredict, test_clear_without_fault_is_ealready)
{
	const enum kfsw_gpredict_state unfaulted[] = {
		KFSW_GPREDICT_PARKED,
		KFSW_GPREDICT_TRACKING,
		KFSW_GPREDICT_HOLDING,
		KFSW_GPREDICT_PARKING,
	};

	for (size_t index = 0U; index < ARRAY_SIZE(unfaulted); index++) {
		enter(unfaulted[index]);
		zassert_equal(kfsw_gpredict_clear(), -EALREADY, "clear accepted from %s",
			      kfsw_gpredict_state_name(unfaulted[index]));
		zassert_equal(current_state(), unfaulted[index]);
	}
}

ZTEST(modules_gpredict, test_silence_ms_tracks_the_monotonic_clock)
{
	struct kfsw_gpredict_status status;

	/* Nothing has arrived, so there is no silence to report yet. */
	zassert_equal(read_status().silence_ms, 0U);

	zassert_ok(kfsw_gpredict_bearing(&mid_bearing));
	zassert_true(read_status().silence_ms < GP_GRACE_MS);

	clock_offset_ms += 1234;
	status = read_status();
	zassert_true(status.silence_ms >= 1234U, "silence_ms was %u after a 1234 ms jump",
		     status.silence_ms);
	zassert_true(status.silence_ms < 1234U + GP_GRACE_MS);

	/* A clock that went backwards reports no silence, not a huge one. */
	clock_offset_ms += 60000;
	zassert_ok(kfsw_gpredict_bearing(&mid_bearing));
	clock_offset_ms -= 5000;
	zassert_equal(read_status().silence_ms, 0U);
}

ZTEST(modules_gpredict, test_silence_work_ends_a_pass_without_a_rotator)
{
	struct kfsw_gpredict_status status;

	zassert_ok(kfsw_gpredict_bearing(&mid_bearing));
	zassert_equal(current_state(), KFSW_GPREDICT_TRACKING);

	/* One grace period holds, the second ends the pass. */
	k_sleep(K_MSEC(GP_GRACE_MS + (GP_GRACE_MS / 2U)));
	zassert_equal(current_state(), KFSW_GPREDICT_HOLDING);

	k_sleep(K_MSEC(GP_GRACE_MS));
	status = read_status();
	zassert_equal(status.state, KFSW_GPREDICT_PARKING);
	zassert_equal(status.passes_ended, 1U);

	/* The timer stops once the pass is over. */
	k_sleep(K_MSEC(GP_GRACE_MS * 2U));
	status = read_status();
	zassert_equal(status.state, KFSW_GPREDICT_PARKING);
	zassert_equal(status.passes_ended, 1U);
}

ZTEST(modules_gpredict, test_silence_check_is_quiet_within_the_grace_period)
{
	zassert_ok(kfsw_gpredict_bearing(&mid_bearing));

	clock_offset_ms += GP_GRACE_MS - 1;
	gpredict_silence_check();
	zassert_equal(current_state(), KFSW_GPREDICT_TRACKING);

	clock_offset_ms += 1;
	gpredict_silence_check();
	zassert_equal(current_state(), KFSW_GPREDICT_HOLDING);
}

ZTEST(modules_gpredict, test_counters_saturate)
{
	const struct kfsw_gpredict_bearing outside = {
		.azimuth_mdeg = GP_AZ_MAX_MDEG + 1,
		.elevation_mdeg = GP_EL_MIN_MDEG,
	};
	struct kfsw_gpredict_status status;

	gpredict_test_set_counters(UINT32_MAX);

	zassert_ok(kfsw_gpredict_bearing(&mid_bearing));
	zassert_equal(kfsw_gpredict_bearing(&outside), -EINVAL);
	gpredict_apply_event(KFSW_GPREDICT_EVENT_SILENCE);
	gpredict_apply_event(KFSW_GPREDICT_EVENT_SILENCE);
	gpredict_apply_event(KFSW_GPREDICT_EVENT_FAULT);

	status = read_status();
	zassert_equal(status.bearings, UINT32_MAX);
	zassert_equal(status.refusals, UINT32_MAX);
	zassert_equal(status.passes_ended, UINT32_MAX);
	zassert_equal(status.faults, UINT32_MAX);
}

ZTEST(modules_gpredict, test_get_status_null_refused)
{
	zassert_equal(kfsw_gpredict_get_status(NULL), -EINVAL);
}

ZTEST(modules_gpredict, test_param_table_is_in_the_module_band)
{
	static const struct {
		const char *name;
		uint8_t offset;
		enum kfsw_param_type type;
		bool writable;
	} expected[] = {
		{"gp_state", 0x00U, KFSW_PARAM_U8, false},
		{"gp_az_mdeg", 0x04U, KFSW_PARAM_I32, false},
		{"gp_el_mdeg", 0x08U, KFSW_PARAM_I32, false},
		{"gp_actual_az_mdeg", 0x0cU, KFSW_PARAM_I32, false},
		{"gp_actual_el_mdeg", 0x10U, KFSW_PARAM_I32, false},
		{"gp_frequency_hz", 0x18U, KFSW_PARAM_U64, false},
		{"gp_silence_ms", 0x20U, KFSW_PARAM_U32, false},
		{"gp_grace_ms", 0x24U, KFSW_PARAM_U32, true},
		{"gp_bearings", 0x28U, KFSW_PARAM_U32, false},
		{"gp_refusals", 0x2cU, KFSW_PARAM_U32, false},
		{"gp_passes_ended", 0x30U, KFSW_PARAM_U32, false},
		{"gp_faults", 0x34U, KFSW_PARAM_U32, false},
		{"gp_last_error", 0x38U, KFSW_PARAM_I32, false},
	};

	zassert_equal(KFSW_GPREDICT_TABLE_ID, 52U);
	zassert_str_equal(KFSW_GPREDICT_TABLE_NAME, "gpredict");
	zassert_str_equal(kfsw_param_band_name(KFSW_GPREDICT_TABLE_ID), "module");
	zassert_equal(kfsw_gpredict_param_definitions.table, KFSW_GPREDICT_TABLE_ID);
	zassert_str_equal(kfsw_gpredict_param_definitions.name, KFSW_GPREDICT_TABLE_NAME);
	zassert_equal(kfsw_gpredict_param_definitions.count, ARRAY_SIZE(expected));

	for (size_t index = 0U; index < ARRAY_SIZE(expected); index++) {
		const struct kfsw_param_definition *definition =
			find_definition(expected[index].name);
		struct kfsw_param_info info;

		zassert_not_null(definition, "%s is missing", expected[index].name);
		zassert_equal(definition->offset, expected[index].offset, "%s moved",
			      expected[index].name);
		zassert_equal(definition->type, expected[index].type, "%s changed type",
			      expected[index].name);
		zassert_ok(kfsw_param_get_info(expected[index].name, &info));
		zassert_equal(info.table, KFSW_GPREDICT_TABLE_ID);
		zassert_equal(info.read_only, !expected[index].writable, "%s is the wrong mode",
			      expected[index].name);
		/* Nothing here survives a reset: the tracker is rebuilt at boot. */
		zassert_false((info.flags & KFSW_PARAM_FLAG_PERSISTENT) != 0U);
	}
}

ZTEST(modules_gpredict, test_param_grace_write_applies_and_refuses)
{
	struct kfsw_param_value value = {
		.type = KFSW_PARAM_U32,
		.size = sizeof(uint32_t),
		.scalar.u32 = 9000U,
	};

	zassert_equal(read_status().grace_ms, GP_GRACE_MS);

	zassert_ok(kfsw_param_set("gp_grace_ms", &value));
	zassert_equal(read_status().grace_ms, 9000U);
	zassert_ok(kfsw_param_get("gp_grace_ms", &value));
	zassert_equal(value.scalar.u32, 9000U);

	/* Below what the owner accepts: the old value stands. */
	value.scalar.u32 = KFSW_GPREDICT_GRACE_MIN_MS - 1U;
	zassert_equal(kfsw_param_set("gp_grace_ms", &value), -ERANGE);
	zassert_equal(read_status().grace_ms, 9000U);

	value.scalar.u32 = KFSW_GPREDICT_GRACE_MAX_MS + 1U;
	zassert_equal(kfsw_param_set("gp_grace_ms", &value), -ERANGE);
	zassert_equal(read_status().grace_ms, 9000U);

	value.scalar.u32 = KFSW_GPREDICT_GRACE_MIN_MS;
	zassert_ok(kfsw_param_set("gp_grace_ms", &value));
	zassert_equal(read_status().grace_ms, KFSW_GPREDICT_GRACE_MIN_MS);
}

ZTEST(modules_gpredict, test_param_reads_refuse_writes_to_the_tracker)
{
	struct kfsw_param_value value;

	enter(KFSW_GPREDICT_TRACKING);
	zassert_ok(kfsw_param_get("gp_state", &value));
	zassert_equal(value.scalar.u8, KFSW_GPREDICT_TRACKING);

	value.scalar.u8 = KFSW_GPREDICT_PARKED;
	zassert_equal(kfsw_param_set("gp_state", &value), -EACCES);
	zassert_equal(current_state(), KFSW_GPREDICT_TRACKING);

	zassert_ok(kfsw_param_get("gp_bearings", &value));
	zassert_equal(value.scalar.u32, 1U);
}

ZTEST(modules_gpredict, test_init_is_idempotent_and_keeps_counters)
{
	zassert_ok(kfsw_gpredict_init());
	zassert_ok(kfsw_gpredict_bearing(&mid_bearing));

	/* A second init must not wipe a lifetime counter. */
	zassert_ok(kfsw_gpredict_init());
	zassert_equal(read_status().bearings, 1U);
	zassert_equal(current_state(), KFSW_GPREDICT_TRACKING);
}

static void *gpredict_setup(void)
{
	const struct kfsw_param_definition_set *const sets[] = {
		&kfsw_gpredict_param_definitions,
	};

	zassert_ok(kfsw_param_init(sets, ARRAY_SIZE(sets)));
	zassert_ok(kfsw_gpredict_init());
	return NULL;
}

static void gpredict_before(void *fixture)
{
	ARG_UNUSED(fixture);

	clock_offset_ms = 0;
	gpredict_state_reset();
}

ZTEST_SUITE(modules_gpredict, NULL, gpredict_setup, gpredict_before, NULL, NULL);
