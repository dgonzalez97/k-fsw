#include <errno.h>
#include <string.h>
#include <zephyr/fs/fs.h>
#include <zephyr/irq_offload.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/ztest.h>
#include <kfsw/platform/storage.h>
#include <kfsw/services/boot.h>
#include <kfsw/services/journal.h>

static bool full;
static bool sync_error;
static bool unavailable;
static bool short_write;

ssize_t __real_fs_write(struct fs_file_t *file, const void *data, size_t size);
int __real_fs_sync(struct fs_file_t *file);
bool __real_kfsw_storage_is_ready(void);

ssize_t __wrap_fs_write(struct fs_file_t *file, const void *data, size_t size)
{
	zassert_false(k_is_in_isr());
	if (full) {
		return -ENOSPC;
	}
	if (short_write) {
		short_write = false;
		full = true;
		return __real_fs_write(file, data, size / 2);
	}
	return __real_fs_write(file, data, size);
}

int __wrap_fs_sync(struct fs_file_t *file)
{
	return sync_error ? -EIO : __real_fs_sync(file);
}

bool __wrap_kfsw_storage_is_ready(void)
{
	return !unavailable && __real_kfsw_storage_is_ready();
}

static void *setup(void)
{
	const struct flash_area *area;

	zassert_ok(
		flash_area_open(DT_FIXED_PARTITION_ID(DT_CHOSEN(kfsw_storage_partition)), &area));
	zassert_ok(flash_area_flatten(area, 0, area->fa_size));
	flash_area_close(area);
	zassert_ok(kfsw_storage_init());
	zassert_ok(kfsw_storage_mount());
	zassert_ok(kfsw_journal_start());
	return NULL;
}

ZTEST(journal_queue, test_filter_duplicate_boot_and_repeat_start)
{
	struct kfsw_journal_stats before;
	struct kfsw_journal_stats after;
	struct kfsw_journal_record record;

	kfsw_journal_get_stats(&before);
	kfsw_event_emit(KFSW_EVENT_SOURCE_BOOT, KFSW_EVENT_BOOT_READY, KFSW_EVENT_INFO, NULL, 0);
	kfsw_event_emit(KFSW_EVENT_SOURCE_BOOT, KFSW_EVENT_BOOT_READY, KFSW_EVENT_INFO, NULL, 0);
	kfsw_event_emit(KFSW_EVENT_SOURCE_APP, 90, KFSW_EVENT_INFO, NULL, 0);
	kfsw_journal_get_stats(&after);
	zassert_equal(after.queued, before.queued + 1U);
	zassert_ok(kfsw_journal_start());
	zassert_ok(kfsw_journal_flush());
	kfsw_journal_get_stats(&after);
	zassert_equal(after.held, MIN(before.held + 1U, 8U));
	zassert_ok(kfsw_journal_get(0, &record));
	zassert_equal(record.event.source, KFSW_EVENT_SOURCE_BOOT);
	zassert_false(record.utc_valid);
	zassert_equal(record.boot, 1);
}

ZTEST(journal_queue, test_queue_overflow_counts_drops)
{
	struct kfsw_journal_stats before;
	struct kfsw_journal_stats after;

	kfsw_journal_get_stats(&before);
	for (unsigned int i = 0; i < 10; i++) {
		kfsw_event_emit(KFSW_EVENT_SOURCE_APP, i, KFSW_EVENT_WARNING, NULL, 0);
	}
	kfsw_journal_get_stats(&after);
	zassert_equal(after.dropped - before.dropped, 6);
	zassert_equal(after.queued, 4);
	zassert_ok(kfsw_journal_flush());
}

ZTEST(journal_queue, test_full_storage_keeps_pending_for_retry)
{
	struct kfsw_journal_record record;
	struct kfsw_journal_stats stats;

	kfsw_event_emit(KFSW_EVENT_SOURCE_APP, 111, KFSW_EVENT_ERROR, NULL, 0);
	full = true;
	zassert_equal(kfsw_journal_flush(), -ENOSPC);
	kfsw_journal_get_stats(&stats);
	zassert_equal(stats.queued, 1);
	zassert_false(stats.ready);
	full = false;
	zassert_ok(kfsw_journal_flush());
	zassert_ok(kfsw_journal_get(0, &record));
	zassert_equal(record.event.id, 111);
	zassert_equal(record.boot, 1);
}

ZTEST(journal_queue, test_sync_failure_retries_same_sequence)
{
	struct kfsw_journal_record before;
	struct kfsw_journal_record after;

	kfsw_event_emit(KFSW_EVENT_SOURCE_APP, 112, KFSW_EVENT_ERROR, NULL, 0);
	zassert_ok(kfsw_journal_flush());
	zassert_ok(kfsw_journal_get(0, &before));
	kfsw_event_emit(KFSW_EVENT_SOURCE_APP, 113, KFSW_EVENT_ERROR, NULL, 0);
	sync_error = true;
	zassert_equal(kfsw_journal_flush(), -EIO);
	sync_error = false;
	zassert_ok(kfsw_journal_flush());
	zassert_ok(kfsw_journal_get(0, &after));
	zassert_equal(after.sequence, before.sequence + 1);
	zassert_equal(after.event.id, 113);
}

ZTEST(journal_queue, test_short_write_recovers_pending_record)
{
	struct kfsw_journal_record record;

	kfsw_event_emit(KFSW_EVENT_SOURCE_APP, 114, KFSW_EVENT_ERROR, NULL, 0);
	short_write = true;
	zassert_equal(kfsw_journal_flush(), -ENOSPC);
	full = false;
	zassert_ok(kfsw_journal_flush());
	zassert_ok(kfsw_journal_get(0, &record));
	zassert_equal(record.event.id, 114);
}

ZTEST(journal_queue, test_unavailable_storage_and_service_sources)
{
	struct kfsw_journal_record record;

	unavailable = true;
	kfsw_event_emit(KFSW_EVENT_SOURCE_GNDWDT, 115, KFSW_EVENT_CRITICAL, NULL, 0);
	zassert_equal(kfsw_journal_flush(), -ENODEV);
	unavailable = false;
	zassert_ok(kfsw_journal_flush());
	zassert_ok(kfsw_journal_get(0, &record));
	zassert_equal(record.event.source, KFSW_EVENT_SOURCE_GNDWDT);
	zassert_equal(record.event.id, 115);
}

static void emit_from_isr(const void *unused)
{
	ARG_UNUSED(unused);
	kfsw_event_emit(KFSW_EVENT_SOURCE_APP, 116, KFSW_EVENT_CRITICAL, NULL, 0);
}

ZTEST(journal_queue, test_isr_only_queues_and_worker_writes)
{
	struct kfsw_journal_record record;

	irq_offload(emit_from_isr, NULL);
	zassert_ok(kfsw_journal_flush());
	zassert_ok(kfsw_journal_get(0, &record));
	zassert_equal(record.event.id, 116);
}

ZTEST_SUITE(journal_queue, NULL, setup, NULL, NULL, NULL);
