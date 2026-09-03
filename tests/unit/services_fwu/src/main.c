#include <errno.h>
#include <string.h>

#include <zephyr/devicetree.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/crc.h>
#include <zephyr/sys/util.h>
#include <zephyr/ztest.h>

#include <kfsw/services/fwu.h>

/* This suite runs on native_sim against the simulated flash, so the real write
 * path is exercised rather than a stand-in: every acceptance and rejection
 * below ends in actual flash. What it cannot cover is the bootloader handshake,
 * because there is no bootloader here; that is what the HIL acceptance is for.
 */

#define FWU_PARTITION_NODE DT_CHOSEN(kfsw_fwu_partition)
#define FWU_PARTITION_ID DT_FIXED_PARTITION_ID(FWU_PARTITION_NODE)
#define FWU_PARTITION_SIZE DT_REG_SIZE(FWU_PARTITION_NODE)

/* Comfortably larger than the 4 KB erase block, so multi-sector writes and the
 * stream buffer boundary are both crossed.
 */
#define TEST_IMAGE_SIZE 10000U

static uint8_t test_image[TEST_IMAGE_SIZE];

static void fill_test_image(void)
{
	for (size_t index = 0U; index < sizeof(test_image); index++) {
		test_image[index] = (uint8_t)((index * 31U) + 7U);
	}
}

static void *fwu_setup(void)
{
	fill_test_image();
	return NULL;
}

static void fwu_before(void *fixture)
{
	ARG_UNUSED(fixture);
	(void)kfsw_fwu_abort();
}

/* Read what actually reached the flash, at the offset the service claims to
 * write at.
 */
static int read_slot(uint32_t image_offset, uint8_t *destination, size_t size)
{
	const struct flash_area *area;
	int result;

	result = flash_area_open(FWU_PARTITION_ID, &area);
	if (result != 0) {
		return result;
	}

	result = flash_area_read(area, kfsw_fwu_slot_write_offset() + image_offset, destination,
				 size);
	flash_area_close(area);
	return result;
}

static int transfer_whole_image(uint32_t size, uint32_t crc, size_t chunk)
{
	uint32_t offset = 0U;
	int result;

	result = kfsw_fwu_begin(size, crc);
	if (result != 0) {
		return result;
	}

	while (offset < size) {
		size_t span = MIN(chunk, (size_t)(size - offset));

		result = kfsw_fwu_write(offset, &test_image[offset], span);
		if (result != 0) {
			return result;
		}
		offset += span;
	}

	return kfsw_fwu_finish();
}

/* ------------------------------------------------------------------ geometry */

ZTEST(services_fwu, test_target_is_bound_and_sized)
{
	struct kfsw_fwu_status status;

	zassert_ok(kfsw_fwu_get_status(&status));
	zassert_true(status.target_bound, "the test overlay must bind a target partition");
	zassert_true(kfsw_fwu_max_image_size() > 0U);
}

ZTEST(services_fwu, test_write_offset_skips_a_whole_sector)
{
	/* MCUboot's swap-using-offset mode needs the image one sector in.
	 * Writing at the slot start is a silent no-op on hardware, so the
	 * offset is asserted rather than assumed.
	 */
	uint32_t offset = kfsw_fwu_slot_write_offset();

	zassert_equal(offset, 4096U, "expected one 4 KB sector, got %u", offset);
}

ZTEST(services_fwu, test_max_size_reserves_the_offset_and_the_trailer)
{
	/* The usable image is the partition less the swap offset at the front
	 * and the bootloader's trailer at the back. Getting this wrong lets a
	 * maximum-sized image overwrite the metadata that says it is there.
	 */
	uint32_t expected = FWU_PARTITION_SIZE - 4096U - 4096U;

	zassert_equal(kfsw_fwu_max_image_size(), expected);
	zassert_true(kfsw_fwu_max_image_size() < FWU_PARTITION_SIZE);
}

/* --------------------------------------------------------------------- begin */

ZTEST(services_fwu, test_begin_rejects_an_empty_image)
{
	zassert_equal(kfsw_fwu_begin(0U, 0U), -EINVAL);
}

ZTEST(services_fwu, test_begin_rejects_an_image_larger_than_the_slot)
{
	zassert_equal(kfsw_fwu_begin(kfsw_fwu_max_image_size() + 1U, 0U), -EFBIG);
	zassert_equal(kfsw_fwu_begin(FWU_PARTITION_SIZE, 0U), -EFBIG);
}

ZTEST(services_fwu, test_begin_accepts_exactly_the_maximum)
{
	zassert_ok(kfsw_fwu_begin(kfsw_fwu_max_image_size(), 0U));
	zassert_ok(kfsw_fwu_abort());
}

ZTEST(services_fwu, test_begin_rejects_a_second_transfer)
{
	zassert_ok(kfsw_fwu_begin(TEST_IMAGE_SIZE, 0U));
	zassert_equal(kfsw_fwu_begin(TEST_IMAGE_SIZE, 0U), -EBUSY,
		      "a transfer already in progress must not be silently restarted");
	zassert_ok(kfsw_fwu_abort());
}

/* --------------------------------------------------------------------- write */

ZTEST(services_fwu, test_write_rejects_bad_arguments)
{
	zassert_ok(kfsw_fwu_begin(TEST_IMAGE_SIZE, 0U));
	zassert_equal(kfsw_fwu_write(0U, NULL, 4U), -EINVAL);
	zassert_equal(kfsw_fwu_write(0U, test_image, 0U), -EINVAL);
	zassert_ok(kfsw_fwu_abort());
}

ZTEST(services_fwu, test_write_is_rejected_when_no_transfer_is_running)
{
	zassert_equal(kfsw_fwu_write(0U, test_image, 16U), -EINVAL);
}

ZTEST(services_fwu, test_write_rejects_a_gap)
{
	/* A hole would leave erased flash inside the image. It might still pass
	 * a whole-image CRC only by collision, but it would certainly not be
	 * noticed until the bootloader jumped into it.
	 */
	zassert_ok(kfsw_fwu_begin(TEST_IMAGE_SIZE, 0U));
	zassert_ok(kfsw_fwu_write(0U, test_image, 100U));
	zassert_equal(kfsw_fwu_write(200U, &test_image[200], 100U), -ESPIPE);
	zassert_ok(kfsw_fwu_abort());
}

ZTEST(services_fwu, test_write_rejects_going_backwards)
{
	zassert_ok(kfsw_fwu_begin(TEST_IMAGE_SIZE, 0U));
	zassert_ok(kfsw_fwu_write(0U, test_image, 100U));
	zassert_equal(kfsw_fwu_write(0U, test_image, 100U), -ESPIPE,
		      "a repeated span must not be written twice");
	zassert_ok(kfsw_fwu_abort());
}

ZTEST(services_fwu, test_write_rejects_running_past_the_declared_size)
{
	zassert_ok(kfsw_fwu_begin(100U, 0U));
	zassert_equal(kfsw_fwu_write(0U, test_image, 101U), -EFBIG);
	zassert_ok(kfsw_fwu_write(0U, test_image, 100U));
	zassert_equal(kfsw_fwu_write(100U, test_image, 1U), -EFBIG);
	zassert_ok(kfsw_fwu_abort());
}

/* -------------------------------------------------------------------- finish */

ZTEST(services_fwu, test_finish_is_rejected_when_no_transfer_is_running)
{
	zassert_equal(kfsw_fwu_finish(), -EINVAL);
}

ZTEST(services_fwu, test_finish_rejects_a_short_image)
{
	zassert_ok(kfsw_fwu_begin(TEST_IMAGE_SIZE, 0U));
	zassert_ok(kfsw_fwu_write(0U, test_image, 100U));
	zassert_equal(kfsw_fwu_finish(), -EAGAIN,
		      "an image shorter than declared must not be offered");
	zassert_ok(kfsw_fwu_abort());
}

ZTEST(services_fwu, test_finish_rejects_a_crc_mismatch_and_erases_the_slot)
{
	struct kfsw_fwu_status before;
	struct kfsw_fwu_status status;
	uint32_t correct = crc32_ieee(test_image, TEST_IMAGE_SIZE);
	uint8_t readback[32];

	/* The counters are lifetime totals, not per-transfer, so the change
	 * across this test is what matters. */
	zassert_ok(kfsw_fwu_get_status(&before));

	zassert_equal(transfer_whole_image(TEST_IMAGE_SIZE, correct ^ 0xFFFFFFFFU, 512U), -EILSEQ);

	zassert_ok(kfsw_fwu_get_status(&status));
	zassert_equal(status.state, KFSW_FWU_FAILED);
	zassert_equal(status.failed - before.failed, 1U);
	zassert_equal(status.completed - before.completed, 0U);

	/* A rejected image must not be left where a bootloader could find it. */
	zassert_ok(read_slot(0U, readback, sizeof(readback)));
	for (size_t index = 0U; index < sizeof(readback); index++) {
		zassert_equal(readback[index], 0xFF,
			      "byte %u of a rejected image was left in the slot", index);
	}
}

ZTEST(services_fwu, test_accepted_image_reaches_flash_at_the_write_offset)
{
	/* The point of the whole service: the bytes are in the slot, one sector
	 * in, byte for byte.
	 */
	uint32_t crc = crc32_ieee(test_image, TEST_IMAGE_SIZE);
	static uint8_t readback[TEST_IMAGE_SIZE];

	zassert_ok(transfer_whole_image(TEST_IMAGE_SIZE, crc, 512U));

	zassert_ok(read_slot(0U, readback, sizeof(readback)));
	zassert_mem_equal(readback, test_image, TEST_IMAGE_SIZE,
			  "the image in flash does not match what was sent");
}

ZTEST(services_fwu, test_nothing_is_written_before_the_offset)
{
	/* If the offset were dropped, the image would start at the slot base
	 * and the bootloader would find nothing to swap -- silently.
	 */
	const struct flash_area *area;
	uint32_t crc = crc32_ieee(test_image, TEST_IMAGE_SIZE);
	uint8_t leading[256];

	zassert_ok(transfer_whole_image(TEST_IMAGE_SIZE, crc, 512U));

	zassert_ok(flash_area_open(FWU_PARTITION_ID, &area));
	zassert_ok(flash_area_read(area, 0, leading, sizeof(leading)));
	flash_area_close(area);

	for (size_t index = 0U; index < sizeof(leading); index++) {
		zassert_equal(leading[index], 0xFF, "byte %u before the write offset was written",
			      index);
	}
}

ZTEST(services_fwu, test_status_reports_a_completed_transfer)
{
	struct kfsw_fwu_status before;
	struct kfsw_fwu_status status;
	uint32_t crc = crc32_ieee(test_image, TEST_IMAGE_SIZE);

	zassert_ok(kfsw_fwu_get_status(&before));
	zassert_ok(transfer_whole_image(TEST_IMAGE_SIZE, crc, 512U));

	zassert_ok(kfsw_fwu_get_status(&status));
	zassert_equal(status.state, KFSW_FWU_READY);
	zassert_equal(status.total_size, TEST_IMAGE_SIZE);
	zassert_equal(status.received, TEST_IMAGE_SIZE);
	zassert_equal(status.expected_crc32, crc);
	zassert_equal(status.actual_crc32, crc);
	zassert_equal(status.completed - before.completed, 1U);
	zassert_equal(status.started - before.started, 1U);
	zassert_equal(status.failed - before.failed, 0U);
}

ZTEST(services_fwu, test_chunk_size_does_not_change_the_result)
{
	/* Chunks arrive at whatever size the link produces, including sizes
	 * that straddle the flash write block and the stream buffer.
	 */
	static const size_t chunks[] = {1U, 7U, 64U, 192U, 511U, 4096U};
	uint32_t crc = crc32_ieee(test_image, 2048U);

	for (size_t index = 0U; index < ARRAY_SIZE(chunks); index++) {
		static uint8_t readback[2048];

		zassert_ok(kfsw_fwu_abort());
		zassert_ok(transfer_whole_image(2048U, crc, chunks[index]), "chunk size %u failed",
			   (unsigned int)chunks[index]);
		zassert_ok(read_slot(0U, readback, sizeof(readback)));
		zassert_mem_equal(readback, test_image, sizeof(readback),
				  "chunk size %u produced the wrong image",
				  (unsigned int)chunks[index]);
	}
}

ZTEST(services_fwu, test_crc_matches_zephyr_over_the_same_bytes)
{
	/* Ground computes this with Python's zlib.crc32, which is the same
	 * IEEE polynomial. The tree also contains libcsp's Castagnoli CRC32,
	 * which is a different algorithm over the same bytes.
	 */
	struct kfsw_fwu_status status;
	uint32_t crc = crc32_ieee(test_image, 1024U);

	zassert_ok(transfer_whole_image(1024U, crc, 256U));
	zassert_ok(kfsw_fwu_get_status(&status));
	zassert_equal(status.actual_crc32, crc);
	zassert_equal(crc32_ieee((const uint8_t *)"123456789", 9U), 0xcbf43926U,
		      "the CRC32 in use is not IEEE");
}

/* --------------------------------------------------------------------- abort */

ZTEST(services_fwu, test_abort_returns_to_idle_and_erases)
{
	struct kfsw_fwu_status status;
	uint8_t readback[64];

	zassert_ok(kfsw_fwu_begin(TEST_IMAGE_SIZE, 0U));
	zassert_ok(kfsw_fwu_write(0U, test_image, 4096U));
	zassert_ok(kfsw_fwu_abort());

	zassert_ok(kfsw_fwu_get_status(&status));
	zassert_equal(status.state, KFSW_FWU_IDLE);
	zassert_equal(status.received, 0U);
	zassert_equal(status.total_size, 0U);

	zassert_ok(read_slot(0U, readback, sizeof(readback)));
	for (size_t index = 0U; index < sizeof(readback); index++) {
		zassert_equal(readback[index], 0xFF,
			      "byte %u of an aborted image was left in the slot", index);
	}
}

ZTEST(services_fwu, test_abort_is_safe_when_idle)
{
	zassert_ok(kfsw_fwu_abort());
	zassert_ok(kfsw_fwu_abort());
}

ZTEST(services_fwu, test_a_new_transfer_can_follow_a_completed_one)
{
	uint32_t crc = crc32_ieee(test_image, 1024U);
	struct kfsw_fwu_status status;

	struct kfsw_fwu_status before;

	zassert_ok(kfsw_fwu_get_status(&before));
	zassert_ok(transfer_whole_image(1024U, crc, 256U));
	zassert_ok(transfer_whole_image(1024U, crc, 256U));

	zassert_ok(kfsw_fwu_get_status(&status));
	zassert_equal(status.state, KFSW_FWU_READY);
	zassert_equal(status.completed - before.completed, 2U);
}

ZTEST(services_fwu, test_a_new_transfer_can_follow_a_failure)
{
	uint32_t crc = crc32_ieee(test_image, 1024U);

	zassert_equal(transfer_whole_image(1024U, crc + 1U, 256U), -EILSEQ);
	zassert_ok(kfsw_fwu_abort());
	zassert_ok(transfer_whole_image(1024U, crc, 256U));
}

/* ----------------------------------------------------------------- interface */

ZTEST(services_fwu, test_get_status_rejects_null)
{
	zassert_equal(kfsw_fwu_get_status(NULL), -EINVAL);
}

ZTEST(services_fwu, test_state_names_are_reported)
{
	zassert_str_equal(kfsw_fwu_state_name(KFSW_FWU_IDLE), "idle");
	zassert_str_equal(kfsw_fwu_state_name(KFSW_FWU_RECEIVING), "receiving");
	zassert_str_equal(kfsw_fwu_state_name(KFSW_FWU_READY), "ready");
	zassert_str_equal(kfsw_fwu_state_name(KFSW_FWU_FAILED), "failed");
	zassert_str_equal(kfsw_fwu_state_name((enum kfsw_fwu_state)99), "unknown");
}

ZTEST_SUITE(services_fwu, NULL, fwu_setup, fwu_before, NULL, NULL);
