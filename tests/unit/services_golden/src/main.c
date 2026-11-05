#include <errno.h>
#include <string.h>
#include <bootutil/image.h>
#include <kfsw/services/fwu.h>
#include <kfsw/services/golden.h>
#include <zephyr/drivers/flash/flash_simulator.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/ztest.h>

#include "golden_image.h"

#define TARGET DT_CHOSEN(kfsw_fwu_partition)
#define GOLDEN DT_CHOSEN(kfsw_golden_partition)
/* Where golden.c starts parsing the TLV area of an untouched image. */
#define HASHED (DT_REG_SIZE(GOLDEN) - GOLDEN_IMAGE_TLV_SIZE)
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

/* Put the image buffer, however it has been edited, in the recovery partition.
 * The erase here is on the recovery slot, not the secondary one the tests watch.
 */
static void store_golden(void)
{
	const struct flash_area *area;

	zassert_ok(flash_area_open(DT_FIXED_PARTITION_ID(GOLDEN), &area));
	zassert_ok(flash_area_flatten(area, 0, area->fa_size));
	zassert_ok(flash_area_write(area, 0, image, sizeof(image)));
	flash_area_close(area);
	erase_calls = 0U;
}

/* Rewrite the three header fields the installer reads before it trusts the
 * image, leaving the magic alone so the image still counts as present.
 */
static void store_header(uint16_t header_size, uint16_t protected_size, uint32_t body_size)
{
	struct image_header header;

	memcpy(&header, image, sizeof(header));
	header.ih_hdr_size = sys_cpu_to_le16(header_size);
	header.ih_protect_tlv_size = sys_cpu_to_le16(protected_size);
	header.ih_img_size = sys_cpu_to_le32(body_size);
	memcpy(image, &header, sizeof(header));
	store_golden();
}

/* An image the installer must refuse without erasing the secondary slot, which
 * is the whole point of checking it here instead of leaving it to MCUboot.
 */
static void refused(int expected)
{
	struct kfsw_fwu_status status;

	zassert_true(kfsw_golden_present());
	zassert_equal(kfsw_golden_install(), expected);
	zassert_equal(erase_calls, 0U, "the secondary slot must stay untouched");
	zassert_ok(kfsw_fwu_get_status(&status));
	zassert_equal(status.state, KFSW_FWU_IDLE);
}

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
	golden_image_build(image, sizeof(image), 0U);
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

ZTEST(golden, test_damaged_image_refused_before_the_slot_erase)
{
	image[IMAGE_HEADER_SIZE] ^= 0x01U;
	store_golden();
	refused(-EBADMSG);
}

/* A partition that holds no image at all is not a damaged one: there is
 * nothing to install and nothing to report as corrupt.
 */
ZTEST(golden, test_image_without_the_magic_is_absent)
{
	struct kfsw_fwu_status status;

	image[0] ^= 0xFFU;
	store_golden();
	zassert_false(kfsw_golden_present());
	zassert_equal(kfsw_golden_install(), -ENOENT);
	zassert_equal(erase_calls, 0U, "the secondary slot must stay untouched");
	zassert_ok(kfsw_fwu_get_status(&status));
	zassert_equal(status.state, KFSW_FWU_IDLE);
}

/* Header sizes are read before anything is hashed, so a header that claims
 * more than the partition holds must be refused on its own arithmetic.
 */
ZTEST(golden, test_header_larger_than_the_partition_refused)
{
	store_header(UINT16_MAX, 0U, 0U);
	refused(-EBADMSG);
}

ZTEST(golden, test_body_larger_than_the_partition_refused)
{
	store_header(IMAGE_HEADER_SIZE, 0U, sizeof(image));
	refused(-EBADMSG);
}

/* The TLV area needs its own header. A body that ends two bytes from the end
 * of the partition leaves no room for one.
 */
ZTEST(golden, test_body_leaving_no_room_for_the_tlv_header_refused)
{
	store_header(IMAGE_HEADER_SIZE, 0U, sizeof(image) - IMAGE_HEADER_SIZE - 2U);
	refused(-EBADMSG);
}

ZTEST(golden, test_tlv_area_without_its_magic_refused)
{
	struct image_tlv_info info;

	memcpy(&info, &image[HASHED], sizeof(info));
	info.it_magic = sys_cpu_to_le16(0U);
	memcpy(&image[HASHED], &info, sizeof(info));
	store_golden();
	refused(-EBADMSG);
}

ZTEST(golden, test_tlv_area_longer_than_the_partition_refused)
{
	struct image_tlv_info info;

	memcpy(&info, &image[HASHED], sizeof(info));
	info.it_tlv_tot = sys_cpu_to_le16(UINT16_MAX);
	memcpy(&image[HASHED], &info, sizeof(info));
	store_golden();
	refused(-EBADMSG);
}

/* A single entry that claims more bytes than the area it sits in would walk
 * the parser past the end of the TLV area.
 */
ZTEST(golden, test_tlv_entry_overrunning_the_area_refused)
{
	struct image_tlv tlv;

	memcpy(&tlv, &image[HASHED + sizeof(struct image_tlv_info)], sizeof(tlv));
	tlv.it_len = sys_cpu_to_le16(GOLDEN_IMAGE_SHA256_SIZE + 1U);
	memcpy(&image[HASHED + sizeof(struct image_tlv_info)], &tlv, sizeof(tlv));
	store_golden();
	refused(-EBADMSG);
}

/* A well-formed TLV area with no SHA-256 entry gives nothing to compare the
 * image against, which is refused rather than taken on trust.
 */
ZTEST(golden, test_tlv_area_without_a_digest_refused)
{
	struct image_tlv tlv;

	memcpy(&tlv, &image[HASHED + sizeof(struct image_tlv_info)], sizeof(tlv));
	tlv.it_type = sys_cpu_to_le16(IMAGE_TLV_SHA512);
	memcpy(&image[HASHED + sizeof(struct image_tlv_info)], &tlv, sizeof(tlv));
	store_golden();
	refused(-EBADMSG);
}

/* A SHA-256 entry of the wrong length is not a digest this installer can use,
 * so the walk goes past it and finds nothing.
 */
ZTEST(golden, test_digest_of_the_wrong_length_refused)
{
	struct image_tlv tlv;

	memcpy(&tlv, &image[HASHED + sizeof(struct image_tlv_info)], sizeof(tlv));
	tlv.it_len = sys_cpu_to_le16(GOLDEN_IMAGE_SHA256_SIZE - 1U);
	memcpy(&image[HASHED + sizeof(struct image_tlv_info)], &tlv, sizeof(tlv));
	store_golden();
	refused(-EBADMSG);
}

ZTEST_SUITE(golden, NULL, NULL, before, NULL, NULL);
