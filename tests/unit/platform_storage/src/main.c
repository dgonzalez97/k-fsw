#include <errno.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/devicetree.h>
#include <zephyr/fs/fs.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/ztest.h>

#include <kfsw/platform/storage.h>

#define STORAGE_PARTITION_NODE DT_CHOSEN(kfsw_storage_partition)
#define STORAGE_PARTITION_ID DT_FIXED_PARTITION_ID(STORAGE_PARTITION_NODE)
#define STORAGE_TEST_PATH KFSW_STORAGE_MOUNT_POINT "/unit-test"

static void erase_storage_partition(void)
{
	const struct flash_area *area;
	int result;

	result = flash_area_open(STORAGE_PARTITION_ID, &area);
	zassert_ok(result, "failed to open storage partition");
	result = flash_area_flatten(area, 0, area->fa_size);
	flash_area_close(area);
	zassert_ok(result, "failed to erase storage partition");
}

static void *storage_setup(void)
{
	erase_storage_partition();
	zassert_ok(kfsw_storage_init(), "storage init failed");
	zassert_ok(kfsw_storage_mount(), "first-boot storage mount failed");
	return NULL;
}

ZTEST(platform_storage, test_basic_file_operation)
{
	static const char expected[] = "K-FSW storage unit test";
	struct fs_file_t file;
	char actual[sizeof(expected)] = {0};
	ssize_t result;

	fs_file_t_init(&file);
	zassert_ok(fs_open(&file, STORAGE_TEST_PATH, FS_O_CREATE | FS_O_WRITE | FS_O_TRUNC),
		   "file create failed");
	result = fs_write(&file, expected, sizeof(expected));
	zassert_equal(result, sizeof(expected), "file write failed");
	zassert_ok(fs_close(&file), "file close after write failed");

	fs_file_t_init(&file);
	zassert_ok(fs_open(&file, STORAGE_TEST_PATH, FS_O_READ), "file reopen failed");
	result = fs_read(&file, actual, sizeof(actual));
	zassert_equal(result, sizeof(actual), "file read failed");
	zassert_mem_equal(actual, expected, sizeof(expected), "file content mismatch");
	zassert_ok(fs_close(&file), "file close after read failed");
	zassert_ok(fs_unlink(STORAGE_TEST_PATH), "file delete failed");
}

ZTEST(platform_storage, test_corrupt_non_erased_media_is_not_formatted)
{
	const struct flash_area *area;
	uint8_t corrupt_byte = 0U;
	uint8_t actual_byte = UINT8_MAX;
	int result;

	zassert_ok(kfsw_storage_unmount(), "storage unmount failed");
	erase_storage_partition();

	zassert_ok(flash_area_open(STORAGE_PARTITION_ID, &area), "partition open failed");
	zassert_ok(flash_area_write(area, 0, &corrupt_byte, sizeof(corrupt_byte)),
		   "corrupt marker write failed");
	flash_area_close(area);

	result = kfsw_storage_mount();
	zassert_equal(result, -EFAULT, "corrupt media was not rejected");
	zassert_false(kfsw_storage_is_ready(), "corrupt media reported ready");

	zassert_ok(flash_area_open(STORAGE_PARTITION_ID, &area), "partition reopen failed");
	zassert_ok(flash_area_read(area, 0, &actual_byte, sizeof(actual_byte)),
		   "corrupt marker read failed");
	flash_area_close(area);
	zassert_equal(actual_byte, corrupt_byte, "corrupt media was automatically formatted");

	erase_storage_partition();
	zassert_ok(kfsw_storage_mount(), "storage recovery mount failed");
}

ZTEST(platform_storage, test_info_and_invalid_argument)
{
	struct kfsw_storage_info info;

	zassert_equal(kfsw_storage_get_info(NULL), -EINVAL, "NULL info was accepted");
	zassert_ok(kfsw_storage_get_info(&info), "storage info failed");
	zassert_true(info.ready, "storage info did not report ready");
	zassert_equal(strcmp(info.filesystem, "LittleFS"), 0, "wrong filesystem");
	zassert_equal(strcmp(info.mount_point, KFSW_STORAGE_MOUNT_POINT), 0, "wrong mount point");
	zassert_not_null(info.backend, "missing backend name");
	zassert_true(info.total_bytes > 0U, "total capacity was not reported");
	zassert_true(info.free_bytes > 0U, "free capacity was not reported");
	zassert_true(info.free_bytes <= info.total_bytes, "free capacity exceeds total");
}

ZTEST(platform_storage, test_lifecycle_is_idempotent)
{
	zassert_ok(kfsw_storage_init(), "repeated init failed");
	zassert_ok(kfsw_storage_mount(), "repeated mount failed");
	zassert_true(kfsw_storage_is_ready(), "storage is not ready");
}

ZTEST(platform_storage, test_unmounted_state_is_reported)
{
	struct kfsw_storage_info info;

	zassert_ok(kfsw_storage_unmount(), "storage unmount failed");
	zassert_ok(kfsw_storage_unmount(), "repeated unmount failed");
	zassert_false(kfsw_storage_is_ready(), "unmounted storage reported ready");
	zassert_ok(kfsw_storage_get_info(&info), "unmounted storage info failed");
	zassert_false(info.ready, "unmounted info reported ready");
	zassert_equal(info.total_bytes, 0U, "unmounted total capacity is nonzero");
	zassert_equal(info.free_bytes, 0U, "unmounted free capacity is nonzero");
	zassert_ok(kfsw_storage_mount(), "remount failed");
}

ZTEST_SUITE(platform_storage, NULL, storage_setup, NULL, NULL, NULL);
