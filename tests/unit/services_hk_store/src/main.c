#include <string.h>

#include <zephyr/devicetree.h>
#include <zephyr/fs/fs.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>
#include <zephyr/ztest.h>

#include <kfsw/platform/storage.h>
#include <kfsw/services/hk.h>
#include <kfsw/services/parameter.h>

#define TEST_TABLE 30U
#define STORE_PATH KFSW_STORAGE_MOUNT_POINT "/hk/report0.bin"
#define STORE_HEADER_SIZE 12U
#define STORAGE_PARTITION_NODE DT_CHOSEN(kfsw_storage_partition)
#define STORAGE_PARTITION_ID DT_FIXED_PARTITION_ID(STORAGE_PARTITION_NODE)

/*
 * The ring that lives on the filesystem.
 *
 * Its whole reason to exist is that a pass is short and a ring in RAM is
 * small: a node collects for hours and the ground turns up for ten minutes. So
 * the properties worth pinning are the ones an operator would be hurt by — the
 * file stops growing once every slot has been used, a record lands in the slot
 * its own sequence number names so a repeated flush does not shift the ring,
 * and a report that cannot fit is refused while somebody is still listening
 * rather than when the filesystem fills mid-pass.
 *
 * Sizing is exposed because the refusal is arithmetic worth checking directly.
 */
int kfsw_hk_store_bytes_needed(uint16_t record_size);

static uint16_t counter_u16 = 0x1234U;
static uint32_t counter_u32 = 0xAABBCCDDU;

static const struct kfsw_param_definition test_definitions[] = {
	{
		.offset = 0x00,
		.type = KFSW_PARAM_U16,
		.flags = KFSW_PARAM_FLAG_READ_ONLY,
		.name = "store_test_u16",
		.value = &counter_u16,
	},
	{
		.offset = 0x02,
		.type = KFSW_PARAM_U32,
		.flags = KFSW_PARAM_FLAG_READ_ONLY,
		.name = "store_test_u32",
		.value = &counter_u32,
	},
};

static const struct kfsw_param_definition_set test_set = {
	.table = TEST_TABLE,
	.name = "storetest",
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

static off_t file_size(const char *path)
{
	struct fs_dirent info;

	if (fs_stat(path, &info) != 0) {
		return -1;
	}
	return (off_t)info.size;
}

static void *store_setup(void)
{
	static const struct kfsw_param_definition_set *const sets[] = {&test_set};

	erase_storage_partition();
	zassert_ok(kfsw_storage_init(), "storage did not start");
	zassert_ok(kfsw_storage_mount(), "storage did not mount");
	zassert_ok(kfsw_param_init(sets, ARRAY_SIZE(sets)), "the parameter table did not start");
	zassert_ok(kfsw_hk_init(), "housekeeping did not start");
	return NULL;
}

static void store_before(void *fixture)
{
	ARG_UNUSED(fixture);
	for (uint8_t report = 0U; report < CONFIG_KFSW_HK_REPORTS; report++) {
		(void)kfsw_hk_set_store(report, 0U);
		(void)kfsw_hk_clear(report);
	}
	(void)fs_unlink(STORE_PATH);
}

ZTEST_SUITE(kfsw_hk_store, NULL, store_setup, store_before, NULL, NULL);

/* A store on a report nobody defined has no record size to work from. */
ZTEST(kfsw_hk_store, test_a_store_needs_a_report_first)
{
	zassert_not_equal(kfsw_hk_set_store(0U, CONFIG_KFSW_HK_STORE_FLOOR_MS), 0,
			  "an undefined report was given a store");
}

ZTEST(kfsw_hk_store, test_an_unknown_report_is_refused)
{
	zassert_equal(kfsw_hk_set_store(CONFIG_KFSW_HK_REPORTS, CONFIG_KFSW_HK_STORE_FLOOR_MS),
		      -EINVAL, "a report that does not exist was given a store");
}

/*
 * The floor is about flash, not about bandwidth: every write costs an erase
 * cycle on a part that has a finite number of them.
 */
ZTEST(kfsw_hk_store, test_an_interval_under_the_floor_is_refused)
{
	uint32_t interval = 1U;

	define_report(0U);
	zassert_equal(kfsw_hk_set_store(0U, CONFIG_KFSW_HK_STORE_FLOOR_MS - 1U), -ERANGE,
		      "an interval under the floor was accepted");
	zassert_ok(kfsw_hk_get_store(0U, &interval), "the store could not be read back");
	zassert_equal(interval, 0U, "the refused interval was kept anyway");
}

ZTEST(kfsw_hk_store, test_what_was_set_reads_back)
{
	uint32_t interval = 0U;

	define_report(0U);
	zassert_ok(kfsw_hk_set_store(0U, CONFIG_KFSW_HK_STORE_FLOOR_MS), "the store was refused");
	zassert_ok(kfsw_hk_get_store(0U, &interval), "the store could not be read back");
	zassert_equal(interval, CONFIG_KFSW_HK_STORE_FLOOR_MS, "the interval changed");
}

/* Creating the store creates the file, sized for the whole ring up front. */
ZTEST(kfsw_hk_store, test_the_file_is_made_when_the_store_is_set)
{
	define_report(0U);
	zassert_equal(file_size(STORE_PATH), -1, "the file existed before the store was set");

	zassert_ok(kfsw_hk_set_store(0U, CONFIG_KFSW_HK_STORE_FLOOR_MS), "the store was refused");
	zassert_true(file_size(STORE_PATH) >= (off_t)STORE_HEADER_SIZE,
		     "the store file was not created");
}

/* The header says what wrote it, so a file from another build is not decoded
 * as if it were this one.
 */
ZTEST(kfsw_hk_store, test_the_file_names_its_format)
{
	struct fs_file_t file;
	uint8_t header[STORE_HEADER_SIZE] = {0};

	define_report(0U);
	zassert_ok(kfsw_hk_set_store(0U, CONFIG_KFSW_HK_STORE_FLOOR_MS), "the store was refused");

	fs_file_t_init(&file);
	zassert_ok(fs_open(&file, STORE_PATH, FS_O_READ), "the store file would not open");
	zassert_equal(fs_read(&file, header, sizeof(header)), (ssize_t)sizeof(header),
		      "the store file has no header");
	(void)fs_close(&file);

	zassert_mem_equal(header, "KHKS", 4U, "the store file does not name its format");
	zassert_equal(header[4], 1U, "the store file does not carry a version");
	zassert_equal(header[5], 0U, "the store file names the wrong report");
}

/*
 * The bound that makes this safe to leave running for months: the file reaches
 * capacity and stops. A store that grew would fill the partition during a pass
 * nobody was watching.
 */
ZTEST(kfsw_hk_store, test_the_file_stops_growing_at_capacity)
{
	off_t when_full;

	define_report(0U);
	zassert_ok(kfsw_hk_set_store(0U, CONFIG_KFSW_HK_STORE_FLOOR_MS), "the store was refused");

	for (int i = 0; i < CONFIG_KFSW_HK_STORE_CAPACITY; i++) {
		zassert_ok(kfsw_hk_collect(0U), "the report collected nothing");
	}
	when_full = file_size(STORE_PATH);
	zassert_true(when_full > (off_t)STORE_HEADER_SIZE, "nothing was written");

	/* Three times round the ring. */
	for (int i = 0; i < CONFIG_KFSW_HK_STORE_CAPACITY * 3; i++) {
		zassert_ok(kfsw_hk_collect(0U), "the report collected nothing");
	}

	zassert_equal(file_size(STORE_PATH), when_full,
		      "the store kept growing after every slot had been used");
}

/* And the size it stops at is the size it was refused or accepted against. */
ZTEST(kfsw_hk_store, test_the_file_is_the_size_it_promised)
{
	struct kfsw_hk_sample sample;
	off_t expected;

	define_report(0U);
	zassert_ok(kfsw_hk_set_store(0U, CONFIG_KFSW_HK_STORE_FLOOR_MS), "the store was refused");
	for (int i = 0; i < CONFIG_KFSW_HK_STORE_CAPACITY; i++) {
		zassert_ok(kfsw_hk_collect(0U), "the report collected nothing");
	}
	zassert_ok(kfsw_hk_get(0U, 0U, &sample), "the report held no sample");

	expected = (off_t)kfsw_hk_store_bytes_needed(sample.length);
	zassert_equal(file_size(STORE_PATH), expected,
		      "the file is not the size the refusal arithmetic promised");
}

/* A record lands where its sequence number says, so the newest can be found by
 * reading sequences rather than by trusting a counter that would have to be
 * rewritten on every single write.
 */
ZTEST(kfsw_hk_store, test_a_record_lands_in_the_slot_its_sequence_names)
{
	struct fs_file_t file;
	struct kfsw_hk_sample sample;
	uint8_t record[CONFIG_KFSW_HK_SAMPLE_BYTES] = {0};
	off_t offset;
	uint16_t sequence;

	define_report(0U);
	zassert_ok(kfsw_hk_set_store(0U, CONFIG_KFSW_HK_STORE_FLOOR_MS), "the store was refused");
	zassert_ok(kfsw_hk_collect(0U), "the report collected nothing");
	zassert_ok(kfsw_hk_get(0U, 0U, &sample), "the report held no sample");

	sequence = sample.sequence;
	offset = (off_t)STORE_HEADER_SIZE +
		 ((off_t)(sequence % CONFIG_KFSW_HK_STORE_CAPACITY) * (off_t)sample.length);

	fs_file_t_init(&file);
	zassert_ok(fs_open(&file, STORE_PATH, FS_O_READ), "the store file would not open");
	zassert_ok(fs_seek(&file, offset, FS_SEEK_SET), "the slot could not be reached");
	zassert_equal(fs_read(&file, record, sample.length), (ssize_t)sample.length,
		      "the slot held no record");
	(void)fs_close(&file);

	zassert_mem_equal(record, sample.data, sample.length,
			  "the record in the slot is not the sample whose sequence names it");
}

/*
 * Stopping is not discarding.
 *
 * Turning a store off used to unlink the file in the same call, so the command
 * that reads as "stop writing" also destroyed the pass already captured. An
 * operator who turns storing off to save flash is not necessarily asking to
 * lose what the node collected, so the two are now separate requests.
 */
ZTEST(kfsw_hk_store, test_turning_it_off_keeps_what_was_written)
{
	off_t before;

	define_report(0U);
	zassert_ok(kfsw_hk_set_store(0U, CONFIG_KFSW_HK_STORE_FLOOR_MS), "the store was refused");
	for (int i = 0; i < 3; i++) {
		zassert_ok(kfsw_hk_collect(0U), "the report collected nothing");
	}
	before = file_size(STORE_PATH);
	zassert_true(before > (off_t)STORE_HEADER_SIZE, "nothing was written");

	zassert_ok(kfsw_hk_set_store(0U, 0U), "the store could not be turned off");

	zassert_equal(file_size(STORE_PATH), before,
		      "turning the store off discarded the samples it had captured");

	/* Stopped means stopped: nothing more is written. */
	for (int i = 0; i < 3; i++) {
		zassert_ok(kfsw_hk_collect(0U), "the report collected nothing");
	}
	zassert_equal(file_size(STORE_PATH), before, "a store that was turned off wrote again");
}

/* And discarding is available, as its own request. */
ZTEST(kfsw_hk_store, test_clearing_the_store_removes_the_file)
{
	define_report(0U);
	zassert_ok(kfsw_hk_set_store(0U, CONFIG_KFSW_HK_STORE_FLOOR_MS), "the store was refused");
	zassert_ok(kfsw_hk_collect(0U), "the report collected nothing");
	zassert_true(file_size(STORE_PATH) > (off_t)STORE_HEADER_SIZE, "nothing was written");

	zassert_ok(kfsw_hk_clear_store(0U), "the store could not be cleared");

	zassert_equal(file_size(STORE_PATH), -1, "clearing the store left the file behind");
	zassert_equal(kfsw_hk_clear_store(CONFIG_KFSW_HK_REPORTS), -EINVAL,
		      "a report that does not exist could be cleared");
}

/* Clearing a store that was never on is not an error worth refusing. */
ZTEST(kfsw_hk_store, test_clearing_a_store_that_never_ran_is_harmless)
{
	define_report(0U);
	zassert_ok(kfsw_hk_clear_store(0U), "clearing an unused store failed");
}

/* The sizing the refusal is built on, checked directly rather than inferred. */
ZTEST(kfsw_hk_store, test_the_sizing_is_header_plus_every_slot)
{
	zassert_equal(kfsw_hk_store_bytes_needed(16U),
		      (int)(STORE_HEADER_SIZE + (16U * CONFIG_KFSW_HK_STORE_CAPACITY)),
		      "the sizing arithmetic changed");
}
