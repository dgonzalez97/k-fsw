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
#define REPORTS_PATH KFSW_STORAGE_MOUNT_POINT "/hk/reports.dat"
#define STORAGE_PARTITION_NODE DT_CHOSEN(kfsw_storage_partition)
#define STORAGE_PARTITION_ID DT_FIXED_PARTITION_ID(STORAGE_PARTITION_NODE)

/*
 * What survives a reset, and what must not.
 *
 * A definition is the one piece of housekeeping state an operator builds by
 * hand over a link, so losing it to a reset costs a pass. That makes the file
 * worth keeping — and makes every way of reading it back wrong worth refusing
 * loudly, because a definition decoded from a damaged file does not fail: it
 * collects the wrong parameters and reports them as if they were the right
 * ones. Ground would have no way to tell.
 *
 * Save and load are internal because nothing outside the service should choose
 * when they happen. Declaring them here is the exception a test earns.
 */
int kfsw_hk_persist_save(void);
int kfsw_hk_persist_load(void);

static uint16_t counter_u16 = 0x1234U;
static uint32_t counter_u32 = 0xAABBCCDDU;

static const struct kfsw_param_definition test_definitions[] = {
	{
		.offset = 0x00,
		.type = KFSW_PARAM_U16,
		.flags = KFSW_PARAM_FLAG_READ_ONLY,
		.name = "persist_test_u16",
		.value = &counter_u16,
	},
	{
		.offset = 0x02,
		.type = KFSW_PARAM_U32,
		.flags = KFSW_PARAM_FLAG_READ_ONLY,
		.name = "persist_test_u32",
		.value = &counter_u32,
	},
};

static const struct kfsw_param_definition_set test_set = {
	.table = TEST_TABLE,
	.name = "persisttest",
	.definitions = test_definitions,
	.count = ARRAY_SIZE(test_definitions),
};

/* The partition holds whatever the last run left in it, and a first mount of
 * dirty media is not the case under test here.
 */
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

static void *persist_setup(void)
{
	static const struct kfsw_param_definition_set *const sets[] = {&test_set};

	erase_storage_partition();
	zassert_ok(kfsw_storage_init(), "storage did not start");
	zassert_ok(kfsw_storage_mount(), "storage did not mount");
	zassert_ok(kfsw_param_init(sets, ARRAY_SIZE(sets)), "the parameter table did not start");
	zassert_ok(kfsw_hk_init(), "housekeeping did not start");
	return NULL;
}

static void persist_before(void *fixture)
{
	ARG_UNUSED(fixture);
	for (uint8_t report = 0U; report < CONFIG_KFSW_HK_REPORTS; report++) {
		(void)kfsw_hk_clear(report);
	}
	(void)fs_unlink(REPORTS_PATH);
}

/* Defines report 0 over both test parameters. */
static void define_report(uint8_t report)
{
	const struct kfsw_hk_entry entries[] = {
		{.node = KFSW_HK_NODE_LOCAL, .param_id = KFSW_PARAM_ID(TEST_TABLE, 0x00)},
		{.node = KFSW_HK_NODE_LOCAL, .param_id = KFSW_PARAM_ID(TEST_TABLE, 0x02)},
	};

	zassert_ok(kfsw_hk_define(report, entries, ARRAY_SIZE(entries)), "the report was refused");
}

static void forget_everything(void)
{
	for (uint8_t report = 0U; report < CONFIG_KFSW_HK_REPORTS; report++) {
		(void)kfsw_hk_clear(report);
	}
}

static size_t read_file(uint8_t *buffer, size_t size);
static void write_file(const uint8_t *buffer, size_t size);

/*
 * What a reset actually looks like from the service's side.
 *
 * Defining a report writes the file, and so does clearing one — the snapshot
 * tracks the live set rather than waiting to be asked. So a test cannot make
 * the service forget by clearing, because that persists the forgetting too.
 * It has to put the bytes back afterwards, which is what a node that lost
 * power mid-pass would come back to.
 */
static void restart_with(const uint8_t *saved, size_t size)
{
	forget_everything();
	write_file(saved, size);
}

/* Reads the saved file into a buffer, and says how long it was. */
static size_t read_file(uint8_t *buffer, size_t size)
{
	struct fs_file_t file;
	ssize_t read;

	fs_file_t_init(&file);
	zassert_ok(fs_open(&file, REPORTS_PATH, FS_O_READ), "the saved file would not open");
	read = fs_read(&file, buffer, size);
	(void)fs_close(&file);
	zassert_true(read > 0, "the saved file was empty");
	return (size_t)read;
}

static void write_file(const uint8_t *buffer, size_t size)
{
	struct fs_file_t file;

	fs_file_t_init(&file);
	zassert_ok(fs_open(&file, REPORTS_PATH, FS_O_CREATE | FS_O_WRITE | FS_O_TRUNC),
		   "the file would not open for writing");
	zassert_equal(fs_write(&file, buffer, size), (ssize_t)size, "the file was not written");
	zassert_ok(fs_close(&file), "the file would not close");
}

ZTEST_SUITE(kfsw_hk_persistence, NULL, persist_setup, persist_before, NULL, NULL);

/* The whole point: a report built over a link comes back after a reset. */
ZTEST(kfsw_hk_persistence, test_a_definition_comes_back)
{
	struct kfsw_hk_entry read_back[CONFIG_KFSW_HK_ENTRIES];
	size_t count = ARRAY_SIZE(read_back);

	uint8_t saved[256];
	size_t size;

	define_report(0U);
	zassert_ok(kfsw_hk_persist_save(), "the definitions were not saved");
	size = read_file(saved, sizeof(saved));

	restart_with(saved, size);
	count = ARRAY_SIZE(read_back);
	zassert_not_equal(kfsw_hk_get_definition(0U, read_back, &count), 0,
			  "the report was still defined before the reload");

	zassert_ok(kfsw_hk_persist_load(), "the definitions were not loaded");

	count = ARRAY_SIZE(read_back);
	zassert_ok(kfsw_hk_get_definition(0U, read_back, &count), "the report did not come back");
	zassert_equal(count, 2U, "the report came back a different shape");
	zassert_equal(read_back[0].param_id, KFSW_PARAM_ID(TEST_TABLE, 0x00),
		      "the first value changed");
	zassert_equal(read_back[1].param_id, KFSW_PARAM_ID(TEST_TABLE, 0x02),
		      "the second value changed");
}

/* A definition without its period would come back and never collect. */
ZTEST(kfsw_hk_persistence, test_the_collection_period_comes_back)
{
	uint32_t period = 0U;

	uint8_t saved[256];
	size_t size;

	define_report(0U);
	zassert_ok(kfsw_hk_set_period(0U, 60000U), "the period was refused");
	zassert_ok(kfsw_hk_persist_save(), "the definitions were not saved");
	size = read_file(saved, sizeof(saved));

	restart_with(saved, size);
	zassert_ok(kfsw_hk_persist_load(), "the definitions were not loaded");

	zassert_ok(kfsw_hk_get_period(0U, &period), "the period could not be read");
	zassert_equal(period, 60000U, "the report came back on a different schedule");
}

ZTEST(kfsw_hk_persistence, test_several_reports_come_back)
{
	struct kfsw_hk_entry read_back[CONFIG_KFSW_HK_ENTRIES];
	size_t count;

	uint8_t saved[256];
	size_t size;

	define_report(0U);
	define_report(1U);
	zassert_ok(kfsw_hk_persist_save(), "the definitions were not saved");
	size = read_file(saved, sizeof(saved));

	restart_with(saved, size);
	zassert_ok(kfsw_hk_persist_load(), "the definitions were not loaded");

	for (uint8_t report = 0U; report < 2U; report++) {
		count = ARRAY_SIZE(read_back);
		zassert_ok(kfsw_hk_get_definition(report, read_back, &count),
			   "a report did not come back");
		zassert_equal(count, 2U, "a report came back a different shape");
	}
}

/* Nothing saved is not an error worth resetting over, but it must not be read
 * as an empty set of definitions either.
 */
ZTEST(kfsw_hk_persistence, test_no_file_is_not_a_definition)
{
	zassert_not_equal(kfsw_hk_persist_load(), 0, "a missing file loaded successfully");
}

/*
 * The three ways a file can be wrong, each refused rather than decoded.
 *
 * This is the part that matters: every one of these, accepted, produces a
 * report that looks defined and collects something other than what it names.
 */
ZTEST(kfsw_hk_persistence, test_a_file_from_another_format_is_refused)
{
	uint8_t blob[256];
	size_t size;

	define_report(0U);
	zassert_ok(kfsw_hk_persist_save(), "the definitions were not saved");
	size = read_file(blob, sizeof(blob));

	blob[0] = 'X';
	restart_with(blob, size);

	zassert_equal(kfsw_hk_persist_load(), -EBADMSG, "a foreign file was accepted");
}

ZTEST(kfsw_hk_persistence, test_a_newer_version_is_refused_rather_than_guessed_at)
{
	uint8_t blob[256];
	size_t size;

	define_report(0U);
	zassert_ok(kfsw_hk_persist_save(), "the definitions were not saved");
	size = read_file(blob, sizeof(blob));

	blob[4] = 99U;
	restart_with(blob, size);

	zassert_equal(kfsw_hk_persist_load(), -EPROTONOSUPPORT,
		      "a layout this reader does not know was decoded anyway");
}

ZTEST(kfsw_hk_persistence, test_a_damaged_file_is_refused)
{
	uint8_t blob[256];
	size_t size;

	define_report(0U);
	zassert_ok(kfsw_hk_persist_save(), "the definitions were not saved");
	size = read_file(blob, sizeof(blob));

	/* One flipped bit in the entries, past the header the CRC covers. */
	blob[size - 1U] ^= 0xFFU;
	restart_with(blob, size);

	zassert_equal(kfsw_hk_persist_load(), -EBADMSG,
		      "a file that failed its own checksum was decoded");
}

ZTEST(kfsw_hk_persistence, test_a_truncated_file_is_refused)
{
	uint8_t blob[256];
	size_t size;

	define_report(0U);
	zassert_ok(kfsw_hk_persist_save(), "the definitions were not saved");
	size = read_file(blob, sizeof(blob));
	zassert_true(size > 8U, "the saved file was implausibly short");

	restart_with(blob, size / 2U);

	zassert_not_equal(kfsw_hk_persist_load(), 0, "half a file was accepted");
}

/*
 * A definition is re-validated on the way in, not trusted. A parameter named
 * by a definition written before a firmware update may not exist any more, and
 * a report that cannot be collected must not come back looking defined.
 */
ZTEST(kfsw_hk_persistence, test_a_report_naming_what_is_gone_does_not_come_back)
{
	struct kfsw_hk_entry read_back[CONFIG_KFSW_HK_ENTRIES];
	size_t count = ARRAY_SIZE(read_back);
	uint8_t blob[256];
	size_t size;
	uint32_t crc_offset = 8U;
	uint32_t crc;

	define_report(0U);
	zassert_ok(kfsw_hk_persist_save(), "the definitions were not saved");
	size = read_file(blob, sizeof(blob));

	/* Point the first entry at an offset this build does not define, then
	 * re-checksum so the file is honestly formed and only its content is
	 * stale — which is exactly what a firmware update leaves behind.
	 *
	 * 12 bytes of file header, then 6 naming the report and its period,
	 * then entries of node and identifier: the first identifier is at 20.
	 */
	sys_put_be16(KFSW_PARAM_ID(TEST_TABLE, 0x7F), &blob[20]);
	sys_put_be32(0U, &blob[crc_offset]);
	crc = crc32_ieee(blob, size);
	sys_put_be32(crc, &blob[crc_offset]);
	restart_with(blob, size);

	(void)kfsw_hk_persist_load();

	zassert_not_equal(kfsw_hk_get_definition(0U, read_back, &count), 0,
			  "a report naming a parameter that no longer exists came back defined");
}
