#include <errno.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/devicetree.h>
#include <zephyr/fs/fs.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/crc.h>
#include <zephyr/ztest.h>

#include <kfsw/platform/storage.h>
#include <kfsw/services/log.h>
#include <kfsw/services/parameter.h>

#define STORAGE_PARTITION_NODE DT_CHOSEN(kfsw_storage_partition)
#define STORAGE_PARTITION_ID DT_FIXED_PARTITION_ID(STORAGE_PARTITION_NODE)
#define SNAPSHOT_DIRECTORY KFSW_STORAGE_MOUNT_POINT "/params"
#define SNAPSHOT_PATH SNAPSHOT_DIRECTORY "/parameters.dat"
#define SNAPSHOT_HEADER_SIZE 20U
#define SNAPSHOT_CRC_OFFSET 16U
#define SNAPSHOT_MAX_SIZE 256U
#define SNAPSHOT_EXPECTED_SIZE 84U

static uint8_t snapshot[SNAPSHOT_MAX_SIZE];

static void erase_storage_partition(void)
{
	const struct flash_area *area;

	zassert_ok(flash_area_open(STORAGE_PARTITION_ID, &area), "partition open failed");
	zassert_ok(flash_area_flatten(area, 0, area->fa_size), "partition erase failed");
	flash_area_close(area);
}

static void set_u32(const char *name, uint32_t raw_value)
{
	struct kfsw_param_value value;

	zassert_ok(kfsw_param_get(name, &value), "parameter get failed");
	zassert_equal(value.type, KFSW_PARAM_U32, "unexpected parameter type");
	value.scalar.u32 = raw_value;
	zassert_ok(kfsw_param_set(name, &value), "parameter set failed");
}

static uint32_t get_u32(const char *name)
{
	struct kfsw_param_value value;

	zassert_ok(kfsw_param_get(name, &value), "parameter get failed");
	zassert_equal(value.type, KFSW_PARAM_U32, "unexpected parameter type");
	return value.scalar.u32;
}

static void set_i32(const char *name, int32_t raw_value)
{
	struct kfsw_param_value value;

	zassert_ok(kfsw_param_get(name, &value), "parameter get failed");
	zassert_equal(value.type, KFSW_PARAM_I32, "unexpected parameter type");
	value.scalar.i32 = raw_value;
	zassert_ok(kfsw_param_set(name, &value), "parameter set failed");
}

static int32_t get_i32(const char *name)
{
	struct kfsw_param_value value;

	zassert_ok(kfsw_param_get(name, &value), "parameter get failed");
	zassert_equal(value.type, KFSW_PARAM_I32, "unexpected parameter type");
	return value.scalar.i32;
}

static void set_u8(const char *name, uint8_t raw_value)
{
	struct kfsw_param_value value;

	zassert_ok(kfsw_param_get(name, &value), "parameter get failed");
	zassert_equal(value.type, KFSW_PARAM_U8, "unexpected parameter type");
	value.scalar.u8 = raw_value;
	zassert_ok(kfsw_param_set(name, &value), "parameter set failed");
}

static size_t read_snapshot(void)
{
	struct fs_dirent entry;
	struct fs_file_t file;
	ssize_t bytes_read;

	zassert_ok(fs_stat(SNAPSHOT_PATH, &entry), "snapshot stat failed");
	zassert_equal(entry.type, FS_DIR_ENTRY_FILE, "snapshot is not a file");
	zassert_true(entry.size <= sizeof(snapshot), "snapshot is oversized");
	fs_file_t_init(&file);
	zassert_ok(fs_open(&file, SNAPSHOT_PATH, FS_O_READ), "snapshot open failed");
	bytes_read = fs_read(&file, snapshot, entry.size);
	zassert_equal(bytes_read, entry.size, "snapshot read failed");
	zassert_ok(fs_close(&file), "snapshot close failed");
	return entry.size;
}

static void write_snapshot(size_t size)
{
	struct fs_file_t file;
	ssize_t written;

	fs_file_t_init(&file);
	zassert_ok(fs_open(&file, SNAPSHOT_PATH, FS_O_WRITE | FS_O_TRUNC),
		   "snapshot rewrite open failed");
	written = fs_write(&file, snapshot, size);
	zassert_equal(written, size, "snapshot rewrite failed");
	zassert_ok(fs_sync(&file), "snapshot sync failed");
	zassert_ok(fs_close(&file), "snapshot rewrite close failed");
}

static void update_snapshot_crc(size_t size)
{
	sys_put_be32(0U, &snapshot[SNAPSHOT_CRC_OFFSET]);
	sys_put_be32(crc32_ieee(snapshot, size), &snapshot[SNAPSHOT_CRC_OFFSET]);
}

static size_t find_snapshot_entry(const char *name, size_t size)
{
	const uint16_t entry_count = sys_get_be16(&snapshot[12]);
	size_t offset = SNAPSHOT_HEADER_SIZE;
	const size_t target_size = strlen(name);

	for (uint16_t index = 0U; index < entry_count; index++) {
		const size_t entry_offset = offset;
		const uint8_t name_size = snapshot[offset];
		const uint16_t value_size = sys_get_be16(&snapshot[offset + 2U]);

		offset += 4U;
		zassert_true(offset + name_size + value_size <= size, "invalid fixture snapshot");
		if ((name_size == target_size) &&
		    (memcmp(&snapshot[offset], name, name_size) == 0)) {
			return entry_offset;
		}
		offset += name_size + value_size;
	}
	return SIZE_MAX;
}

static void *persistence_setup(void)
{
	erase_storage_partition();
	zassert_ok(kfsw_storage_init(), "storage init failed");
	zassert_ok(kfsw_storage_mount(), "storage mount failed");
	zassert_ok(kfsw_param_init(), "parameter init failed");
	return NULL;
}

static void persistence_before(void *fixture)
{
	ARG_UNUSED(fixture);

	zassert_ok(kfsw_param_persist_clear(), "snapshot clear failed");
	zassert_ok(kfsw_param_restore_defaults(), "default restore failed");
}

ZTEST(param_persistence, test_no_file_uses_compiled_defaults)
{
	zassert_equal(kfsw_param_persist_load(), -ENOENT, "missing snapshot was accepted");
	zassert_equal(get_u32("test_u32"), 42U, "compiled default changed");
}

ZTEST(param_persistence, test_save_modify_load_and_repeated_operations)
{
	set_u32("test_u32", 1234U);
	set_i32("test_i32", -1234);
	zassert_ok(kfsw_param_persist_save(), "first save failed");
	zassert_ok(kfsw_param_persist_save(), "repeated save failed");
	zassert_equal(read_snapshot(), SNAPSHOT_EXPECTED_SIZE, "unexpected snapshot size");

	set_u32("test_u32", 9U);
	set_i32("test_i32", 9);
	zassert_ok(kfsw_param_persist_load(), "first load failed");
	zassert_ok(kfsw_param_persist_load(), "repeated load failed");
	zassert_equal(get_u32("test_u32"), 1234U, "u32 was not restored");
	zassert_equal(get_i32("test_i32"), -1234, "i32 was not restored");
}

ZTEST(param_persistence, test_defaults_and_clear_have_separate_semantics)
{
	set_u32("test_u32", 1234U);
	zassert_ok(kfsw_param_persist_save(), "save failed");
	zassert_ok(kfsw_param_restore_defaults(), "defaults failed");
	zassert_equal(get_u32("test_u32"), 42U, "default was not restored");
	zassert_ok(kfsw_param_persist_load(), "snapshot was deleted by defaults");
	zassert_equal(get_u32("test_u32"), 1234U, "saved value was not reloaded");
	zassert_ok(kfsw_param_persist_clear(), "clear failed");
	zassert_equal(get_u32("test_u32"), 1234U, "clear changed RAM");
	zassert_equal(kfsw_param_persist_load(), -ENOENT, "clear retained snapshot");
}

ZTEST(param_persistence, test_read_only_parameter_is_excluded)
{
	size_t size;

	zassert_ok(kfsw_param_persist_save(), "save failed");
	size = read_snapshot();

	zassert_equal(find_snapshot_entry("node_id", size), SIZE_MAX,
		      "read-only node identity was persisted");
	zassert_not_equal(find_snapshot_entry("log_level", size), SIZE_MAX,
			  "persistent log level is absent");
}

ZTEST(param_persistence, test_crc_and_truncation_are_rejected_without_changes)
{
	size_t size;

	set_u32("test_u32", 1234U);
	zassert_ok(kfsw_param_persist_save(), "save failed");
	size = read_snapshot();
	snapshot[size - 1U] ^= 0x01U;
	write_snapshot(size);
	zassert_ok(kfsw_param_restore_defaults(), "defaults failed");
	zassert_equal(kfsw_param_persist_load(), -EBADMSG, "bad CRC was accepted");
	zassert_equal(get_u32("test_u32"), 42U, "bad CRC changed parameters");

	zassert_ok(kfsw_param_persist_save(), "replacement save failed");
	size = read_snapshot();
	write_snapshot(size - 1U);
	zassert_equal(kfsw_param_persist_load(), -EMSGSIZE, "truncated snapshot was accepted");
	zassert_equal(get_u32("test_u32"), 42U, "truncated snapshot changed parameters");
}

ZTEST(param_persistence, test_bad_magic_and_version_are_rejected)
{
	size_t size;

	zassert_ok(kfsw_param_persist_save(), "save failed");
	size = read_snapshot();
	snapshot[0] ^= 0x01U;
	write_snapshot(size);
	zassert_equal(kfsw_param_persist_load(), -EBADMSG, "bad magic was accepted");

	zassert_ok(kfsw_param_persist_save(), "replacement save failed");
	size = read_snapshot();
	sys_put_be16(2U, &snapshot[4]);
	write_snapshot(size);
	zassert_equal(kfsw_param_persist_load(), -EPROTONOSUPPORT,
		      "unsupported version was accepted");
}

ZTEST(param_persistence, test_unknown_and_incompatible_entries_are_ignored)
{
	size_t entry_offset;
	size_t size;

	set_u32("test_u32", 1234U);
	set_i32("test_i32", -1234);
	zassert_ok(kfsw_param_persist_save(), "save failed");
	size = read_snapshot();
	entry_offset = find_snapshot_entry("test_u32", size);
	zassert_not_equal(entry_offset, SIZE_MAX, "test entry missing");
	snapshot[entry_offset + 4U] = 'b';
	update_snapshot_crc(size);
	write_snapshot(size);
	zassert_ok(kfsw_param_restore_defaults(), "defaults failed");
	zassert_ok(kfsw_param_persist_load(), "unknown name invalidated snapshot");
	zassert_equal(get_u32("test_u32"), 42U, "unknown name changed u32");
	zassert_equal(get_i32("test_i32"), -1234, "known entry was not restored");

	set_u32("test_u32", 1234U);
	zassert_ok(kfsw_param_persist_save(), "replacement save failed");
	size = read_snapshot();
	entry_offset = find_snapshot_entry("test_u32", size);
	snapshot[entry_offset + 1U] = 1U;
	update_snapshot_crc(size);
	write_snapshot(size);
	zassert_ok(kfsw_param_restore_defaults(), "defaults failed");
	zassert_ok(kfsw_param_persist_load(), "incompatible type invalidated snapshot");
	zassert_equal(get_u32("test_u32"), 42U, "incompatible type changed u32");
}

ZTEST(param_persistence, test_restored_log_level_is_applied)
{
	struct kfsw_param_value value;

	set_u8("log_level", 3U);
	zassert_equal(kfsw_log_get_level(), 3U, "runtime log level was not changed");
	zassert_ok(kfsw_param_persist_save(), "save failed");
	zassert_ok(kfsw_param_restore_defaults(), "defaults failed");
	zassert_equal(kfsw_log_get_level(), CONFIG_KFSW_LOG_MIN_LEVEL,
		      "runtime default log level was not applied");
	zassert_ok(kfsw_param_persist_load(), "load failed");
	zassert_ok(kfsw_param_get("log_level", &value), "log level get failed");
	zassert_equal(value.scalar.u8, 3U, "log level value was not restored");
	zassert_equal(kfsw_log_get_level(), 3U, "restored log policy was not applied");
}

ZTEST(param_persistence, test_unavailable_storage_and_save_failure_are_reported)
{
	struct fs_file_t file;

	zassert_ok(kfsw_storage_unmount(), "storage unmount failed");
	zassert_equal(kfsw_param_persist_save(), -EACCES, "save ignored unavailable storage");
	zassert_equal(kfsw_param_persist_load(), -EACCES, "load ignored unavailable storage");
	zassert_ok(kfsw_storage_mount(), "storage remount failed");

	zassert_ok(fs_unlink(SNAPSHOT_DIRECTORY), "snapshot directory removal failed");
	fs_file_t_init(&file);
	zassert_ok(fs_open(&file, SNAPSHOT_DIRECTORY, FS_O_CREATE | FS_O_WRITE),
		   "blocking file create failed");
	zassert_ok(fs_close(&file), "blocking file close failed");
	zassert_true(kfsw_param_persist_save() < 0, "save failure was not reported");
	zassert_ok(fs_unlink(SNAPSHOT_DIRECTORY), "blocking file removal failed");
	zassert_ok(fs_mkdir(SNAPSHOT_DIRECTORY), "snapshot directory recovery failed");
}

ZTEST_SUITE(param_persistence, NULL, persistence_setup, persistence_before, NULL, NULL);
