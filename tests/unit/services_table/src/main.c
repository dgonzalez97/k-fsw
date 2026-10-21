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
#include <kfsw/services/parameter.h>
#include <kfsw/services/table.h>

#define STORAGE_PARTITION_NODE DT_CHOSEN(kfsw_storage_partition)
#define STORAGE_PARTITION_ID DT_FIXED_PARTITION_ID(STORAGE_PARTITION_NODE)
#define TABLE_DIRECTORY KFSW_STORAGE_MOUNT_POINT "/tables"
#define TABLE_PATH TABLE_DIRECTORY "/fixture.tbl"

#define TABLE_HEADER_SIZE 20U
#define TABLE_CRC_OFFSET 16U
#define TABLE_ENTRY_HEADER_SIZE 4U
#define TABLE_MAX_SIZE 256U

/* The wire codes, which the file format fixes and this suite writes by hand. */
#define WIRE_U8 1U
#define WIRE_U32 2U
#define WIRE_I32 3U

/* The fixture table and the offsets in it. */
#define FIXTURE_TABLE 23U
#define OFFSET_LIMIT 0x00U
#define OFFSET_MODE 0x04U
#define OFFSET_SERIAL 0x08U
#define OFFSET_UNUSED 0x20U

#define LIMIT_MAX 1000U

static uint32_t fixture_limit = 10U;
static int32_t fixture_mode = -1;
static uint8_t fixture_serial = 3U;

/* The range check a table file has to pass before any of it is applied. */
static int validate_limit(const union kfsw_param_scalar *value)
{
	return (value->u32 <= LIMIT_MAX) ? 0 : -ERANGE;
}

static const struct kfsw_param_definition fixture_definitions[] = {
	{
		.offset = OFFSET_LIMIT,
		.type = KFSW_PARAM_U32,
		.flags = KFSW_PARAM_FLAG_CONFIGURATION |
			 (IS_ENABLED(KFSW_TEST_FALLIBLE_MIXED) ? KFSW_PARAM_FLAG_PERSISTENT : 0),
		.name = "fixture_limit",
		.description = "Range-checked configuration value",
		.value = &fixture_limit,
		.default_value = {.u32 = 10U},
		.validate = validate_limit,
	},
	{
		.offset = OFFSET_MODE,
		.type = KFSW_PARAM_I32,
		.flags = KFSW_PARAM_FLAG_CONFIGURATION,
		.name = "fixture_mode",
		.description = "Signed configuration value",
		.value = &fixture_mode,
		.default_value = {.i32 = -1},
	},
	{
		.offset = OFFSET_SERIAL,
		.type = KFSW_PARAM_U8,
		.flags = KFSW_PARAM_FLAG_READ_ONLY,
		.name = "fixture_serial",
		.description = "Read-only value a file must not write",
		.value = &fixture_serial,
		.default_value = {.u8 = 3U},
	},
};

static const struct kfsw_param_definition_set fixture_set = {
	.table = FIXTURE_TABLE,
	.name = "fixture",
	.definitions = fixture_definitions,
	.count = ARRAY_SIZE(fixture_definitions),
};

static uint32_t applied_value;
static int apply_calls;
static int apply_error;
static int notifications;
static bool refuse_validation;

static int validate_applied(const union kfsw_param_scalar *value)
{
	return refuse_validation ? -ERANGE : validate_limit(value);
}

static int apply_value(const union kfsw_param_scalar *value)
{
	apply_calls++;
	if (apply_error != 0) {
		return apply_error;
	}
	applied_value = value->u32;
	return 0;
}

static void applied_changed(const union kfsw_param_scalar *value)
{
	ARG_UNUSED(value);
	notifications++;
}

static const struct kfsw_param_definition fallible_definitions[] = {
	{.offset = 0,
	 .type = KFSW_PARAM_U32,
	 .name = "applied_value",
	 .value = &applied_value,
	 .validate = validate_applied,
	 .apply = apply_value,
	 .changed = applied_changed,
	 .flags = (IS_ENABLED(KFSW_TEST_FALLIBLE_PERSIST) || IS_ENABLED(KFSW_TEST_FALLIBLE_MIXED))
			  ? KFSW_PARAM_FLAG_PERSISTENT
			  : 0},
	{.offset = 4, .type = KFSW_PARAM_I32, .name = "applied_mode", .value = &fixture_mode},
};
static const struct kfsw_param_definition_set fallible_set = {
	.table = 22,
	.name = "fallible",
	.definitions = fallible_definitions,
	.count = ARRAY_SIZE(fallible_definitions),
};

static char fixture_text[8];
static uint8_t fixture_data[2];
static int text_changes;
static int data_changes;

static void text_changed(const char *text)
{
	ARG_UNUSED(text);
	text_changes++;
}

static void data_changed(const uint8_t *data, size_t size)
{
	ARG_UNUSED(data);
	ARG_UNUSED(size);
	data_changes++;
}

static const struct kfsw_param_definition text_definitions[] = {
	{.offset = 0,
	 .type = KFSW_PARAM_STRING,
	 .name = "fixture_text",
	 .value = fixture_text,
	 .capacity = sizeof(fixture_text),
	 .flags = IS_ENABLED(KFSW_TEST_TEXT_PERSIST) ? KFSW_PARAM_FLAG_PERSISTENT : 0,
	 .changed_text = text_changed},
	{.offset = 8,
	 .type = KFSW_PARAM_DATA,
	 .name = "fixture_data",
	 .value = fixture_data,
	 .capacity = sizeof(fixture_data),
	 .flags = IS_ENABLED(KFSW_TEST_TEXT_PERSIST) ? KFSW_PARAM_FLAG_PERSISTENT : 0,
	 .changed_data = data_changed},
};
static const struct kfsw_param_definition_set text_set = {
	.table = 21,
	.name = "text",
	.definitions = text_definitions,
	.count = ARRAY_SIZE(text_definitions),
};

static uint8_t file_image[TABLE_MAX_SIZE];
static size_t file_size;

static void erase_storage_partition(void)
{
	const struct flash_area *area;

	zassert_ok(flash_area_open(STORAGE_PARTITION_ID, &area), "partition open failed");
	zassert_ok(flash_area_flatten(area, 0, area->fa_size), "partition erase failed");
	flash_area_close(area);
}

/* Start a file for a table, with no entries yet. */
static void begin_file(uint8_t table)
{
	(void)memset(file_image, 0, sizeof(file_image));
	(void)memcpy(file_image, "KTBL", 4);
	sys_put_be16(1U, &file_image[4]);
	sys_put_be16(TABLE_HEADER_SIZE, &file_image[6]);
	file_image[14] = table;
	file_size = TABLE_HEADER_SIZE;
}

static void add_entry(uint8_t offset, uint8_t type, const uint8_t *value, uint16_t value_size)
{
	zassert_true((file_size + TABLE_ENTRY_HEADER_SIZE + value_size) <= sizeof(file_image));
	file_image[file_size] = offset;
	file_image[file_size + 1U] = type;
	sys_put_be16(value_size, &file_image[file_size + 2U]);
	(void)memcpy(&file_image[file_size + TABLE_ENTRY_HEADER_SIZE], value, value_size);
	file_size += TABLE_ENTRY_HEADER_SIZE + value_size;
}

static void add_u32(uint8_t offset, uint32_t value)
{
	uint8_t encoded[4];

	sys_put_be32(value, encoded);
	add_entry(offset, WIRE_U32, encoded, sizeof(encoded));
}

static void add_i32(uint8_t offset, int32_t value)
{
	uint8_t encoded[4];

	sys_put_be32((uint32_t)value, encoded);
	add_entry(offset, WIRE_I32, encoded, sizeof(encoded));
}

/* Seal the header, then write the file where the service will read it. */
static void write_file(uint16_t entry_count)
{
	struct fs_file_t file;

	sys_put_be32((uint32_t)(file_size - TABLE_HEADER_SIZE), &file_image[8]);
	sys_put_be16(entry_count, &file_image[12]);
	sys_put_be32(0U, &file_image[TABLE_CRC_OFFSET]);
	sys_put_be32(crc32_ieee(file_image, file_size), &file_image[TABLE_CRC_OFFSET]);

	fs_file_t_init(&file);
	zassert_ok(fs_open(&file, TABLE_PATH, FS_O_CREATE | FS_O_WRITE | FS_O_TRUNC));
	zassert_equal(fs_write(&file, file_image, file_size), (ssize_t)file_size);
	zassert_ok(fs_close(&file));
}

static uint32_t read_u32(const char *name)
{
	struct kfsw_param_value value;

	zassert_ok(kfsw_param_get(name, &value));
	zassert_equal(value.type, KFSW_PARAM_U32);
	return value.scalar.u32;
}

static int32_t read_i32(const char *name)
{
	struct kfsw_param_value value;

	zassert_ok(kfsw_param_get(name, &value));
	zassert_equal(value.type, KFSW_PARAM_I32);
	return value.scalar.i32;
}

static void set_u32(const char *name, uint32_t raw_value)
{
	struct kfsw_param_value value = {
		.type = KFSW_PARAM_U32,
		.size = sizeof(uint32_t),
		.scalar.u32 = raw_value,
	};

	zassert_ok(kfsw_param_set(name, &value));
}

static void set_i32(const char *name, int32_t raw_value)
{
	struct kfsw_param_value value = {
		.type = KFSW_PARAM_I32,
		.size = sizeof(int32_t),
		.scalar.i32 = raw_value,
	};

	zassert_ok(kfsw_param_set(name, &value));
}

ZTEST(services_table, test_a_whole_file_is_applied_when_every_entry_passes)
{
	struct kfsw_table_report report;
	struct kfsw_table_status status;
	uint32_t loads_before;

	zassert_ok(kfsw_table_get_status(&status));
	loads_before = status.loads;

	begin_file(FIXTURE_TABLE);
	add_u32(OFFSET_LIMIT, 250U);
	add_i32(OFFSET_MODE, 7);
	write_file(2U);

	zassert_ok(kfsw_table_load(TABLE_PATH, &report));
	zassert_equal(report.table, FIXTURE_TABLE);
	zassert_equal(report.entries, 2U);
	zassert_equal(report.accepted, 2U);
	zassert_equal(report.failed_entry, 0U);
	zassert_equal(read_u32("fixture_limit"), 250U);
	zassert_equal(read_i32("fixture_mode"), 7);

	zassert_ok(kfsw_table_get_status(&status));
	zassert_equal(status.state, KFSW_TABLE_LOADED);
	zassert_equal(status.table, FIXTURE_TABLE);
	zassert_equal(status.entries, 2U);
	zassert_equal(status.loads, loads_before + 1U);
	zassert_str_equal(status.path, TABLE_PATH);
}

ZTEST(services_table, test_a_load_is_undone_once_and_not_twice)
{
	struct kfsw_table_status status;
	uint32_t reverts_before;

	zassert_ok(kfsw_table_get_status(&status));
	reverts_before = status.reverts;

	set_u32("fixture_limit", 11U);

	begin_file(FIXTURE_TABLE);
	add_u32(OFFSET_LIMIT, 900U);
	write_file(1U);

	zassert_ok(kfsw_table_load(TABLE_PATH, NULL));
	zassert_equal(read_u32("fixture_limit"), 900U);

	zassert_ok(kfsw_table_revert());
	zassert_equal(read_u32("fixture_limit"), 11U, "the replaced value did not come back");

	zassert_ok(kfsw_table_get_status(&status));
	zassert_equal(status.state, KFSW_TABLE_REVERTED);
	zassert_equal(status.reverts, reverts_before + 1U);

	/* Nothing is held any more, so a second revert has nothing to put back. */
	zassert_equal(kfsw_table_revert(), -ENOENT);
	zassert_equal(read_u32("fixture_limit"), 11U);
}

ZTEST(services_table, test_one_bad_value_leaves_the_whole_file_unapplied)
{
	struct kfsw_table_report report;
	struct kfsw_table_status status;
	uint32_t rejections_before;

	zassert_ok(kfsw_table_get_status(&status));
	rejections_before = status.rejections;

	set_u32("fixture_limit", 12U);
	set_i32("fixture_mode", -5);

	/* The first entry is fine and the second is outside its range. */
	begin_file(FIXTURE_TABLE);
	add_i32(OFFSET_MODE, 3);
	add_u32(OFFSET_LIMIT, LIMIT_MAX + 1U);
	write_file(2U);

	zassert_equal(kfsw_table_load(TABLE_PATH, &report), -EBADMSG);
	zassert_equal(report.failed_entry, 2U, "the report did not name the entry that failed");
	zassert_equal(report.failed_offset, OFFSET_LIMIT);
	zassert_equal(report.reason, -ERANGE, "the parameter's own range check did not run");
	zassert_equal(report.accepted, 1U);

	zassert_equal(read_u32("fixture_limit"), 12U);
	zassert_equal(read_i32("fixture_mode"), -5,
		      "the entry that passed was applied although the file was refused");

	zassert_ok(kfsw_table_get_status(&status));
	zassert_equal(status.rejections, rejections_before + 1U);
}

ZTEST(services_table, test_validating_a_good_file_changes_nothing)
{
	struct kfsw_table_report report;
	struct kfsw_table_status status;
	uint32_t loads_before;
	uint32_t rejections_before;

	zassert_ok(kfsw_table_get_status(&status));
	loads_before = status.loads;
	rejections_before = status.rejections;

	set_u32("fixture_limit", 13U);

	begin_file(FIXTURE_TABLE);
	add_u32(OFFSET_LIMIT, 500U);
	write_file(1U);

	zassert_ok(kfsw_table_validate(TABLE_PATH, &report));
	zassert_equal(report.accepted, 1U);
	zassert_equal(read_u32("fixture_limit"), 13U, "validating applied the file");

	zassert_ok(kfsw_table_get_status(&status));
	zassert_equal(status.loads, loads_before, "validating counted a load");
	zassert_equal(status.rejections, rejections_before, "validating counted a rejection");
}

ZTEST(services_table, test_a_file_whose_checksum_does_not_match_is_refused)
{
	struct kfsw_table_report report;

	begin_file(FIXTURE_TABLE);
	add_u32(OFFSET_LIMIT, 100U);
	write_file(1U);

	/* One flipped byte in a value, which the checksum covers. */
	file_image[TABLE_HEADER_SIZE + TABLE_ENTRY_HEADER_SIZE] ^= 0xFFU;
	{
		struct fs_file_t file;

		fs_file_t_init(&file);
		zassert_ok(fs_open(&file, TABLE_PATH, FS_O_CREATE | FS_O_WRITE | FS_O_TRUNC));
		zassert_equal(fs_write(&file, file_image, file_size), (ssize_t)file_size);
		zassert_ok(fs_close(&file));
	}

	zassert_equal(kfsw_table_validate(TABLE_PATH, &report), -EBADMSG);
	zassert_equal(report.reason, -EBADMSG);
	zassert_equal(report.accepted, 0U);
}

ZTEST(services_table, test_an_offset_no_parameter_uses_is_refused)
{
	struct kfsw_table_report report;

	begin_file(FIXTURE_TABLE);
	add_u32(OFFSET_UNUSED, 1U);
	write_file(1U);

	zassert_equal(kfsw_table_validate(TABLE_PATH, &report), -EBADMSG);
	zassert_equal(report.failed_offset, OFFSET_UNUSED);
	zassert_equal(report.reason, -ENXIO);
}

ZTEST(services_table, test_a_read_only_parameter_is_refused)
{
	struct kfsw_table_report report;
	const uint8_t value = 9U;

	begin_file(FIXTURE_TABLE);
	add_entry(OFFSET_SERIAL, WIRE_U8, &value, sizeof(value));
	write_file(1U);

	zassert_equal(kfsw_table_validate(TABLE_PATH, &report), -EBADMSG);
	zassert_equal(report.failed_offset, OFFSET_SERIAL);
	zassert_equal(report.reason, -EROFS);
}

ZTEST(services_table, test_the_same_offset_twice_is_refused)
{
	struct kfsw_table_report report;

	begin_file(FIXTURE_TABLE);
	add_u32(OFFSET_LIMIT, 100U);
	add_u32(OFFSET_LIMIT, 200U);
	write_file(2U);

	zassert_equal(kfsw_table_validate(TABLE_PATH, &report), -EBADMSG);
	zassert_equal(report.failed_entry, 2U);
	zassert_equal(report.reason, -EEXIST);
}

ZTEST(services_table, test_a_type_code_that_is_not_the_parameters_own_is_refused)
{
	struct kfsw_table_report report;
	const uint8_t value = 1U;

	/* A u8 where the parameter is a u32: the value would fit in the union and
	 * quietly mean something else. */
	begin_file(FIXTURE_TABLE);
	add_entry(OFFSET_LIMIT, WIRE_U8, &value, sizeof(value));
	write_file(1U);

	zassert_equal(kfsw_table_validate(TABLE_PATH, &report), -EBADMSG);
	zassert_equal(report.reason, -EBADMSG);
}

ZTEST(services_table, test_a_file_for_a_table_that_is_not_registered_is_refused)
{
	struct kfsw_table_report report;

	begin_file(FIXTURE_TABLE + 1U);
	add_u32(OFFSET_LIMIT, 100U);
	write_file(1U);

	zassert_equal(kfsw_table_validate(TABLE_PATH, &report), -EBADMSG);
	zassert_equal(report.reason, -ENXIO);
}

ZTEST(services_table, test_a_missing_file_is_told_apart_from_a_bad_one)
{
	zassert_equal(kfsw_table_validate(TABLE_DIRECTORY "/absent.tbl", NULL), -ENOENT);
	zassert_equal(kfsw_table_validate(NULL, NULL), -EINVAL);
}

ZTEST(services_table, test_a_dumped_table_loads_back_unchanged)
{
	struct kfsw_table_report report;
	uint16_t entries = 0U;

	set_u32("fixture_limit", 77U);

	/* The read-only parameter is left out, so the file only holds what a
	 * load is allowed to write. */
	zassert_ok(kfsw_table_dump(FIXTURE_TABLE, TABLE_DIRECTORY "/dumped.tbl", &entries));
	zassert_equal(entries, 2U);

	set_u32("fixture_limit", 1U);
	zassert_ok(kfsw_table_load(TABLE_DIRECTORY "/dumped.tbl", &report));
	zassert_equal(report.entries, 2U);
	zassert_equal(read_u32("fixture_limit"), 77U);
}

ZTEST(services_table, test_fallible_load_and_retryable_undo)
{
	struct kfsw_table_report report;
	struct kfsw_table_status before, after;

	apply_calls = 0;
	notifications = 0;
	applied_value = 11;
	begin_file(22);
	add_u32(0, 77);
	write_file(1);
	zassert_ok(kfsw_table_validate(TABLE_PATH, &report));
	zassert_equal(apply_calls, 0, "file validation applied owner state");
	zassert_ok(kfsw_table_load(TABLE_PATH, &report));
	zassert_equal(applied_value, 77);
	zassert_equal(apply_calls, 1);
	zassert_equal(notifications, 1, "duplicate load notification");

	zassert_ok(kfsw_table_get_status(&before));
	begin_file(22);
	add_u32(0, 88);
	add_i32(4, 9);
	write_file(2);
	zassert_equal(kfsw_table_load(TABLE_PATH, &report), -EBADMSG);
	zassert_equal(report.reason, -ENOTSUP);
	zassert_equal(applied_value, 77);
	zassert_equal(apply_calls, 1);

	begin_file(22);
	add_u32(0, LIMIT_MAX + 1);
	write_file(1);
	zassert_equal(kfsw_table_load(TABLE_PATH, &report), -EBADMSG);
	zassert_equal(apply_calls, 1);

	begin_file(22);
	add_u32(0, 88);
	write_file(1);
	apply_error = -EIO;
	zassert_equal(kfsw_table_load(TABLE_PATH, &report), -EIO);
	zassert_equal(report.reason, -EIO);
	zassert_equal(applied_value, 77);
	zassert_equal(notifications, 1);
	zassert_ok(kfsw_table_get_status(&after));
	zassert_equal(after.loads, before.loads);
	zassert_equal(after.state, KFSW_TABLE_LOADED);

	refuse_validation = true;
	zassert_equal(kfsw_table_revert(), -ERANGE);
	zassert_equal(apply_calls, 2, "rejected undo applied owner state");
	refuse_validation = false;
	zassert_equal(kfsw_table_revert(), -EIO);
	zassert_equal(applied_value, 77);
	zassert_equal(notifications, 1);
	apply_error = 0;
	zassert_ok(kfsw_table_revert());
	zassert_equal(applied_value, 11, "undo captured owner backing after application");
	zassert_equal(apply_calls, 4);
	zassert_equal(notifications, 2);
}

ZTEST(services_table, test_text_and_data_batch_notifies_once)
{
	const uint8_t text[] = "new";
	const uint8_t data[] = {3, 4};

	strcpy(fixture_text, "old");
	fixture_data[0] = 1;
	fixture_data[1] = 2;
	text_changes = 0;
	data_changes = 0;
	begin_file(21);
	add_entry(0, 7, text, sizeof(text));
	add_entry(8, 8, data, sizeof(data));
	write_file(2);
	zassert_ok(kfsw_table_load(TABLE_PATH, NULL));
	zassert_str_equal(fixture_text, "new");
	zassert_mem_equal(fixture_data, data, sizeof(data));
	zassert_equal(text_changes, 1);
	zassert_equal(data_changes, 1);
	zassert_ok(kfsw_table_revert());
	zassert_str_equal(fixture_text, "old");
	zassert_equal(fixture_data[0], 1);
	zassert_equal(fixture_data[1], 2);
	zassert_equal(text_changes, 2);
	zassert_equal(data_changes, 2);
}

#if defined(KFSW_TEST_FALLIBLE_PERSIST) || defined(KFSW_TEST_FALLIBLE_MIXED)
ZTEST(services_table, test_fallible_snapshot_and_defaults)
{
	struct kfsw_param_value value = {
		.type = KFSW_PARAM_U32, .size = sizeof(uint32_t), .scalar.u32 = 41};

	apply_calls = 0;
	notifications = 0;
	zassert_ok(kfsw_param_set("applied_value", &value));
	zassert_ok(kfsw_param_persist_save());
	value.scalar.u32 = 52;
	zassert_ok(kfsw_param_set("applied_value", &value));
	if (IS_ENABLED(KFSW_TEST_FALLIBLE_MIXED)) {
		set_u32("fixture_limit", 25);
		zassert_equal(kfsw_param_persist_load(), -ENOTSUP);
		zassert_equal(kfsw_param_restore_defaults(), -ENOTSUP);
		zassert_equal(applied_value, 52);
		zassert_equal(fixture_limit, 25);
		zassert_equal(apply_calls, 2);
		zassert_equal(notifications, 2);
		return;
	}
	apply_error = -EIO;
	zassert_equal(kfsw_param_persist_load(), -EIO);
	zassert_equal(kfsw_param_restore_defaults(), -EIO);
	zassert_equal(applied_value, 52);
	zassert_equal(apply_calls, 4);
	zassert_equal(notifications, 2);
	apply_error = 0;
	zassert_ok(kfsw_param_persist_load());
	zassert_equal(applied_value, 41);
	zassert_ok(kfsw_param_restore_defaults());
	zassert_equal(applied_value, 0);
	zassert_equal(apply_calls, 6);
	zassert_equal(notifications, 4);
}
#endif

#if defined(KFSW_TEST_TEXT_PERSIST)
ZTEST(services_table, test_text_and_data_snapshot_and_defaults)
{
	strcpy(fixture_text, "saved");
	fixture_data[0] = 3;
	fixture_data[1] = 4;
	text_changes = 0;
	data_changes = 0;
	zassert_ok(kfsw_param_persist_save());
	strcpy(fixture_text, "other");
	fixture_data[0] = 9;
	zassert_ok(kfsw_param_persist_load());
	zassert_str_equal(fixture_text, "saved");
	zassert_equal(fixture_data[0], 3);
	zassert_equal(fixture_data[1], 4);
	zassert_equal(text_changes, 1);
	zassert_equal(data_changes, 1);
	zassert_ok(kfsw_param_restore_defaults());
	zassert_str_equal(fixture_text, "");
	zassert_equal(fixture_data[0], 0);
	zassert_equal(fixture_data[1], 0);
	zassert_equal(text_changes, 2);
	zassert_equal(data_changes, 2);
}
#endif

static void table_before(void *fixture)
{
	struct kfsw_table_status status;

	ARG_UNUSED(fixture);

	/* The service keeps its counters and its held values for the lifetime of
	 * the image, so each test starts from whatever the last one left and
	 * asserts on differences where it matters. Reverting a pending load is
	 * the one piece of shared state worth clearing. */
	(void)kfsw_table_get_status(&status);
	if (status.state == KFSW_TABLE_LOADED) {
		(void)kfsw_table_revert();
	}
	(void)fs_unlink(TABLE_PATH);
}

static void *table_setup(void)
{
	const struct kfsw_param_definition_set *const sets[] = {
		&fixture_set, &fallible_set, &text_set, &kfsw_table_param_definitions};

	erase_storage_partition();
	zassert_ok(kfsw_storage_mount());
	(void)fs_mkdir(TABLE_DIRECTORY);
	zassert_ok(kfsw_param_init(sets, ARRAY_SIZE(sets)));
	zassert_equal(apply_calls, 0, "registry initialization applied hardware");
	return NULL;
}

ZTEST_SUITE(services_table, NULL, table_setup, table_before, NULL, NULL);
