#include <errno.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#include <zephyr/ztest.h>

#include <kfsw/modules/gpredict.h>
#include <kfsw/services/command.h>
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

#define GP_PROFILE_COUNT ((uint8_t)CONFIG_KFSW_GPREDICT_PROFILES)
#define GP_CATALOGUE 43017U
#define GP_DOWNLINK_HZ 437505000ULL
#define GP_HOLD_HZ 145800000ULL
/* A value no judgement could produce, so an untouched destination shows. */
#define GP_UNTOUCHED_HZ 1ULL

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
		{"gp_profile", 0x3cU, KFSW_PARAM_U8, false},
		{"gp_policy", 0x3dU, KFSW_PARAM_U8, false},
		{"gp_profiles", 0x3eU, KFSW_PARAM_U8, false},
		{"gp_profiles_on", 0x3fU, KFSW_PARAM_U8, false},
		{"gp_catalogue", 0x40U, KFSW_PARAM_U32, false},
		{"gp_profile_name", 0x44U, KFSW_PARAM_STRING, false},
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

	/* A name that fits a profile fits the row that reports it. */
	zassert_equal(find_definition("gp_profile_name")->capacity, KFSW_GPREDICT_PROFILE_NAME_MAX);
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

/* A profile that allows the whole compiled travel and follows Gpredict. */
static struct kfsw_gpredict_profile make_profile(const char *name)
{
	struct kfsw_gpredict_profile profile = {
		.catalogue = GP_CATALOGUE,
		.azimuth_min_mdeg = GP_AZ_MIN_MDEG,
		.azimuth_max_mdeg = GP_AZ_MAX_MDEG,
		.elevation_min_mdeg = GP_EL_MIN_MDEG,
		.elevation_max_mdeg = GP_EL_MAX_MDEG,
		.frequency_hz = GP_HOLD_HZ,
		.policy = KFSW_GPREDICT_FREQUENCY_FOLLOW,
		.enabled = true,
	};

	(void)strncpy(profile.name, name, sizeof(profile.name) - 1U);
	return profile;
}

static void define_and_select(uint8_t index, const struct kfsw_gpredict_profile *profile)
{
	zassert_ok(kfsw_gpredict_profile_define(index, profile));
	zassert_ok(kfsw_gpredict_profile_select(index));
}

ZTEST(modules_gpredict, test_profile_define_and_get)
{
	const struct kfsw_gpredict_profile lur = make_profile("LUR-1");
	struct kfsw_gpredict_profile refused;
	struct kfsw_gpredict_profile read;

	zassert_ok(kfsw_gpredict_profile_define(0U, &lur));
	zassert_ok(kfsw_gpredict_profile_get(0U, &read));
	zassert_str_equal(read.name, "LUR-1");
	zassert_equal(read.catalogue, GP_CATALOGUE);
	zassert_equal(read.azimuth_min_mdeg, GP_AZ_MIN_MDEG);
	zassert_equal(read.azimuth_max_mdeg, GP_AZ_MAX_MDEG);
	zassert_equal(read.elevation_min_mdeg, GP_EL_MIN_MDEG);
	zassert_equal(read.elevation_max_mdeg, GP_EL_MAX_MDEG);
	zassert_equal(read.frequency_hz, GP_HOLD_HZ);
	zassert_equal(read.policy, KFSW_GPREDICT_FREQUENCY_FOLLOW);
	zassert_true(read.enabled);

	/* The last slot is a slot; the one after it is not. */
	zassert_ok(kfsw_gpredict_profile_define(GP_PROFILE_COUNT - 1U, &lur));
	zassert_equal(kfsw_gpredict_profile_define(GP_PROFILE_COUNT, &lur), -ENOSPC);
	zassert_equal(kfsw_gpredict_profile_define(0U, NULL), -EINVAL);

	refused = lur;
	refused.name[0] = '\0';
	zassert_equal(kfsw_gpredict_profile_define(1U, &refused), -EINVAL, "empty name accepted");

	refused = lur;
	(void)memset(refused.name, 'X', sizeof(refused.name));
	zassert_equal(kfsw_gpredict_profile_define(1U, &refused), -EINVAL,
		      "unterminated name accepted");

	refused = lur;
	refused.policy = KFSW_GPREDICT_FREQUENCY_IGNORE + 1U;
	zassert_equal(kfsw_gpredict_profile_define(1U, &refused), -EINVAL,
		      "unknown policy accepted");

	refused = lur;
	refused.policy = KFSW_GPREDICT_FREQUENCY_HOLD;
	refused.frequency_hz = 0U;
	zassert_equal(kfsw_gpredict_profile_define(1U, &refused), -EINVAL,
		      "HOLD with nothing to hold accepted");

	refused = lur;
	refused.azimuth_min_mdeg = mid_bearing.azimuth_mdeg + 1;
	refused.azimuth_max_mdeg = mid_bearing.azimuth_mdeg;
	zassert_equal(kfsw_gpredict_profile_define(1U, &refused), -EINVAL,
		      "azimuth minimum above maximum accepted");

	refused = lur;
	refused.elevation_min_mdeg = mid_bearing.elevation_mdeg + 1;
	refused.elevation_max_mdeg = mid_bearing.elevation_mdeg;
	zassert_equal(kfsw_gpredict_profile_define(1U, &refused), -EINVAL,
		      "elevation minimum above maximum accepted");

	/* Nothing refused landed in a slot. */
	zassert_equal(kfsw_gpredict_profile_get(1U, &read), -ENOENT);

	/* A frequency is only read under HOLD, so FOLLOW does not need one. */
	refused = lur;
	refused.frequency_hz = 0U;
	zassert_ok(kfsw_gpredict_profile_define(1U, &refused));

	zassert_equal(kfsw_gpredict_profile_get(0U, NULL), -EINVAL);
	zassert_equal(kfsw_gpredict_profile_get(GP_PROFILE_COUNT, &read), -ENOENT);
}

ZTEST(modules_gpredict, test_profile_get_empty_slot_is_enoent)
{
	struct kfsw_gpredict_profile read;

	zassert_equal(kfsw_gpredict_profile_get(0U, &read), -ENOENT);
	zassert_equal(kfsw_gpredict_profile_enable(0U, true), -ENOENT);
}

ZTEST(modules_gpredict, test_profile_select_and_selected)
{
	const struct kfsw_gpredict_profile lur = make_profile("LUR-1");
	const struct kfsw_gpredict_profile other = make_profile("OTHER");
	struct kfsw_gpredict_status before;
	struct kfsw_gpredict_status after;

	zassert_equal(kfsw_gpredict_profile_selected(), -ENOENT);
	zassert_ok(kfsw_gpredict_profile_define(0U, &lur));
	zassert_ok(kfsw_gpredict_profile_define(1U, &other));
	/* Defining is not selecting. */
	zassert_equal(kfsw_gpredict_profile_selected(), -ENOENT);

	enter(KFSW_GPREDICT_TRACKING);
	before = read_status();
	zassert_ok(kfsw_gpredict_profile_select(1U));
	zassert_equal(kfsw_gpredict_profile_selected(), 1);

	/* Selecting does not move the antenna or touch the pass. */
	after = read_status();
	zassert_equal(after.state, KFSW_GPREDICT_TRACKING);
	zassert_equal(after.bearings, before.bearings);
	zassert_equal(after.azimuth_mdeg, before.azimuth_mdeg);

	zassert_equal(kfsw_gpredict_profile_select(2U), -ENOENT);
	zassert_equal(kfsw_gpredict_profile_select(GP_PROFILE_COUNT), -ENOENT);
	zassert_equal(kfsw_gpredict_profile_selected(), 1);

	zassert_ok(kfsw_gpredict_profile_select(0U));
	zassert_equal(kfsw_gpredict_profile_selected(), 0);
}

ZTEST(modules_gpredict, test_profile_select_disabled_is_eperm)
{
	const struct kfsw_gpredict_profile lur = make_profile("LUR-1");
	struct kfsw_gpredict_profile off = make_profile("OFF");

	off.enabled = false;
	define_and_select(0U, &lur);
	zassert_ok(kfsw_gpredict_profile_define(1U, &off));

	zassert_equal(kfsw_gpredict_profile_select(1U), -EPERM);
	/* The refusal leaves the rules that were in force. */
	zassert_equal(kfsw_gpredict_profile_selected(), 0);

	zassert_ok(kfsw_gpredict_profile_enable(1U, true));
	zassert_ok(kfsw_gpredict_profile_select(1U));
	zassert_equal(kfsw_gpredict_profile_selected(), 1);
}

ZTEST(modules_gpredict, test_profile_disable_selected_is_ebusy)
{
	const struct kfsw_gpredict_profile lur = make_profile("LUR-1");
	const struct kfsw_gpredict_profile other = make_profile("OTHER");
	struct kfsw_gpredict_profile read;

	define_and_select(0U, &lur);
	zassert_ok(kfsw_gpredict_profile_define(1U, &other));

	zassert_equal(kfsw_gpredict_profile_enable(0U, false), -EBUSY);
	zassert_ok(kfsw_gpredict_profile_get(0U, &read));
	zassert_true(read.enabled);
	zassert_equal(kfsw_gpredict_profile_selected(), 0);

	/* One that is not selected can be disabled, and twice is not an error. */
	zassert_ok(kfsw_gpredict_profile_enable(1U, false));
	zassert_ok(kfsw_gpredict_profile_enable(1U, false));
	zassert_ok(kfsw_gpredict_profile_get(1U, &read));
	zassert_false(read.enabled);
	zassert_equal(kfsw_gpredict_profile_enable(2U, false), -ENOENT);
}

ZTEST(modules_gpredict, test_profile_redefine_selected_disabled_clears)
{
	const struct kfsw_gpredict_profile lur = make_profile("LUR-1");
	struct kfsw_gpredict_profile changed = make_profile("LUR-1B");

	/* Redefined and still enabled: the selection stands with the new rules. */
	define_and_select(0U, &lur);
	zassert_ok(kfsw_gpredict_profile_define(0U, &changed));
	zassert_equal(kfsw_gpredict_profile_selected(), 0);

	/* Redefined disabled: the selection goes with it. */
	changed.enabled = false;
	zassert_ok(kfsw_gpredict_profile_define(0U, &changed));
	zassert_equal(kfsw_gpredict_profile_selected(), -ENOENT);
	zassert_equal(kfsw_gpredict_profile_bearing(&mid_bearing), -ENOENT);
}

ZTEST(modules_gpredict, test_profile_narrows_travel)
{
	struct kfsw_gpredict_profile narrow = make_profile("NARROW");
	const struct kfsw_gpredict_bearing refused[] = {
		{.azimuth_mdeg = 99999, .elevation_mdeg = 40000},
		{.azimuth_mdeg = 200001, .elevation_mdeg = 40000},
		{.azimuth_mdeg = 150000, .elevation_mdeg = 19999},
		{.azimuth_mdeg = 150000, .elevation_mdeg = 60001},
	};
	const struct kfsw_gpredict_bearing edge = {.azimuth_mdeg = 100000, .elevation_mdeg = 20000};
	const struct kfsw_gpredict_bearing inside = {.azimuth_mdeg = 150000,
						     .elevation_mdeg = 30000};
	struct kfsw_gpredict_status status;

	narrow.azimuth_min_mdeg = 100000;
	narrow.azimuth_max_mdeg = 200000;
	narrow.elevation_min_mdeg = 20000;
	narrow.elevation_max_mdeg = 60000;
	define_and_select(0U, &narrow);
	enter(KFSW_GPREDICT_TRACKING);

	for (size_t index = 0U; index < ARRAY_SIZE(refused); index++) {
		/* The rotator itself would have followed this one. */
		zassert_true((refused[index].azimuth_mdeg >= GP_AZ_MIN_MDEG) &&
			     (refused[index].azimuth_mdeg <= GP_AZ_MAX_MDEG) &&
			     (refused[index].elevation_mdeg >= GP_EL_MIN_MDEG) &&
			     (refused[index].elevation_mdeg <= GP_EL_MAX_MDEG));
		zassert_equal(kfsw_gpredict_profile_bearing(&refused[index]), -EINVAL,
			      "az %d el %d passed the profile", refused[index].azimuth_mdeg,
			      refused[index].elevation_mdeg);
	}

	status = read_status();
	zassert_equal(status.state, KFSW_GPREDICT_TRACKING);
	zassert_equal(status.refusals, ARRAY_SIZE(refused));
	zassert_equal(status.last_error, -EINVAL);
	zassert_equal(status.bearings, 1U);
	zassert_equal(status.azimuth_mdeg, mid_bearing.azimuth_mdeg);

	zassert_ok(kfsw_gpredict_profile_bearing(&edge));
	zassert_ok(kfsw_gpredict_profile_bearing(&inside));
	status = read_status();
	zassert_equal(status.state, KFSW_GPREDICT_TRACKING);
	zassert_equal(status.bearings, 3U);
	zassert_equal(status.azimuth_mdeg, inside.azimuth_mdeg);
	zassert_equal(status.elevation_mdeg, inside.elevation_mdeg);
}

ZTEST(modules_gpredict, test_profile_refused_for_widening)
{
	const struct kfsw_gpredict_profile base = make_profile("WIDE");
	struct kfsw_gpredict_profile wide[4];
	struct kfsw_gpredict_profile read;

	for (size_t index = 0U; index < ARRAY_SIZE(wide); index++) {
		wide[index] = base;
	}
	wide[0].azimuth_min_mdeg = GP_AZ_MIN_MDEG - 1;
	wide[1].azimuth_max_mdeg = GP_AZ_MAX_MDEG + 1;
	wide[2].elevation_min_mdeg = GP_EL_MIN_MDEG - 1;
	wide[3].elevation_max_mdeg = GP_EL_MAX_MDEG + 1;

	for (size_t index = 0U; index < ARRAY_SIZE(wide); index++) {
		zassert_equal(kfsw_gpredict_profile_define(0U, &wide[index]), -EINVAL,
			      "bound %u widened the rotator's travel", (unsigned int)index);
		zassert_equal(kfsw_gpredict_profile_get(0U, &read), -ENOENT);
	}

	/* Exactly the rotator's travel is not wider than it. */
	zassert_ok(kfsw_gpredict_profile_define(0U, &base));
}

ZTEST(modules_gpredict, test_profile_frequency_policies)
{
	struct kfsw_gpredict_profile follow = make_profile("FOLLOW");
	struct kfsw_gpredict_profile hold = make_profile("HOLD");
	struct kfsw_gpredict_profile ignore = make_profile("IGNORE");
	uint64_t send = GP_UNTOUCHED_HZ;

	hold.policy = KFSW_GPREDICT_FREQUENCY_HOLD;
	ignore.policy = KFSW_GPREDICT_FREQUENCY_IGNORE;
	zassert_ok(kfsw_gpredict_profile_define(0U, &follow));
	zassert_ok(kfsw_gpredict_profile_define(1U, &hold));
	zassert_ok(kfsw_gpredict_profile_define(2U, &ignore));

	zassert_ok(kfsw_gpredict_profile_select(0U));
	zassert_ok(kfsw_gpredict_profile_frequency(GP_DOWNLINK_HZ, &send));
	zassert_equal(send, GP_DOWNLINK_HZ, "FOLLOW sent %llu", (unsigned long long)send);

	zassert_ok(kfsw_gpredict_profile_select(1U));
	send = GP_UNTOUCHED_HZ;
	zassert_ok(kfsw_gpredict_profile_frequency(GP_DOWNLINK_HZ, &send));
	zassert_equal(send, GP_HOLD_HZ, "HOLD sent %llu", (unsigned long long)send);

	zassert_ok(kfsw_gpredict_profile_select(2U));
	send = GP_UNTOUCHED_HZ;
	zassert_ok(kfsw_gpredict_profile_frequency(GP_DOWNLINK_HZ, &send));
	zassert_equal(send, 0U, "IGNORE sent %llu", (unsigned long long)send);

	send = GP_UNTOUCHED_HZ;
	zassert_equal(kfsw_gpredict_profile_frequency(0U, &send), -EINVAL);
	zassert_equal(send, 0U);
	zassert_equal(kfsw_gpredict_profile_frequency(GP_DOWNLINK_HZ, NULL), -EINVAL);

	/* Deciding is not tuning: the tracker's frequency is untouched. */
	zassert_equal(read_status().frequency_hz, 0U);
}

ZTEST(modules_gpredict, test_profile_calls_without_selection)
{
	const struct kfsw_gpredict_profile lur = make_profile("LUR-1");
	uint64_t send = GP_UNTOUCHED_HZ;
	struct kfsw_gpredict_status status;

	/* Defined but not selected, so slot 0 is not a default. */
	zassert_ok(kfsw_gpredict_profile_define(0U, &lur));

	zassert_equal(kfsw_gpredict_profile_bearing(&mid_bearing), -ENOENT);
	zassert_equal(kfsw_gpredict_profile_frequency(GP_DOWNLINK_HZ, &send), -ENOENT);
	zassert_equal(send, 0U, "no selection still sent %llu", (unsigned long long)send);

	status = read_status();
	zassert_equal(status.state, KFSW_GPREDICT_PARKED);
	zassert_equal(status.bearings, 0U);
	/* Nothing was judged, so nothing was refused. */
	zassert_equal(status.refusals, 0U);
	zassert_equal(kfsw_gpredict_profile_bearing(NULL), -EINVAL);
}

ZTEST(modules_gpredict, test_profile_bearing_in_fault_is_eperm)
{
	const struct kfsw_gpredict_profile lur = make_profile("LUR-1");
	struct kfsw_gpredict_status status;

	define_and_select(0U, &lur);
	zassert_ok(kfsw_gpredict_profile_bearing(&mid_bearing));
	zassert_equal(current_state(), KFSW_GPREDICT_TRACKING);

	enter(KFSW_GPREDICT_FAULT);
	zassert_equal(kfsw_gpredict_profile_bearing(&mid_bearing), -EPERM);
	status = read_status();
	zassert_equal(status.state, KFSW_GPREDICT_FAULT);
	zassert_equal(status.refusals, 1U);
	zassert_equal(status.last_error, -EPERM);
}

static struct kfsw_command_result run_command(uint16_t id, const uint32_t *values, size_t count)
{
	const struct kfsw_command_source source = {.node = 0U};
	struct kfsw_command_arg args[2];
	struct kfsw_command_result result = {0};

	zassert_true(count <= ARRAY_SIZE(args));
	for (size_t index = 0U; index < count; index++) {
		args[index].type = KFSW_COMMAND_TYPE_U32;
		args[index].value.u32 = values[index];
	}
	(void)kfsw_command_invoke_id(id, args, count, &source, &result);
	return result;
}

ZTEST(modules_gpredict, test_gpredict_commands_are_registered)
{
	static const struct {
		const char *name;
		uint16_t id;
		uint8_t arg_count;
	} expected[] = {
		{"gpredict_select", 40U, 1U},
		{"gpredict_enable", 41U, 2U},
		{"gpredict_park", 42U, 0U},
		{"gpredict_clear", 43U, 0U},
	};

	zassert_equal(kfsw_gpredict_command_definitions.count, ARRAY_SIZE(expected));
	for (size_t index = 0U; index < ARRAY_SIZE(expected); index++) {
		struct kfsw_command_info info;

		zassert_ok(kfsw_command_find(expected[index].name, &info), "%s is missing",
			   expected[index].name);
		zassert_equal(info.id, expected[index].id, "%s moved", expected[index].name);
		zassert_equal(info.arg_count, expected[index].arg_count);
		zassert_true((info.flags & KFSW_COMMAND_FLAG_MUTATING) != 0U,
			     "%s is not marked mutating", expected[index].name);
	}
}

ZTEST(modules_gpredict, test_gpredict_commands_select_and_enable)
{
	const struct kfsw_gpredict_profile lur = make_profile("LUR-1");
	struct kfsw_gpredict_profile off = make_profile("OFF");
	struct kfsw_command_result result;
	uint32_t args[2];

	off.enabled = false;
	zassert_ok(kfsw_gpredict_profile_define(0U, &lur));
	zassert_ok(kfsw_gpredict_profile_define(1U, &off));

	args[0] = 0U;
	result = run_command(KFSW_COMMAND_ID_GPREDICT_SELECT, args, 1U);
	zassert_equal(result.status, KFSW_COMMAND_OK);
	zassert_str_equal(result.detail, "selected=0 name=LUR-1 catalogue=43017");
	zassert_equal(kfsw_gpredict_profile_selected(), 0);

	args[0] = 1U;
	result = run_command(KFSW_COMMAND_ID_GPREDICT_SELECT, args, 1U);
	zassert_equal(result.status, KFSW_COMMAND_DENIED);
	zassert_str_equal(result.detail, "the profile is disabled; enable it first");

	args[0] = 2U;
	result = run_command(KFSW_COMMAND_ID_GPREDICT_SELECT, args, 1U);
	zassert_equal(result.status, KFSW_COMMAND_INVALID_ARGUMENT);

	/* 256 is not slot 0 with the high bits dropped. */
	zassert_ok(kfsw_gpredict_profile_enable(1U, true));
	zassert_ok(kfsw_gpredict_profile_select(1U));
	args[0] = 256U;
	result = run_command(KFSW_COMMAND_ID_GPREDICT_SELECT, args, 1U);
	zassert_equal(result.status, KFSW_COMMAND_INVALID_ARGUMENT);
	zassert_equal(kfsw_gpredict_profile_selected(), 1);

	args[0] = 1U;
	args[1] = 0U;
	result = run_command(KFSW_COMMAND_ID_GPREDICT_ENABLE, args, 2U);
	zassert_equal(result.status, KFSW_COMMAND_BUSY);
	zassert_str_equal(result.detail, "the profile is selected; select another first");

	args[0] = 0U;
	result = run_command(KFSW_COMMAND_ID_GPREDICT_ENABLE, args, 2U);
	zassert_equal(result.status, KFSW_COMMAND_OK);
	zassert_str_equal(result.detail, "profile=0 enabled=0");

	args[1] = 2U;
	result = run_command(KFSW_COMMAND_ID_GPREDICT_ENABLE, args, 2U);
	zassert_equal(result.status, KFSW_COMMAND_INVALID_ARGUMENT);

	args[0] = 2U;
	args[1] = 1U;
	result = run_command(KFSW_COMMAND_ID_GPREDICT_ENABLE, args, 2U);
	zassert_equal(result.status, KFSW_COMMAND_INVALID_ARGUMENT);
	args[0] = 256U;
	result = run_command(KFSW_COMMAND_ID_GPREDICT_ENABLE, args, 2U);
	zassert_equal(result.status, KFSW_COMMAND_INVALID_ARGUMENT);
}

ZTEST(modules_gpredict, test_gpredict_commands_park_and_clear)
{
	struct kfsw_command_result result;

	enter(KFSW_GPREDICT_TRACKING);
	result = run_command(KFSW_COMMAND_ID_GPREDICT_PARK, NULL, 0U);
	zassert_equal(result.status, KFSW_COMMAND_OK);
	zassert_str_equal(result.detail, "state=parking");

	result = run_command(KFSW_COMMAND_ID_GPREDICT_CLEAR, NULL, 0U);
	zassert_equal(result.status, KFSW_COMMAND_BUSY);
	zassert_str_equal(result.detail, "the tracker is not in fault");

	enter(KFSW_GPREDICT_FAULT);
	result = run_command(KFSW_COMMAND_ID_GPREDICT_PARK, NULL, 0U);
	zassert_equal(result.status, KFSW_COMMAND_DENIED);
	zassert_str_equal(result.detail, "the tracker is in fault; clear it first");
	zassert_equal(current_state(), KFSW_GPREDICT_FAULT);

	result = run_command(KFSW_COMMAND_ID_GPREDICT_CLEAR, NULL, 0U);
	zassert_equal(result.status, KFSW_COMMAND_OK);
	zassert_str_equal(result.detail, "state=parked");
}

ZTEST(modules_gpredict, test_param_profile_rows_sample)
{
	struct kfsw_gpredict_profile hold = make_profile("LUR-1");
	struct kfsw_gpredict_profile off = make_profile("OFF");
	struct kfsw_param_value value;

	/* Nothing selected reads as the reserved values, not as slot 0 or FOLLOW. */
	zassert_ok(kfsw_param_get("gp_profile", &value));
	zassert_equal(value.scalar.u8, UINT8_MAX);
	zassert_ok(kfsw_param_get("gp_policy", &value));
	zassert_equal(value.scalar.u8, UINT8_MAX);
	zassert_ok(kfsw_param_get("gp_catalogue", &value));
	zassert_equal(value.scalar.u32, 0U);
	zassert_ok(kfsw_param_get("gp_profile_name", &value));
	zassert_str_equal(value.text, "");
	zassert_ok(kfsw_param_get("gp_profiles", &value));
	zassert_equal(value.scalar.u8, 0U);

	hold.policy = KFSW_GPREDICT_FREQUENCY_HOLD;
	off.enabled = false;
	zassert_ok(kfsw_gpredict_profile_define(0U, &off));
	define_and_select(2U, &hold);

	zassert_ok(kfsw_param_get("gp_profile", &value));
	zassert_equal(value.scalar.u8, 2U);
	zassert_ok(kfsw_param_get("gp_policy", &value));
	zassert_equal(value.scalar.u8, KFSW_GPREDICT_FREQUENCY_HOLD);
	zassert_ok(kfsw_param_get("gp_catalogue", &value));
	zassert_equal(value.scalar.u32, GP_CATALOGUE);
	zassert_ok(kfsw_param_get("gp_profile_name", &value));
	zassert_str_equal(value.text, "LUR-1");
	zassert_ok(kfsw_param_get("gp_profiles", &value));
	zassert_equal(value.scalar.u8, 2U);
	zassert_ok(kfsw_param_get("gp_profiles_on", &value));
	zassert_equal(value.scalar.u8, 1U);

	zassert_ok(kfsw_gpredict_profile_enable(0U, true));
	zassert_ok(kfsw_param_get("gp_profiles_on", &value));
	zassert_equal(value.scalar.u8, 2U);

	/* Read only: the ground chooses through the command, which checks. */
	value.type = KFSW_PARAM_U8;
	value.size = sizeof(uint8_t);
	value.scalar.u8 = 0U;
	zassert_equal(kfsw_param_set("gp_profile", &value), -EACCES);
	zassert_equal(kfsw_gpredict_profile_selected(), 2);
}

static void *gpredict_setup(void)
{
	const struct kfsw_param_definition_set *const sets[] = {
		&kfsw_gpredict_param_definitions,
	};
	const struct kfsw_command_definition_set *const commands[] = {
		&kfsw_gpredict_command_definitions,
	};

	zassert_ok(kfsw_param_init(sets, ARRAY_SIZE(sets)));
	zassert_ok(kfsw_command_init(commands, ARRAY_SIZE(commands)));
	zassert_ok(kfsw_gpredict_init());
	return NULL;
}

static void gpredict_before(void *fixture)
{
	ARG_UNUSED(fixture);

	clock_offset_ms = 0;
	gpredict_state_reset();
	gpredict_test_profiles_reset();
}

ZTEST_SUITE(modules_gpredict, NULL, gpredict_setup, gpredict_before, NULL, NULL);
