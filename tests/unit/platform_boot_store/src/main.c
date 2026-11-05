#include <errno.h>
#include <stdint.h>

#include <zephyr/kvss/nvs.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/crc.h>
#include <zephyr/ztest.h>

#include <kfsw/platform/boot_store.h>

static int mount_error;
static bool bad_crc;

/* Fails one mount and then steps aside, which is what a store NVS refuses
 * looks like: the erase makes the next mount succeed.
 */
int __real_nvs_mount(struct nvs_fs *fs);
int __wrap_nvs_mount(struct nvs_fs *fs)
{
	int error = mount_error;

	mount_error = 0;
	return error != 0 ? error : __real_nvs_mount(fs);
}

uint32_t __real_crc32_ieee(const uint8_t *data, size_t size);
uint32_t __wrap_crc32_ieee(const uint8_t *data, size_t size)
{
	return __real_crc32_ieee(data, size) ^ (bad_crc ? 1U : 0U);
}

/* The store is one process-wide record that opens once and stays open, so its
 * behaviour is a single ordered sequence rather than independent cases.
 */
ZTEST(boot_store, test_store_opened_from_a_mount_nvs_refuses)
{
	uint8_t left = 0xFFU;
	uint8_t maximum = 0xFFU;

	/* Nothing is readable or writable until the record has been opened. */
	zassert_equal(kfsw_boot_store_read(&left, &maximum), -ENODEV);
	zassert_equal(kfsw_boot_store_write(3U, 3U), -ENODEV);

	/* NVS refuses both sectors closed for good, so the store is erased and
	   mounted again rather than left unusable for the rest of the mission. */
	mount_error = -EDEADLK;
	zassert_ok(kfsw_boot_store_open());
	zassert_equal(mount_error, 0, "the erase must be followed by a second mount");

	/* The erase leaves an empty store, which is not a damaged one. */
	zassert_equal(kfsw_boot_store_read(&left, &maximum), -ENOENT);

	zassert_ok(kfsw_boot_store_write(2U, 5U));
	zassert_ok(kfsw_boot_store_read(&left, &maximum));
	zassert_equal(left, 2U);
	zassert_equal(maximum, 5U);

	/* A record whose checksum does not match its bytes is reported as
	   damaged, not returned as a budget. */
	bad_crc = true;
	zassert_equal(kfsw_boot_store_read(&left, &maximum), -EBADMSG);
	bad_crc = false;

	/* Opening an open store is free and changes nothing. */
	zassert_ok(kfsw_boot_store_open());
	zassert_ok(kfsw_boot_store_read(&left, &maximum));
	zassert_equal(left, 2U);
	zassert_equal(maximum, 5U);
}

ZTEST_SUITE(boot_store, NULL, NULL, NULL, NULL, NULL);
