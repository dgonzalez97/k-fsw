#include <errno.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/devicetree.h>
#include <zephyr/fs/fs.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/crc.h>
#include <zephyr/sys/util.h>
#include <zephyr/ztest.h>

#include <kfsw/platform/storage.h>
#include <kfsw/services/hk.h>
#include <kfsw/services/parameter.h>

#define TEST_TABLE 30U
#define STORE_PATH KFSW_HK_STORE_PATH "/report0.bin"
#define DATASET_PATH KFSW_HK_STORE_PATH "/dataset.bin"
#define STORAGE_PARTITION_NODE DT_CHOSEN(kfsw_storage_partition)
#define STORAGE_PARTITION_ID DT_FIXED_PARTITION_ID(STORAGE_PARTITION_NODE)

#define DATASET_HEADER_SIZE 20U
#define DATASET_CRC_OFFSET 16U

/*
 * Reading back what the store wrote: the window it holds, a filter over it, and
 * the file an extract leaves for downlink.
 */

static uint16_t counter_u16 = 0x1234U;
static uint32_t counter_u32 = 0xAABBCCDDU;

static const struct kfsw_param_definition test_definitions[] = {
	{
		.offset = 0x00,
		.type = KFSW_PARAM_U16,
		.flags = KFSW_PARAM_FLAG_READ_ONLY,
		.name = "reader_test_u16",
		.value = &counter_u16,
	},
	{
		.offset = 0x02,
		.type = KFSW_PARAM_U32,
		.flags = KFSW_PARAM_FLAG_READ_ONLY,
		.name = "reader_test_u32",
		.value = &counter_u32,
	},
};

static const struct kfsw_param_definition_set test_set = {
	.table = TEST_TABLE,
	.name = "readertest",
	.definitions = test_definitions,
	.count = ARRAY_SIZE(test_definitions),
};

static void erase_storage_partition(void)
{
	const struct flash_area *area;
	int result;

	zassert_ok(flash_area_open(STORAGE_PARTITION_ID, &area),
		   "the storage partition would not open");
	result = flash_area_flatten(area, 0, area->fa_size);
	flash_area_close(area);
	zassert_ok(result, "the storage partition would not erase");
}

static void define_report(uint8_t report)
{
	const struct kfsw_hk_entry entries[] = {
		{.node = KFSW_HK_NODE_LOCAL, .param_id = KFSW_PARAM_ID(TEST_TABLE, 0x00)},
		{.node = KFSW_HK_NODE_LOCAL, .param_id = KFSW_PARAM_ID(TEST_TABLE, 0x02)},
	};

	zassert_ok(kfsw_hk_define(report, entries, ARRAY_SIZE(entries)), "the report was refused");
}

/* Storing at the floor writes on every collection, so a test controls exactly
 * how many records land.
 */
static void store_records(uint8_t report, uint16_t count)
{
	define_report(report);
	zassert_ok(kfsw_hk_set_store(report, CONFIG_KFSW_HK_STORE_FLOOR_MS),
		   "the store was refused");
	for (uint16_t index = 0U; index < count; index++) {
		counter_u16 = (uint16_t)(0x1000U + index);
		zassert_ok(kfsw_hk_collect(report), "a collection failed");
	}
}

static void read_file(const char *path, uint8_t *data, size_t size)
{
	struct fs_file_t file;

	fs_file_t_init(&file);
	zassert_ok(fs_open(&file, path, FS_O_READ), "the file would not open");
	zassert_equal(fs_read(&file, data, size), (ssize_t)size, "the file is not that long");
	zassert_ok(fs_close(&file));
}

ZTEST(kfsw_hk_reader, test_the_window_says_what_the_file_holds)
{
	struct kfsw_hk_store_info info;

	store_records(0U, 3U);

	zassert_ok(kfsw_hk_store_query(0U, &info));
	zassert_equal(info.capacity, CONFIG_KFSW_HK_STORE_CAPACITY);
	zassert_equal(info.records, 3U);
	zassert_equal(info.oldest, 0U);
	zassert_equal(info.newest, 2U);
	zassert_equal(info.interval_ms, CONFIG_KFSW_HK_STORE_FLOOR_MS);
	zassert_true(info.record_size > 0U);
}

ZTEST(kfsw_hk_reader, test_records_come_back_oldest_first_and_carry_their_values)
{
	struct kfsw_hk_store_filter filter = {.report = 0U};
	struct kfsw_hk_sample sample;
	uint16_t records = 0U;

	store_records(0U, 3U);

	zassert_ok(kfsw_hk_store_count(&filter, &records));
	zassert_equal(records, 3U);

	for (uint16_t index = 0U; index < records; index++) {
		zassert_ok(kfsw_hk_store_read(&filter, index, &sample));
		zassert_equal(sample.sequence, index, "the records are not in sequence order");
		zassert_equal(sample.entry_count, 2U);
		/* The value the counter held when that record was collected. */
		zassert_equal(sys_get_be16(&sample.data[KFSW_HK_HEADER_SIZE]),
			      (uint16_t)(0x1000U + index), "the stored value came back changed");
	}
	zassert_equal(kfsw_hk_store_read(&filter, records, &sample), -ENOENT,
		      "a record past the end was returned");
}

ZTEST(kfsw_hk_reader, test_a_sequence_window_selects_only_what_is_inside_it)
{
	struct kfsw_hk_store_filter filter = {.report = 0U, .from_sequence = 2U, .to_sequence = 4U};
	struct kfsw_hk_sample sample;
	uint16_t records = 0U;

	store_records(0U, 6U);

	zassert_ok(kfsw_hk_store_count(&filter, &records));
	zassert_equal(records, 3U, "the window did not select three records");
	zassert_ok(kfsw_hk_store_read(&filter, 0U, &sample));
	zassert_equal(sample.sequence, 2U);
	zassert_ok(kfsw_hk_store_read(&filter, 2U, &sample));
	zassert_equal(sample.sequence, 4U);
}

ZTEST(kfsw_hk_reader, test_a_window_that_matches_nothing_selects_nothing)
{
	struct kfsw_hk_store_filter filter = {.report = 0U, .from_sequence = 900U};
	struct kfsw_hk_sample sample;
	uint16_t records = 0U;

	store_records(0U, 3U);

	zassert_ok(kfsw_hk_store_count(&filter, &records));
	zassert_equal(records, 0U);
	zassert_equal(kfsw_hk_store_read(&filter, 0U, &sample), -ENOENT);
	zassert_equal(kfsw_hk_store_extract(&filter, DATASET_PATH, NULL), -ENOENT,
		      "an empty selection still wrote a file");
	zassert_false(fs_stat(DATASET_PATH, &(struct fs_dirent){0}) == 0,
		      "a file was left behind for an empty selection");
}

ZTEST(kfsw_hk_reader, test_a_flag_filter_skips_the_records_that_carry_it)
{
	struct kfsw_hk_store_filter filter = {.report = 0U,
					      .without_flags = KFSW_HK_FLAG_CLOCK_UNSET};
	uint16_t records = 0U;

	store_records(0U, 3U);

	/* The suite has no clock, so every record carries the flag and the filter
	 * takes all of them out. */
	zassert_ok(kfsw_hk_store_count(&filter, &records));
	zassert_equal(records, 0U, "a flag filter did not remove the records carrying it");

	filter.without_flags = 0U;
	zassert_ok(kfsw_hk_store_count(&filter, &records));
	zassert_equal(records, 3U);
}

ZTEST(kfsw_hk_reader, test_the_ring_keeps_only_its_last_capacity_of_records)
{
	struct kfsw_hk_store_filter filter = {.report = 0U};
	struct kfsw_hk_store_info info;
	struct kfsw_hk_sample sample;
	uint16_t records = 0U;
	const uint16_t written = CONFIG_KFSW_HK_STORE_CAPACITY + 3U;

	store_records(0U, written);

	zassert_ok(kfsw_hk_store_query(0U, &info));
	zassert_equal(info.records, CONFIG_KFSW_HK_STORE_CAPACITY,
		      "the ring did not fill to its capacity");
	zassert_equal(info.newest, written - 1U);
	zassert_equal(info.oldest, written - CONFIG_KFSW_HK_STORE_CAPACITY);

	/* The overwritten records are gone, and what is left is still in order. */
	zassert_ok(kfsw_hk_store_count(&filter, &records));
	zassert_equal(records, CONFIG_KFSW_HK_STORE_CAPACITY);
	zassert_ok(kfsw_hk_store_read(&filter, 0U, &sample));
	zassert_equal(sample.sequence, info.oldest);
	zassert_ok(kfsw_hk_store_read(&filter, (uint16_t)(records - 1U), &sample));
	zassert_equal(sample.sequence, info.newest);
}

ZTEST(kfsw_hk_reader, test_an_extract_is_a_dataset_the_ground_can_check)
{
	struct kfsw_hk_store_filter filter = {.report = 0U, .from_sequence = 1U, .to_sequence = 3U};
	struct kfsw_hk_store_info info;
	struct fs_dirent entry;
	uint8_t file[DATASET_HEADER_SIZE + (3U * CONFIG_KFSW_HK_SAMPLE_BYTES)];
	uint16_t records = 0U;
	uint32_t expected_crc;
	uint32_t stored_crc;
	size_t size;

	store_records(0U, 5U);
	zassert_ok(kfsw_hk_store_query(0U, &info));

	zassert_ok(kfsw_hk_store_extract(&filter, DATASET_PATH, &records));
	zassert_equal(records, 3U);

	size = DATASET_HEADER_SIZE + ((size_t)info.record_size * 3U);
	zassert_ok(fs_stat(DATASET_PATH, &entry));
	zassert_equal((size_t)entry.size, size, "the file is not a header plus three records");
	read_file(DATASET_PATH, file, size);

	zassert_mem_equal(file, "KHKD", 4, "the file does not say what it is");
	zassert_equal(file[4], 1U, "the format version changed");
	zassert_equal(file[5], 0U, "the header does not name the report");
	zassert_equal(sys_get_be16(&file[6]), info.record_size);
	zassert_equal(sys_get_be32(&file[8]), 3U);
	zassert_equal(sys_get_be16(&file[12]), 1U, "the first sequence is wrong");
	zassert_equal(sys_get_be16(&file[14]), 3U, "the last sequence is wrong");

	/* The checksum covers the whole file with its own four bytes zeroed. */
	stored_crc = sys_get_be32(&file[DATASET_CRC_OFFSET]);
	sys_put_be32(0U, &file[DATASET_CRC_OFFSET]);
	expected_crc = crc32_ieee(file, size);
	zassert_equal(stored_crc, expected_crc, "the checksum does not cover the file");

	/* And the records are the stored ones, unchanged. */
	for (uint16_t index = 0U; index < 3U; index++) {
		struct kfsw_hk_sample sample;
		const uint8_t *record = &file[DATASET_HEADER_SIZE + (index * info.record_size)];

		zassert_ok(kfsw_hk_store_read(&filter, index, &sample));
		zassert_mem_equal(record, sample.data, info.record_size,
				  "an extracted record differs from the stored one");
	}
}

ZTEST(kfsw_hk_reader, test_an_extract_replaces_the_previous_file_and_leaves_no_temporary)
{
	struct kfsw_hk_store_filter filter = {.report = 0U};
	struct fs_dirent entry;
	uint16_t records = 0U;

	store_records(0U, 2U);
	zassert_ok(kfsw_hk_store_extract(&filter, DATASET_PATH, &records));
	zassert_equal(records, 2U);

	filter.to_sequence = 1U;
	zassert_ok(kfsw_hk_store_extract(&filter, DATASET_PATH, &records));
	zassert_equal(records, 2U, "a window of sequences 0 and 1 did not select both");

	zassert_ok(fs_stat(DATASET_PATH, &entry));
	zassert_false(fs_stat(DATASET_PATH ".part", &entry) == 0,
		      "the temporary file was left behind");
}

ZTEST(kfsw_hk_reader, test_a_report_with_no_file_is_told_apart_from_a_bad_argument)
{
	struct kfsw_hk_store_filter filter = {.report = 0U};
	struct kfsw_hk_store_info info;
	uint16_t records = 0U;

	zassert_equal(kfsw_hk_store_query(0U, &info), -ENOENT, "a report with no file was read");
	zassert_equal(kfsw_hk_store_count(&filter, &records), -ENOENT);

	filter.report = CONFIG_KFSW_HK_REPORTS;
	zassert_equal(kfsw_hk_store_count(&filter, &records), -EINVAL);
	zassert_equal(kfsw_hk_store_query(CONFIG_KFSW_HK_REPORTS, &info), -EINVAL);
	zassert_equal(kfsw_hk_store_query(0U, NULL), -EINVAL);
	zassert_equal(kfsw_hk_store_extract(NULL, DATASET_PATH, NULL), -EINVAL);
	zassert_equal(kfsw_hk_store_extract(&(struct kfsw_hk_store_filter){0}, "", NULL), -EINVAL);
}

ZTEST(kfsw_hk_reader, test_a_file_that_is_not_this_reports_store_is_refused)
{
	struct kfsw_hk_store_info info;
	struct fs_file_t file;
	uint8_t header[12] = {'K', 'H', 'K', 'S', 1U, 9U, 0U, 16U, 0U, 0U, 0U, 8U};

	(void)fs_mkdir(KFSW_HK_STORE_PATH);
	fs_file_t_init(&file);
	zassert_ok(fs_open(&file, STORE_PATH, FS_O_CREATE | FS_O_WRITE | FS_O_TRUNC));
	zassert_equal(fs_write(&file, header, sizeof(header)), (ssize_t)sizeof(header));
	zassert_ok(fs_close(&file));

	/* The header names report 9, so it is not report 0's store. */
	zassert_equal(kfsw_hk_store_query(0U, &info), -EBADMSG);
}

static void *reader_setup(void)
{
	static const struct kfsw_param_definition_set *const sets[] = {&test_set};

	erase_storage_partition();
	zassert_ok(kfsw_storage_init(), "storage did not start");
	zassert_ok(kfsw_storage_mount(), "storage did not mount");
	zassert_ok(kfsw_param_init(sets, ARRAY_SIZE(sets)), "the parameter table did not start");
	zassert_ok(kfsw_hk_init(), "housekeeping did not start");
	return NULL;
}

/* Each test starts from an empty ring, so sequence numbers begin at zero. */
static void reader_before(void *fixture)
{
	ARG_UNUSED(fixture);
	for (uint8_t report = 0U; report < CONFIG_KFSW_HK_REPORTS; report++) {
		(void)kfsw_hk_set_store(report, 0U);
		(void)kfsw_hk_clear(report);
	}
	(void)fs_unlink(STORE_PATH);
	(void)fs_unlink(DATASET_PATH);
	(void)fs_unlink(DATASET_PATH ".part");
}

ZTEST_SUITE(kfsw_hk_reader, NULL, reader_setup, reader_before, NULL, NULL);
