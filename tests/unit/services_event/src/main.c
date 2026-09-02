#include <errno.h>
#include <string.h>

#include <zephyr/sys/util.h>
#include <zephyr/ztest.h>

#include <kfsw/services/event.h>

/* The ring is sized to 8 by this test configuration, so wrap behaviour is
 * reachable without emitting an unreasonable number of records.
 */
#define TEST_RING_DEPTH 8U

static void *event_setup(void)
{
	kfsw_event_clear();
	return NULL;
}

static void emit_numbered(uint16_t id)
{
	const uint8_t payload[2] = {(uint8_t)(id >> 8), (uint8_t)id};

	kfsw_event_emit(KFSW_EVENT_SOURCE_APP, id, KFSW_EVENT_INFO, payload, sizeof(payload));
}

ZTEST(services_event, test_record_and_read_back_by_age)
{
	struct kfsw_event_record record;
	struct kfsw_event_stats stats;

	kfsw_event_clear();
	emit_numbered(10U);
	emit_numbered(11U);
	emit_numbered(12U);

	kfsw_event_get_stats(&stats);
	zassert_equal(stats.held, 3U);
	zassert_equal(stats.capacity, TEST_RING_DEPTH);

	/* Age 0 is the newest. */
	zassert_ok(kfsw_event_get(0U, &record));
	zassert_equal(record.id, 12U);
	zassert_equal(record.source, KFSW_EVENT_SOURCE_APP);
	zassert_equal(record.payload_size, 2U);
	zassert_equal(record.payload[1], 12U);

	zassert_ok(kfsw_event_get(2U, &record));
	zassert_equal(record.id, 10U);

	zassert_equal(kfsw_event_get(3U, &record), -ENOENT);
	zassert_equal(kfsw_event_get(0U, NULL), -EINVAL);
}

ZTEST(services_event, test_sequence_is_monotonic_across_clear)
{
	struct kfsw_event_record first;
	struct kfsw_event_record second;

	kfsw_event_clear();
	emit_numbered(1U);
	zassert_ok(kfsw_event_get(0U, &first));

	/* Clearing discards records but must not restart the sequence, or a
	 * reader could not tell a clear from a loss. */
	kfsw_event_clear();
	emit_numbered(2U);
	zassert_ok(kfsw_event_get(0U, &second));

	zassert_true(second.sequence > first.sequence, "sequence must keep increasing");
}

ZTEST(services_event, test_wrap_overwrites_oldest_and_counts_the_loss)
{
	struct kfsw_event_record record;
	struct kfsw_event_stats before;
	struct kfsw_event_stats after;

	kfsw_event_clear();
	kfsw_event_get_stats(&before);

	for (uint16_t id = 0U; id < (TEST_RING_DEPTH + 3U); id++) {
		emit_numbered(id);
	}

	kfsw_event_get_stats(&after);
	zassert_equal(after.held, TEST_RING_DEPTH, "the ring must stay bounded");
	zassert_equal(after.overwritten - before.overwritten, 3U,
		      "losing history must be counted, not silent");

	/* The newest is kept and the three oldest are gone. */
	zassert_ok(kfsw_event_get(0U, &record));
	zassert_equal(record.id, TEST_RING_DEPTH + 2U);
	zassert_ok(kfsw_event_get(TEST_RING_DEPTH - 1U, &record));
	zassert_equal(record.id, 3U);
}

ZTEST(services_event, test_invalid_input_is_rejected_and_counted)
{
	static const uint8_t oversized[KFSW_EVENT_MAX_PAYLOAD_SIZE + 1U] = {0};
	struct kfsw_event_stats before;
	struct kfsw_event_stats after;

	kfsw_event_clear();
	kfsw_event_get_stats(&before);

	kfsw_event_emit(KFSW_EVENT_SOURCE_APP, 1U, KFSW_EVENT_INFO, oversized, sizeof(oversized));
	kfsw_event_emit((enum kfsw_event_source)999, 1U, KFSW_EVENT_INFO, NULL, 0U);
	kfsw_event_emit(KFSW_EVENT_SOURCE_APP, 1U, KFSW_EVENT_INFO, NULL, 4U);

	kfsw_event_get_stats(&after);
	zassert_equal(after.held, 0U, "no invalid record may be stored");
	zassert_equal(after.rejected - before.rejected, 3U);
	zassert_equal(after.recorded, before.recorded, "a rejection is not a record");
}

ZTEST(services_event, test_empty_payload_is_valid)
{
	struct kfsw_event_record record;

	kfsw_event_clear();
	kfsw_event_emit(KFSW_EVENT_SOURCE_BOOT, 7U, KFSW_EVENT_CRITICAL, NULL, 0U);

	zassert_ok(kfsw_event_get(0U, &record));
	zassert_equal(record.payload_size, 0U);
	zassert_equal(record.severity, KFSW_EVENT_CRITICAL);
	zassert_equal(record.source, KFSW_EVENT_SOURCE_BOOT);
}

static bool count_events(const struct kfsw_event_record *record, void *context)
{
	ARG_UNUSED(record);
	(*(uint32_t *)context)++;
	return true;
}

static bool stop_after_first(const struct kfsw_event_record *record, void *context)
{
	ARG_UNUSED(record);
	(*(uint32_t *)context)++;
	return false;
}

ZTEST(services_event, test_visit_walks_oldest_first_and_honours_early_stop)
{
	struct kfsw_event_record oldest;
	uint32_t seen = 0U;

	kfsw_event_clear();
	emit_numbered(100U);
	emit_numbered(101U);
	emit_numbered(102U);

	kfsw_event_visit(count_events, &seen);
	zassert_equal(seen, 3U);

	seen = 0U;
	kfsw_event_visit(stop_after_first, &seen);
	zassert_equal(seen, 1U, "a visitor returning false must stop the walk");

	/* Oldest first: the last held record is the newest. */
	zassert_ok(kfsw_event_get(2U, &oldest));
	zassert_equal(oldest.id, 100U);

	kfsw_event_visit(NULL, &seen);
}

ZTEST(services_event, test_names_are_reported_for_shell_output)
{
	zassert_equal(strcmp(kfsw_event_source_name(KFSW_EVENT_SOURCE_FTP), "ftp"), 0);
	zassert_equal(strcmp(kfsw_event_source_name((enum kfsw_event_source)200), "unknown"), 0);
	zassert_equal(strcmp(kfsw_event_severity_name(KFSW_EVENT_ERROR), "error"), 0);
	zassert_equal(strcmp(kfsw_event_severity_name((enum kfsw_event_severity)9), "unknown"), 0);
}

ZTEST_SUITE(services_event, NULL, event_setup, NULL, NULL, NULL);
