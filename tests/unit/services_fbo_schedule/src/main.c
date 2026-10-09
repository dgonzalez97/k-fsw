/*
 * Copyright (c) 2026 K-FSW
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <string.h>
#include <zephyr/fs/fs.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/ztest.h>

#include <kfsw/platform/storage.h>
#include <kfsw/services/command.h>
#include <kfsw/services/fbo.h>

/* Long enough for several ticks of the queue, so a sweep has certainly run. */
#define SWEEP_MS (4 * CONFIG_KFSW_FBO_SCHEDULE_TICK_MS)

static atomic_t handler_calls;
static int64_t utc_now;
static bool clock_set;
static int64_t mono_offset_ms;

uint64_t __real_kfsw_time_monotonic_ms(void);

uint64_t __wrap_kfsw_time_monotonic_ms(void)
{
	return __real_kfsw_time_monotonic_ms() + (uint64_t)mono_offset_ms;
}

/*
 * The queue reads the clock through this seam, not through
 * kfsw_fbo_clock_seconds: that call is made inside the same file and the
 * linker's --wrap never sees it.
 */
int __wrap_kfsw_fbo_schedule_clock_seconds(int64_t *seconds)
{
	if (!clock_set) {
		return -ENODATA;
	}
	*seconds = utc_now;
	return 0;
}

static int handler(const struct kfsw_command_arg *args, size_t count,
		   const struct kfsw_command_source *source, struct kfsw_command_result *result)
{
	ARG_UNUSED(args);
	ARG_UNUSED(count);
	ARG_UNUSED(source);

	atomic_inc(&handler_calls);
	result->status = KFSW_COMMAND_OK;
	return 0;
}

static const struct kfsw_command_definition definitions[] = {
	{.id = 1, .name = "count", .handler = handler},
};

static const struct kfsw_command_definition_set set = {
	.commands = definitions,
	.count = ARRAY_SIZE(definitions),
};

static void *setup(void)
{
	const struct kfsw_command_definition_set *sets[] = {&set};
	const struct flash_area *area;

	zassert_ok(
		flash_area_open(DT_FIXED_PARTITION_ID(DT_CHOSEN(kfsw_storage_partition)), &area));
	zassert_ok(flash_area_flatten(area, 0, area->fa_size));
	flash_area_close(area);
	zassert_ok(kfsw_storage_init());
	zassert_ok(kfsw_storage_mount());
	zassert_ok(kfsw_command_init(sets, ARRAY_SIZE(sets)));
	zassert_ok(kfsw_fbo_init());
	return NULL;
}

static void before(void *fixture)
{
	ARG_UNUSED(fixture);

	(void)kfsw_fbo_schedule_clear();
	atomic_clear(&handler_calls);
	clock_set = false;
	utc_now = 0;
	mono_offset_ms = 0;
}

static void advance_ms(int64_t milliseconds)
{
	mono_offset_ms += milliseconds;
	k_sleep(K_MSEC(SWEEP_MS));
}

static struct kfsw_fbo_schedule_entry read_entry(uint16_t index)
{
	struct kfsw_fbo_schedule_entry entry;

	zassert_ok(kfsw_fbo_schedule_get_entry(index, &entry), "slot %u is empty",
		   (unsigned int)index);
	return entry;
}

static struct kfsw_fbo_schedule_status read_status(void)
{
	struct kfsw_fbo_schedule_status status;

	zassert_ok(kfsw_fbo_schedule_get_status(&status), "the queue state could not be read");
	return status;
}

ZTEST(fbo_schedule, test_relative_entry_runs_once_its_deadline_passes)
{
	uint16_t index = UINT16_MAX;

	zassert_ok(kfsw_fbo_schedule_add(KFSW_FBO_TIME_RELATIVE, 10, 0U, "count", &index),
		   "a relative entry was refused");
	k_sleep(K_MSEC(SWEEP_MS));
	zassert_equal(atomic_get(&handler_calls), 0, "the entry ran before its deadline");
	zassert_equal(read_entry(index).status, KFSW_FBO_ENTRY_SCHEDULED,
		      "the entry left the queue early");

	advance_ms(10 * MSEC_PER_SEC);
	zassert_equal(atomic_get(&handler_calls), 1, "the entry did not run after its deadline");
	zassert_equal(read_entry(index).status, KFSW_FBO_ENTRY_COMPLETED,
		      "the entry did not report completion");
}

ZTEST(fbo_schedule, test_absolute_entry_waits_for_a_clock)
{
	uint16_t index = UINT16_MAX;

	zassert_equal(
		kfsw_fbo_schedule_add(KFSW_FBO_TIME_ABSOLUTE, 1800000000, 0U, "count", &index),
		-ENODATA, "an absolute entry was accepted with no clock set");
	zassert_false(read_status().clock_set, "the queue claims a clock it does not have");

	clock_set = true;
	utc_now = 1800000000;
	zassert_ok(kfsw_fbo_schedule_add(KFSW_FBO_TIME_ABSOLUTE, utc_now + 10, 0U, "count", &index),
		   "an absolute entry was refused with the clock set");
	zassert_true(read_status().clock_set, "the queue does not see the clock");

	utc_now += 10;
	k_sleep(K_MSEC(SWEEP_MS));
	zassert_equal(atomic_get(&handler_calls), 1, "the absolute entry did not run when due");
}

ZTEST(fbo_schedule, test_a_clock_step_leaves_relative_entries_alone)
{
	uint16_t index = UINT16_MAX;

	clock_set = true;
	utc_now = 1800000000;
	k_sleep(K_MSEC(SWEEP_MS));
	zassert_ok(kfsw_fbo_schedule_add(KFSW_FBO_TIME_RELATIVE, 3600, 0U, "count", &index),
		   "a relative entry was refused");

	utc_now += 86400;
	k_sleep(K_MSEC(SWEEP_MS));
	zassert_equal(atomic_get(&handler_calls), 0, "a clock step released a relative entry");
	zassert_equal(read_entry(index).status, KFSW_FBO_ENTRY_SCHEDULED,
		      "a clock step changed a relative entry");
	zassert_true(read_status().clock_steps > 0U, "the step was not noticed");
}

ZTEST(fbo_schedule, test_an_entry_later_than_the_buffer_reports_overdue)
{
	uint16_t index = UINT16_MAX;
	struct kfsw_fbo_schedule_entry entry;

	zassert_ok(kfsw_fbo_schedule_add(KFSW_FBO_TIME_RELATIVE, 1, 0U, "count", &index),
		   "a relative entry was refused");
	advance_ms((int64_t)(kfsw_fbo_schedule_get_latency_s() + 10U) * MSEC_PER_SEC);

	entry = read_entry(index);
	zassert_equal(entry.status, KFSW_FBO_ENTRY_OVERDUE, "a late entry did not report overdue");
	zassert_equal(entry.result, -ETIME, "an overdue entry did not carry -ETIME");
	zassert_equal(atomic_get(&handler_calls), 0, "an overdue entry still ran");
	zassert_equal(read_status().overdues, 1U, "the overdue was not counted");
}

ZTEST(fbo_schedule, test_one_sweep_releases_no_more_than_the_burst)
{
	uint16_t index = UINT16_MAX;

	for (unsigned int entry = 0U; entry < CONFIG_KFSW_FBO_SCHEDULE_ENTRIES; entry++) {
		zassert_ok(kfsw_fbo_schedule_add(KFSW_FBO_TIME_RELATIVE, 1 + entry, 0U, "count",
						 &index),
			   "entry %u was refused", entry);
	}
	/*
	 * Let the queue consume the wake each add gives it, so nothing but the
	 * periodic tick is left to drive a sweep. Then make all three due at
	 * once. The assertion is on what one sweep may do, not on when a sweep
	 * happens, because the tick is not synchronised to this thread.
	 */
	k_sleep(K_MSEC(SWEEP_MS));
	zassert_equal(atomic_get(&handler_calls), 0, "an entry ran before its deadline");
	mono_offset_ms += 5 * MSEC_PER_SEC;
	k_sleep(K_MSEC(CONFIG_KFSW_FBO_SCHEDULE_TICK_MS + (CONFIG_KFSW_FBO_SCHEDULE_TICK_MS / 2)));
	zassert_true(atomic_get(&handler_calls) < CONFIG_KFSW_FBO_SCHEDULE_ENTRIES,
		     "the queue released the whole backlog at once");

	k_sleep(K_MSEC(10 * CONFIG_KFSW_FBO_SCHEDULE_TICK_MS));
	zassert_equal(atomic_get(&handler_calls), CONFIG_KFSW_FBO_SCHEDULE_ENTRIES,
		      "the backlog never drained");
}

ZTEST(fbo_schedule, test_a_full_queue_changes_nothing)
{
	uint16_t index = UINT16_MAX;
	uint32_t refusals;

	for (unsigned int entry = 0U; entry < CONFIG_KFSW_FBO_SCHEDULE_ENTRIES; entry++) {
		zassert_ok(kfsw_fbo_schedule_add(KFSW_FBO_TIME_RELATIVE, 3600 + entry, 0U, "count",
						 &index),
			   "entry %u was refused", entry);
	}
	refusals = read_status().refusals;
	zassert_equal(kfsw_fbo_schedule_add(KFSW_FBO_TIME_RELATIVE, 7200, 0U, "count", &index),
		      -ENOSPC, "the queue accepted more than its capacity");
	zassert_equal(read_status().entries, CONFIG_KFSW_FBO_SCHEDULE_ENTRIES,
		      "a refused entry changed the queue");
	zassert_equal(read_status().refusals, refusals + 1U, "the refusal was not counted");
}

ZTEST(fbo_schedule, test_the_same_content_re_adopts_its_slot)
{
	uint16_t first = UINT16_MAX;
	uint16_t again = UINT16_MAX;

	zassert_ok(kfsw_fbo_schedule_add(KFSW_FBO_TIME_RELATIVE, 3600, 0U, "count", &first),
		   "the first entry was refused");
	zassert_equal(kfsw_fbo_schedule_add(KFSW_FBO_TIME_RELATIVE, 3600, 0U, "count", &again),
		      -EEXIST, "the same content took a second slot");
	zassert_equal(again, first, "the refusal did not name the slot already holding it");
	zassert_equal(read_status().entries, 1U, "a repeated upload grew the queue");
}

ZTEST(fbo_schedule, test_cancel_never_claims_to_have_stopped_a_finished_entry)
{
	uint16_t index = UINT16_MAX;

	zassert_ok(kfsw_fbo_schedule_add(KFSW_FBO_TIME_RELATIVE, 3600, 0U, "count", &index),
		   "a relative entry was refused");
	zassert_ok(kfsw_fbo_schedule_cancel(index), "a scheduled entry could not be cancelled");
	zassert_equal(read_entry(index).status, KFSW_FBO_ENTRY_CANCELLED,
		      "the entry was not marked cancelled");
	zassert_equal(kfsw_fbo_schedule_cancel(index), -EALREADY,
		      "cancelling a finished entry reported success");

	advance_ms(3600 * MSEC_PER_SEC);
	zassert_equal(atomic_get(&handler_calls), 0, "a cancelled entry still ran");
}

ZTEST(fbo_schedule, test_cancel_rejects_a_slot_that_holds_nothing)
{
	zassert_equal(kfsw_fbo_schedule_cancel(CONFIG_KFSW_FBO_SCHEDULE_ENTRIES), -ENOENT,
		      "a slot outside the queue was cancelled");
	zassert_equal(kfsw_fbo_schedule_cancel(0U), -ENOENT, "an empty slot was cancelled");
}

ZTEST(fbo_schedule, test_an_unknown_command_is_refused_while_an_operator_is_listening)
{
	uint16_t index = UINT16_MAX;

	zassert_equal(
		kfsw_fbo_schedule_add(KFSW_FBO_TIME_RELATIVE, 10, 0U, "no_such_command", &index),
		-ENOENT, "an unregistered command was scheduled");
	zassert_equal(read_status().entries, 0U, "a refused entry took a slot");
}

ZTEST(fbo_schedule, test_a_build_without_remote_commanding_refuses_another_node)
{
	uint16_t index = UINT16_MAX;

	zassert_equal(kfsw_fbo_schedule_add(KFSW_FBO_TIME_RELATIVE, 10, 1U, "count", &index),
		      -ENOTSUP, "a remote entry was accepted with no remote commanding");
	zassert_equal(read_status().entries, 0U, "a refused entry took a slot");
}

ZTEST(fbo_schedule, test_clear_empties_the_queue)
{
	uint16_t index = UINT16_MAX;

	zassert_ok(kfsw_fbo_schedule_add(KFSW_FBO_TIME_RELATIVE, 3600, 0U, "count", &index),
		   "a relative entry was refused");
	zassert_equal(kfsw_fbo_schedule_clear(), 1, "clear did not report the slot it dropped");
	zassert_equal(read_status().entries, 0U, "the queue still holds an entry");
}

ZTEST_SUITE(fbo_schedule, NULL, setup, before, NULL, NULL);
