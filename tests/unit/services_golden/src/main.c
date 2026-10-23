#include <errno.h>
#include <string.h>
#include <bootutil/image.h>
#include <kfsw/services/fwu.h>
#include <kfsw/services/golden.h>
#include <zephyr/drivers/flash/flash_simulator.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/ztest.h>

#define TARGET DT_CHOSEN(kfsw_fwu_partition)
#define GOLDEN DT_CHOSEN(kfsw_golden_partition)
static uint8_t image[DT_REG_SIZE(GOLDEN)];
static int erase_error;
static unsigned int fail_erase_at;
static unsigned int erase_calls;
static bool fail_write;

int __real_flash_area_flatten(const struct flash_area *area, off_t offset, size_t size);
int __wrap_flash_area_flatten(const struct flash_area *area, off_t offset, size_t size)
{
	if (area->fa_id == DT_FIXED_PARTITION_ID(TARGET)) {
		erase_calls++;
		if (erase_error != 0 || (fail_erase_at != 0U && erase_calls >= fail_erase_at)) {
			return erase_error != 0 ? erase_error : -EIO;
		}
	}
	return __real_flash_area_flatten(area, offset, size);
}

static int write_byte(const struct device *device, off_t offset, uint8_t value)
{
	ARG_UNUSED(device);
	if (fail_write && offset >= DT_REG_ADDR(TARGET) &&
	    offset < DT_REG_ADDR(TARGET) + DT_REG_SIZE(TARGET)) {
		return -EIO;
	}
	return value;
}

static const struct flash_simulator_cb callbacks = {.write_byte = write_byte};

static void before(void *fixture)
{
	const struct flash_area *area;
	struct kfsw_fwu_status status;

	ARG_UNUSED(fixture);
	erase_error = 0;
	fail_erase_at = erase_calls = 0U;
	fail_write = false;
	flash_simulator_set_callbacks(DEVICE_DT_GET(DT_MTD_FROM_FIXED_PARTITION(TARGET)),
				      &callbacks);
	zassert_ok(kfsw_fwu_get_status(&status));
	if (status.handle.generation != 0U) {
		zassert_ok(kfsw_fwu_session_abort(&status.handle, &status));
	}
	for (size_t i = 0U; i < sizeof(image); i++) {
		image[i] = (uint8_t)(i * 7U);
	}
	sys_put_le32(IMAGE_MAGIC, image);
	zassert_ok(flash_area_open(DT_FIXED_PARTITION_ID(GOLDEN), &area));
	zassert_ok(flash_area_flatten(area, 0, area->fa_size));
	zassert_ok(flash_area_write(area, 0, image, sizeof(image)));
	flash_area_close(area);
	erase_calls = 0U;
}

ZTEST(golden, test_failed_begin_cleanup_retried_on_next_install)
{
	struct kfsw_fwu_status first;
	struct kfsw_fwu_status after;

	erase_error = -EIO;
	zassert_equal(kfsw_golden_install(), -EIO);
	zassert_ok(kfsw_fwu_get_status(&first));
	zassert_equal(first.state, KFSW_FWU_FAILED);
	zassert_equal(kfsw_golden_install(), -EIO);
	zassert_ok(kfsw_fwu_get_status(&after));
	zassert_equal(after.handle.generation, first.handle.generation);
	erase_error = 0;
	zassert_ok(kfsw_golden_install());
	zassert_ok(kfsw_fwu_get_status(&after));
	zassert_equal(after.state, KFSW_FWU_READY);
	zassert_true(after.handle.generation > first.handle.generation);
}

ZTEST(golden, test_failed_copy_cleanup_retried_on_next_install)
{
	const struct flash_area *area;
	uint8_t readback[64];

	fail_write = true;
	fail_erase_at = 2U;
	zassert_equal(kfsw_golden_install(), -EIO);
	fail_write = false;
	fail_erase_at = 0U;
	zassert_ok(kfsw_golden_install());
	zassert_ok(flash_area_open(DT_FIXED_PARTITION_ID(TARGET), &area));
	for (size_t offset = 0; offset < sizeof(image); offset += sizeof(readback)) {
		zassert_ok(flash_area_read(area, kfsw_fwu_slot_write_offset() + offset, readback,
					   sizeof(readback)));
		zassert_mem_equal(readback, image + offset, sizeof(readback));
	}
	flash_area_close(area);
}

ZTEST(golden, test_cached_cleanup_cannot_adopt_successor)
{
	const struct kfsw_fwu_image description = {.size = sizeof(image)};
	struct kfsw_fwu_status status;
	struct kfsw_fwu_handle next;

	erase_error = -EIO;
	zassert_equal(kfsw_golden_install(), -EIO);
	zassert_ok(kfsw_fwu_get_status(&status));
	erase_error = 0;
	zassert_ok(kfsw_fwu_session_abort(&status.handle, &status));
	zassert_ok(kfsw_fwu_session_begin(&status.handle.owner, &description, 0U, &next, &status));
	zassert_equal(kfsw_golden_install(), -EBUSY);
	zassert_ok(kfsw_fwu_session_status(&next, &status));
	zassert_equal(status.state, KFSW_FWU_RECEIVING);
	zassert_false(status.aborted);
}

ZTEST_SUITE(golden, NULL, NULL, before, NULL, NULL);
