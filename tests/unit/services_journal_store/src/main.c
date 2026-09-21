#include <errno.h>
#include <string.h>
#include <zephyr/fs/fs.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/ztest.h>
#include <kfsw/platform/storage.h>
#include "journal_store.h"

static struct kfsw_journal_store store;
static bool fail_header;
ssize_t __real_fs_write(struct fs_file_t *file, const void *data, size_t size);
ssize_t __wrap_fs_write(struct fs_file_t *file, const void *data, size_t size)
{
	return fail_header ? -ENOSPC : __real_fs_write(file, data, size);
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
	return NULL;
}

static void before(void *unused)
{
	ARG_UNUSED(unused);
	(void)fs_unlink(KFSW_JOURNAL_PATH);
	zassert_ok(kfsw_journal_store_open(&store));
}

static struct kfsw_journal_record append(uint16_t id, uint64_t boot)
{
	struct kfsw_journal_record record = {.boot = boot,
					     .utc_valid = true,
					     .utc_seconds = 1900000000,
					     .event = {.source = KFSW_EVENT_SOURCE_APP,
						       .id = id,
						       .severity = KFSW_EVENT_ERROR,
						       .monotonic_us = 123456,
						       .payload_size = 2,
						       .payload = {0xaa, 0xbb}}};

	zassert_ok(kfsw_journal_store_append(&store, &record));
	return record;
}

ZTEST(journal_store, test_restart_retains_boot_payload_and_times)
{
	struct kfsw_journal_store reopened;
	struct kfsw_journal_record record;

	(void)append(123, 1);
	zassert_ok(kfsw_journal_store_open(&reopened));
	zassert_equal(reopened.held, 1);
	zassert_equal(reopened.highest_boot, 1);
	zassert_ok(kfsw_journal_store_get(&reopened, 0, &record));
	zassert_equal(record.event.id, 123);
	zassert_equal(record.event.monotonic_us, 123456);
	zassert_equal(record.utc_seconds, 1900000000);
	zassert_true(record.utc_valid);
	zassert_mem_equal(record.event.payload, "\xaa\xbb", 2);
	store = reopened;
	(void)append(124, reopened.highest_boot + 1);
	zassert_ok(kfsw_journal_store_get(&store, 0, &record));
	zassert_equal(record.boot, 2);
	zassert_equal(record.sequence, 2);
}

ZTEST(journal_store, test_rotation_stays_bounded_and_newest_first)
{
	struct fs_dirent info;
	struct kfsw_journal_record record;

	for (unsigned int i = 0; i < 20; i++) {
		(void)append(i, 1);
	}
	zassert_ok(fs_stat(KFSW_JOURNAL_PATH, &info));
	zassert_equal(info.size, KFSW_JOURNAL_HEADER_SIZE + 8 * KFSW_JOURNAL_RECORD_SIZE);
	zassert_ok(kfsw_journal_store_open(&store));
	zassert_equal(store.held, 8);
	for (unsigned int i = 0; i < 8; i++) {
		zassert_ok(kfsw_journal_store_get(&store, i, &record));
		zassert_equal(record.event.id, 19 - i);
	}
	zassert_equal(kfsw_journal_store_get(&store, 8, &record), -ENOENT);
}

ZTEST(journal_store, test_truncated_tail_keeps_complete_records)
{
	struct fs_file_t file;
	struct kfsw_journal_record record;

	for (unsigned int i = 0; i < 3; i++) {
		(void)append(i, 1);
	}
	fs_file_t_init(&file);
	zassert_ok(fs_open(&file, KFSW_JOURNAL_PATH, FS_O_WRITE));
	zassert_ok(
		fs_truncate(&file, KFSW_JOURNAL_HEADER_SIZE + 2 * KFSW_JOURNAL_RECORD_SIZE + 10));
	zassert_ok(fs_close(&file));
	zassert_ok(kfsw_journal_store_open(&store));
	zassert_equal(store.held, 2);
	zassert_equal(store.corrupt, 1);
	zassert_ok(kfsw_journal_store_get(&store, 0, &record));
	zassert_equal(record.event.id, 1);
	(void)append(4, 2);
	zassert_ok(kfsw_journal_store_open(&store));
	zassert_equal(store.held, 3);
	zassert_equal(store.corrupt, 0);
}

static void corrupt(off_t offset)
{
	struct fs_file_t file;
	uint8_t byte = 0xff;

	fs_file_t_init(&file);
	zassert_ok(fs_open(&file, KFSW_JOURNAL_PATH, FS_O_WRITE));
	zassert_ok(fs_seek(&file, offset, FS_SEEK_SET));
	zassert_equal(fs_write(&file, &byte, 1), 1);
	zassert_ok(fs_close(&file));
}

ZTEST(journal_store, test_bad_crc_skips_only_damaged_slot)
{
	struct kfsw_journal_record record;

	for (unsigned int i = 0; i < 3; i++) {
		(void)append(i, 1);
	}
	corrupt(KFSW_JOURNAL_HEADER_SIZE + KFSW_JOURNAL_RECORD_SIZE + 32);
	zassert_ok(kfsw_journal_store_open(&store));
	zassert_equal(store.held, 2);
	zassert_equal(store.corrupt, 1);
	zassert_ok(kfsw_journal_store_get(&store, 0, &record));
	zassert_equal(record.event.id, 2);
	zassert_ok(kfsw_journal_store_get(&store, 1, &record));
	zassert_equal(record.event.id, 0);
}

ZTEST(journal_store, test_bad_header_is_not_silently_reformatted)
{
	struct fs_dirent before;
	struct fs_dirent after;

	(void)append(1, 1);
	zassert_ok(fs_stat(KFSW_JOURNAL_PATH, &before));
	corrupt(0);
	zassert_equal(kfsw_journal_store_open(&store), -EBADMSG);
	zassert_false(store.ready);
	zassert_ok(fs_stat(KFSW_JOURNAL_PATH, &after));
	zassert_equal(before.size, after.size);
}

ZTEST(journal_store, test_retry_same_slot_does_not_duplicate_record)
{
	struct kfsw_journal_record pending = append(55, 1);

	zassert_ok(kfsw_journal_store_open(&store));
	zassert_ok(kfsw_journal_store_append(&store, &pending));
	zassert_equal(store.held, 1);
	zassert_equal(store.next_sequence, 2);
	for (unsigned int i = 0; i < 8; i++) {
		(void)append(i, 1);
	}
	zassert_equal(kfsw_journal_store_append(&store, &pending), -ESTALE);
}

ZTEST(journal_store, test_initial_full_storage_can_recover_without_bad_header)
{
	struct fs_dirent info;

	zassert_ok(fs_unlink(KFSW_JOURNAL_PATH));
	fail_header = true;
	zassert_equal(kfsw_journal_store_open(&store), -ENOSPC);
	fail_header = false;
	zassert_equal(fs_stat(KFSW_JOURNAL_PATH, &info), -ENOENT);
	zassert_ok(kfsw_journal_store_open(&store));
	zassert_equal(store.held, 0);
}

ZTEST_SUITE(journal_store, NULL, setup, before, NULL, NULL);
