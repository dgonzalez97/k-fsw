#include <cmdline.h>
#include <errno.h>
#include <psa/crypto.h>
#include <posix_native_task.h>
#include <string.h>

#include <zephyr/devicetree.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/drivers/flash/flash_simulator.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/crc.h>
#include <zephyr/sys/util.h>
#include <zephyr/ztest.h>

#include <kfsw/services/fwu.h>

/* Runs on native_sim with simulated flash. The bootloader handshake is tested on hardware. */

#define FWU_PARTITION_NODE DT_CHOSEN(kfsw_fwu_partition)
#define FWU_PARTITION_ID DT_FIXED_PARTITION_ID(FWU_PARTITION_NODE)
#define FWU_PARTITION_SIZE DT_REG_SIZE(FWU_PARTITION_NODE)

/* Comfortably larger than the 4 KB erase block, so multi-sector writes and the
 * stream buffer boundary are both crossed.
 */
#define TEST_IMAGE_SIZE 10000U
#define PARTIAL_IMAGE_SIZE 4096U

static uint8_t test_image[TEST_IMAGE_SIZE];
static int erase_error;
static unsigned int erase_calls;
static int write_error;
static int read_error;
static bool corrupt_read;
static size_t bytes_read;
static bool restart_inspection;

static void add_restart_option(void)
{
	static struct args_struct_t options[] = {
		{
			.option = "fwu_restart_inspection",
			.type = 'b',
			.is_switch = true,
			.dest = &restart_inspection,
			.descript = "Inspect inherited partial FWU image without erasing it",
		},
		ARG_TABLE_ENDMARKER,
	};

	native_add_command_line_opts(options);
}

NATIVE_TASK(add_restart_option, PRE_BOOT_1, 10);

static int test_write_byte(const struct device *dev, off_t offset, uint8_t data)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(offset);
	return write_error != 0 ? write_error : data;
}

static const struct flash_simulator_cb flash_callbacks = {
	.write_byte = test_write_byte,
};

int __real_flash_area_flatten(const struct flash_area *area, off_t offset, size_t size);

int __wrap_flash_area_flatten(const struct flash_area *area, off_t offset, size_t size)
{
	erase_calls++;
	return erase_error != 0 ? erase_error : __real_flash_area_flatten(area, offset, size);
}

int __real_flash_area_read(const struct flash_area *area, off_t offset, void *data, size_t size);

int __wrap_flash_area_read(const struct flash_area *area, off_t offset, void *data, size_t size)
{
	int result;

	if (read_error != 0) {
		return read_error;
	}
	result = __real_flash_area_read(area, offset, data, size);
	if (result == 0) {
		bytes_read += size;
		if (corrupt_read && size > 0U) {
			((uint8_t *)data)[0] ^= 1U;
		}
	}
	return result;
}

static void fill_test_image(void)
{
	for (size_t index = 0U; index < sizeof(test_image); index++) {
		test_image[index] = (uint8_t)((index * 31U) + 7U);
	}
}

static void *fwu_setup(void)
{
	struct kfsw_fwu_status status;

	zassert_ok(kfsw_fwu_get_status(&status));
	zassert_equal(status.state, KFSW_FWU_IDLE);
	zassert_equal(status.received, 0U);
	zassert_false(status.swap_scheduled);

	fill_test_image();
	flash_simulator_set_callbacks(
		DEVICE_DT_GET(DT_MTD_FROM_FIXED_PARTITION(FWU_PARTITION_NODE)), &flash_callbacks);
	return NULL;
}

static void fwu_before(void *fixture)
{
	ARG_UNUSED(fixture);
	erase_error = 0;
	write_error = 0;
	read_error = 0;
	corrupt_read = false;
	bytes_read = 0U;
	struct kfsw_fwu_status status;

	zassert_ok(kfsw_fwu_get_status(&status));
	if (status.handle.generation != 0U) {
		zassert_ok(kfsw_fwu_session_abort(&status.handle, &status));
	}
}

ZTEST(services_fwu, test_failed_abort_retains_details_until_cleanup_succeeds)
{
	struct kfsw_fwu_status before;
	struct kfsw_fwu_status after;

	zassert_ok(kfsw_fwu_begin(TEST_IMAGE_SIZE, 0x12345678U));
	zassert_ok(kfsw_fwu_write(0, test_image, 19U));
	zassert_ok(kfsw_fwu_get_status(&before));
	erase_error = -EIO;
	zassert_equal(kfsw_fwu_abort(), -EIO);
	zassert_ok(kfsw_fwu_get_status(&after));
	zassert_equal(after.state, KFSW_FWU_FAILED);
	zassert_equal(after.total_size, before.total_size);
	zassert_equal(after.received, before.received);
	zassert_equal(after.expected_crc32, before.expected_crc32);
	zassert_equal(after.failed, before.failed + 1U);
	zassert_equal(kfsw_fwu_abort(), -EIO);
	zassert_ok(kfsw_fwu_get_status(&after));
	zassert_equal(after.failed, before.failed + 1U);
	erase_error = 0;
	zassert_ok(kfsw_fwu_abort());
	zassert_ok(kfsw_fwu_get_status(&after));
	zassert_equal(after.state, KFSW_FWU_IDLE);
	zassert_equal(after.received, 0U);
}

/* Read the flash at the offset the service writes to. */
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
	/* The image must start one sector in, for swap-using-offset. */
	uint32_t offset = kfsw_fwu_slot_write_offset();

	zassert_equal(offset, IS_ENABLED(CONFIG_KFSW_FWU_RECOVERY) ? 0U : 4096U);
}

ZTEST(services_fwu, test_max_size_reserves_the_offset_and_the_trailer)
{
	/* The size limit must reserve both the swap offset and bootloader trailer.
	 */
	uint32_t reserved = IS_ENABLED(CONFIG_KFSW_FWU_RECOVERY) ? 0U : 8192U;
	uint32_t expected = FWU_PARTITION_SIZE - reserved;

	zassert_equal(kfsw_fwu_max_image_size(), expected);
	zassert_true(kfsw_fwu_max_image_size() <= FWU_PARTITION_SIZE);
}

/* --------------------------------------------------------------------- begin */

ZTEST(services_fwu, test_begin_rejects_an_empty_image)
{
	zassert_equal(kfsw_fwu_begin(0U, 0U), -EINVAL);
}

ZTEST(services_fwu, test_begin_rejects_an_image_larger_than_the_slot)
{
	zassert_equal(kfsw_fwu_begin(kfsw_fwu_max_image_size() + 1U, 0U), -EFBIG);
	if (!IS_ENABLED(CONFIG_KFSW_FWU_RECOVERY)) {
		zassert_equal(kfsw_fwu_begin(FWU_PARTITION_SIZE, 0U), -EFBIG);
	}
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
		      "a transfer in progress must not be restarted");
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
	/* A gap would leave erased flash inside the image. */
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
	zassert_equal(kfsw_fwu_write(100U, test_image, SIZE_MAX), -EFBIG);
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

	/* The counters are lifetime totals, so check the change. */
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
	/* The bytes are in the slot, one sector in. */
	uint32_t crc = crc32_ieee(test_image, TEST_IMAGE_SIZE);
	static uint8_t readback[TEST_IMAGE_SIZE];

	zassert_ok(transfer_whole_image(TEST_IMAGE_SIZE, crc, 512U));

	zassert_ok(read_slot(0U, readback, sizeof(readback)));
	zassert_mem_equal(readback, test_image, TEST_IMAGE_SIZE,
			  "the image in flash does not match what was sent");
}

ZTEST(services_fwu, test_nothing_is_written_before_the_offset)
{
	/* Without the offset the bootloader would find nothing to swap. */
	const struct flash_area *area;
	uint32_t crc = crc32_ieee(test_image, TEST_IMAGE_SIZE);
	uint8_t leading[256];

	zassert_ok(transfer_whole_image(TEST_IMAGE_SIZE, crc, 512U));

	if (IS_ENABLED(CONFIG_KFSW_FWU_RECOVERY)) {
		ztest_test_skip();
	}
	zassert_ok(flash_area_open(FWU_PARTITION_ID, &area));
	zassert_ok(flash_area_read(area, 0, leading,
				   MIN(sizeof(leading), kfsw_fwu_slot_write_offset())));
	flash_area_close(area);

	for (size_t index = 0U; index < MIN(sizeof(leading), kfsw_fwu_slot_write_offset());
	     index++) {
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
	zassert_false(status.swap_scheduled);
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
	zassert_ok(kfsw_fwu_write(0U, test_image, PARTIAL_IMAGE_SIZE));
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
	zassert_false(status.swap_scheduled);
	zassert_equal(status.completed - before.completed, 2U);
}

ZTEST(services_fwu, test_a_new_transfer_can_follow_a_failure)
{
	uint32_t crc = crc32_ieee(test_image, 1024U);

	zassert_equal(transfer_whole_image(1024U, crc + 1U, 256U), -EILSEQ);
	zassert_ok(kfsw_fwu_abort());
	zassert_ok(transfer_whole_image(1024U, crc, 256U));
}

/* --------------------------------------------------------------- edge cases */

ZTEST(services_fwu, test_a_single_byte_image_is_accepted)
{
	/* A one-byte image stays buffered until finish; verify that finish flushes it.
	 */
	uint32_t crc = crc32_ieee(test_image, 1U);
	uint8_t readback[4];

	zassert_ok(kfsw_fwu_begin(1U, crc));
	zassert_ok(kfsw_fwu_write(0U, test_image, 1U));
	zassert_ok(kfsw_fwu_finish());

	zassert_ok(read_slot(0U, readback, sizeof(readback)));
	zassert_equal(readback[0], test_image[0], "the single byte never reached flash");
}

ZTEST(services_fwu, test_image_sizes_around_a_sector_boundary)
{
	/* Off-by-one around an erase block is where a streaming writer is most
	 * likely to drop or duplicate a byte.
	 */
	static const uint32_t sizes[] = {4095U, 4096U, 4097U, 8191U, 8192U, 8193U};

	for (size_t index = 0U; index < ARRAY_SIZE(sizes); index++) {
		static uint8_t readback[8193];
		uint32_t size = sizes[index];
		uint32_t crc = crc32_ieee(test_image, size);

		zassert_ok(kfsw_fwu_abort());
		zassert_ok(transfer_whole_image(size, crc, 333U), "size %u failed", size);
		zassert_ok(read_slot(0U, readback, size));
		zassert_mem_equal(readback, test_image, size, "size %u came back wrong", size);
	}
}

ZTEST(services_fwu, test_finish_replay_preserves_outcome_and_counters)
{
	struct kfsw_fwu_status before;
	struct kfsw_fwu_status after;
	uint32_t crc = crc32_ieee(test_image, 1024U);

	zassert_ok(transfer_whole_image(1024U, crc, 256U));
	zassert_ok(kfsw_fwu_get_status(&before));
	zassert_ok(kfsw_fwu_finish());
	zassert_ok(kfsw_fwu_get_status(&after));
	zassert_mem_equal(&before, &after, sizeof(before));
}

ZTEST(services_fwu, test_validation_reads_exact_declared_image)
{
	const uint32_t size = 513U;

	zassert_ok(kfsw_fwu_begin(size, crc32_ieee(test_image, size)));
	zassert_ok(kfsw_fwu_write(0U, test_image, size));
	bytes_read = 0U;
	zassert_ok(kfsw_fwu_verify());
	zassert_equal(bytes_read, size);
}

ZTEST(services_fwu, test_validation_rejects_flash_read_failure)
{
	struct kfsw_fwu_status status;

	zassert_ok(kfsw_fwu_begin(513U, crc32_ieee(test_image, 513U)));
	zassert_ok(kfsw_fwu_write(0U, test_image, 513U));
	read_error = -EIO;
	zassert_equal(kfsw_fwu_verify(), -EIO);
	zassert_ok(kfsw_fwu_get_status(&status));
	zassert_equal(status.state, KFSW_FWU_FAILED);
	zassert_false(status.swap_scheduled);
}

ZTEST(services_fwu, test_validation_rejects_corrupt_stored_bytes)
{
	struct kfsw_fwu_status status;

	zassert_ok(kfsw_fwu_begin(513U, crc32_ieee(test_image, 513U)));
	zassert_ok(kfsw_fwu_write(0U, test_image, 513U));
	corrupt_read = true;
	zassert_equal(kfsw_fwu_verify(), -EILSEQ);
	zassert_ok(kfsw_fwu_get_status(&status));
	zassert_equal(status.state, KFSW_FWU_FAILED);
	zassert_false(status.swap_scheduled);
}

ZTEST(services_fwu, test_write_after_finish_is_rejected)
{
	uint32_t crc = crc32_ieee(test_image, 1024U);

	zassert_ok(transfer_whole_image(1024U, crc, 256U));
	zassert_equal(kfsw_fwu_write(1024U, test_image, 16U), -EINVAL,
		      "a completed image must not be appended to");
}

ZTEST(services_fwu, test_abort_after_completion_returns_to_idle)
{
	struct kfsw_fwu_status status;
	uint32_t crc = crc32_ieee(test_image, 1024U);

	zassert_ok(transfer_whole_image(1024U, crc, 256U));
	zassert_ok(kfsw_fwu_abort());

	zassert_ok(kfsw_fwu_get_status(&status));
	zassert_equal(status.state, KFSW_FWU_IDLE);
}

ZTEST(services_fwu, test_an_all_zero_image_is_carried_faithfully)
{
	/* An all-zero image, which would look plausible if the write did nothing. */
	static uint8_t zeros[512];
	static uint8_t readback[512];
	uint32_t crc = crc32_ieee(zeros, sizeof(zeros));

	zassert_ok(kfsw_fwu_begin(sizeof(zeros), crc));
	zassert_ok(kfsw_fwu_write(0U, zeros, sizeof(zeros)));
	zassert_ok(kfsw_fwu_finish());

	zassert_ok(read_slot(0U, readback, sizeof(readback)));
	for (size_t index = 0U; index < sizeof(readback); index++) {
		zassert_equal(readback[index], 0x00,
			      "byte %u is 0x%02x, so the write did not happen", index,
			      readback[index]);
	}
}

ZTEST(services_fwu, test_a_failed_begin_leaves_no_transfer_running)
{
	struct kfsw_fwu_status status;

	zassert_equal(kfsw_fwu_begin(0U, 0U), -EINVAL);
	zassert_ok(kfsw_fwu_get_status(&status));
	zassert_not_equal(status.state, KFSW_FWU_RECEIVING);

	/* And a good transfer still works afterwards. */
	zassert_ok(transfer_whole_image(1024U, crc32_ieee(test_image, 1024U), 256U));
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

ZTEST(services_fwu, test_full_capacity_preserves_adjacent_flash)
{
	const struct flash_area *area;
	const struct device *device;
	uint8_t sentinel[8] = {0x51, 0x32, 0x17, 0x08, 0x93, 0x24, 0x65, 0x46};
	uint8_t readback[8];
	static uint8_t image_readback[TEST_IMAGE_SIZE];
	uint32_t maximum = kfsw_fwu_max_image_size();
	uint32_t crc = 0U;

	zassert_ok(flash_area_open(FWU_PARTITION_ID, &area));
	device = flash_area_get_device(area);
	zassert_ok(flash_erase(device, area->fa_off - 4096, 4096));
	zassert_ok(flash_erase(device, area->fa_off + area->fa_size, 4096));
	zassert_ok(
		flash_write(device, area->fa_off - sizeof(sentinel), sentinel, sizeof(sentinel)));
	zassert_ok(flash_write(device, area->fa_off + area->fa_size, sentinel, sizeof(sentinel)));
	for (uint32_t offset = 0U; offset < maximum;) {
		size_t span = MIN(sizeof(test_image), maximum - offset);

		crc = crc32_ieee_update(crc, test_image, span);
		offset += span;
	}
	zassert_ok(kfsw_fwu_begin(maximum, crc));
	for (uint32_t offset = 0U; offset < maximum;) {
		size_t span = MIN(sizeof(test_image), maximum - offset);

		zassert_ok(kfsw_fwu_write(offset, test_image, span));
		offset += span;
	}
	zassert_ok(kfsw_fwu_finish());
	for (uint32_t offset = 0U; offset < maximum;) {
		size_t span = MIN(sizeof(test_image), maximum - offset);

		zassert_ok(read_slot(offset, image_readback, span));
		zassert_mem_equal(image_readback, test_image, span);
		offset += span;
	}
	zassert_ok(flash_read(device, area->fa_off - sizeof(sentinel), readback, sizeof(readback)));
	zassert_mem_equal(readback, sentinel, sizeof(sentinel));
	zassert_ok(flash_read(device, area->fa_off + area->fa_size, readback, sizeof(readback)));
	zassert_mem_equal(readback, sentinel, sizeof(sentinel));
	flash_area_close(area);
}

ZTEST(services_fwu, test_erase_failure_refuses_transfer)
{
	struct kfsw_fwu_status status;

	erase_error = -EIO;
	zassert_equal(kfsw_fwu_begin(1024U, 0U), -EIO);
	zassert_ok(kfsw_fwu_get_status(&status));
	zassert_equal(status.state, KFSW_FWU_FAILED);
	zassert_equal(kfsw_fwu_write(0U, test_image, 8U), -EINVAL);
}

ZTEST(services_fwu, test_flash_write_failure_refuses_more_data)
{
	struct kfsw_fwu_status status;

	zassert_ok(kfsw_fwu_begin(TEST_IMAGE_SIZE, 0U));
	write_error = -EIO;
	zassert_equal(kfsw_fwu_write(0U, test_image, sizeof(test_image)), -EIO);
	zassert_ok(kfsw_fwu_get_status(&status));
	zassert_equal(status.state, KFSW_FWU_FAILED);
	zassert_equal(kfsw_fwu_write(0U, test_image, 8U), -EINVAL);
	zassert_equal(kfsw_fwu_finish(), -EINVAL);
	write_error = 0;
	zassert_ok(kfsw_fwu_abort());
}

ZTEST(services_fwu, test_flash_flush_failure_is_reported)
{
	struct kfsw_fwu_status status;

	zassert_ok(kfsw_fwu_begin(1U, crc32_ieee(test_image, 1U)));
	zassert_ok(kfsw_fwu_write(0U, test_image, 1U));
	write_error = -EIO;
	zassert_equal(kfsw_fwu_finish(), -EIO);
	zassert_ok(kfsw_fwu_get_status(&status));
	zassert_equal(status.state, KFSW_FWU_FAILED);
	write_error = 0;
	zassert_ok(kfsw_fwu_abort());
}

ZTEST(services_fwu, test_interruption_leaves_partial_image)
{
	uint8_t readback[8];

	zassert_ok(kfsw_fwu_begin(TEST_IMAGE_SIZE, crc32_ieee(test_image, TEST_IMAGE_SIZE)));
	zassert_ok(kfsw_fwu_write(0U, test_image, PARTIAL_IMAGE_SIZE));
	zassert_ok(read_slot(0U, readback, sizeof(readback)));
	zassert_mem_equal(readback, test_image, sizeof(readback));
	zassert_equal(kfsw_fwu_finish(), -EAGAIN);
}

static const struct kfsw_fwu_owner remote = {.kind = KFSW_FWU_OWNER_LITE, .peer = 16U};

static struct kfsw_fwu_image digest_image(void)
{
	struct kfsw_fwu_image image = {
		.size = 513U,
		.block_size = 160U,
		.digest_present = true,
	};
	size_t digest_size;

	image.crc32 = crc32_ieee(test_image, image.size);
	zassert_equal(psa_hash_compute(PSA_ALG_SHA_256, test_image, image.size, image.sha256,
				       sizeof(image.sha256), &digest_size),
		      PSA_SUCCESS);
	zassert_equal(digest_size, KFSW_FWU_SHA256_SIZE);
	return image;
}

ZTEST(services_fwu, test_remote_owner_excludes_local_and_other_principals)
{
	struct kfsw_fwu_image image = digest_image();
	struct kfsw_fwu_handle handle;
	struct kfsw_fwu_handle wrong;
	struct kfsw_fwu_status status;
	unsigned int erases = erase_calls;

	zassert_ok(kfsw_fwu_session_begin(&remote, &image, 10U, &handle, &status));
	wrong = handle;
	wrong.owner.peer++;
	zassert_equal(kfsw_fwu_write(0U, test_image, 1U), -EACCES);
	zassert_equal(kfsw_fwu_verify(), -EACCES);
	zassert_equal(kfsw_fwu_finish(), -EACCES);
	zassert_equal(kfsw_fwu_abort(), -EACCES);
	zassert_equal(kfsw_fwu_begin(1U, 0U), -EBUSY);
	zassert_equal(kfsw_fwu_session_write(&wrong, 0U, test_image, 1U, &status), -EACCES);
	zassert_equal(kfsw_fwu_session_verify(&wrong, &status), -EACCES);
	zassert_equal(kfsw_fwu_session_activate(&wrong, &status), -EACCES);
	zassert_equal(kfsw_fwu_session_abort(&wrong, &status), -EACCES);
	zassert_equal(kfsw_fwu_session_status(&wrong, &status), -EACCES);
	wrong = handle;
	wrong.owner.kind = KFSW_FWU_OWNER_FTP;
	zassert_equal(kfsw_fwu_session_abort(&wrong, &status), -EACCES);
	zassert_equal(erase_calls, erases + 1U);
	zassert_equal(status.received, 0U);
	zassert_ok(kfsw_fwu_session_abort(&handle, &status));
}

ZTEST(services_fwu, test_digest_begin_resumes_only_exact_owner_identity_and_descriptor)
{
	struct kfsw_fwu_image image = digest_image();
	struct kfsw_fwu_image other;
	struct kfsw_fwu_owner owner = remote;
	struct kfsw_fwu_handle handle;
	struct kfsw_fwu_handle replay;
	struct kfsw_fwu_status status;
	unsigned int erases;

	zassert_ok(kfsw_fwu_session_begin(&remote, &image, 11U, &handle, &status));
	zassert_ok(kfsw_fwu_session_write(&handle, 0U, test_image, 17U, &status));
	erases = erase_calls;
	zassert_ok(kfsw_fwu_session_begin(&remote, &image, 11U, &replay, &status));
	zassert_equal(replay.generation, handle.generation);
	zassert_equal(status.received, 17U);
	zassert_equal(status.committed, 0U);
	other = image;
	other.sha256[0] ^= 1U;
	zassert_equal(kfsw_fwu_session_begin(&remote, &other, 11U, &replay, &status), -EBUSY);
	other = image;
	other.size++;
	zassert_equal(kfsw_fwu_session_begin(&remote, &other, 11U, &replay, &status), -EBUSY);
	other = image;
	other.crc32++;
	zassert_equal(kfsw_fwu_session_begin(&remote, &other, 11U, &replay, &status), -EBUSY);
	other = image;
	other.block_size++;
	zassert_equal(kfsw_fwu_session_begin(&remote, &other, 11U, &replay, &status), -EBUSY);
	owner.peer++;
	zassert_equal(kfsw_fwu_session_begin(&owner, &image, 11U, &replay, &status), -EBUSY);
	owner = remote;
	owner.kind = KFSW_FWU_OWNER_FTP;
	other = image;
	other.digest_present = false;
	zassert_equal(kfsw_fwu_session_begin(&owner, &other, 0U, &replay, &status), -EBUSY);
	zassert_equal(kfsw_fwu_session_begin(&remote, &image, 12U, &replay, &status), -EBUSY);
	zassert_equal(erase_calls, erases);
	zassert_ok(kfsw_fwu_session_abort(&handle, &status));
}

ZTEST(services_fwu, test_abort_tombstone_and_stale_handles_cannot_mutate_successor)
{
	struct kfsw_fwu_image image = digest_image();
	struct kfsw_fwu_handle old;
	struct kfsw_fwu_handle next;
	struct kfsw_fwu_status status;
	unsigned int erases;

	zassert_ok(kfsw_fwu_session_begin(&remote, &image, 13U, &old, &status));
	zassert_ok(kfsw_fwu_session_abort(&old, &status));
	zassert_true(status.aborted);
	erases = erase_calls;
	zassert_ok(kfsw_fwu_session_abort(&old, &status));
	zassert_equal(kfsw_fwu_session_begin(&remote, &image, 13U, &next, &status), -ESTALE);
	zassert_equal(erase_calls, erases);
	zassert_ok(kfsw_fwu_session_begin(&remote, &image, 14U, &next, &status));
	zassert_true(next.generation > old.generation);
	erases = erase_calls;
	zassert_equal(kfsw_fwu_session_write(&old, 0U, test_image, 1U, &status), -ESTALE);
	zassert_equal(kfsw_fwu_session_verify(&old, &status), -ESTALE);
	zassert_equal(kfsw_fwu_session_activate(&old, &status), -ESTALE);
	zassert_equal(kfsw_fwu_session_abort(&old, &status), -ESTALE);
	zassert_equal(kfsw_fwu_session_status(&old, &status), -ESTALE);
	zassert_equal(status.handle.generation, next.generation);
	zassert_equal(status.received, 0U);
	zassert_equal(erase_calls, erases);
	zassert_ok(kfsw_fwu_session_abort(&next, &status));
}

ZTEST(services_fwu, test_crc_only_ftp_and_golden_descriptors_do_not_resume)
{
	struct kfsw_fwu_image image = {.size = 10U};
	struct kfsw_fwu_handle handle;
	struct kfsw_fwu_handle ignored;
	struct kfsw_fwu_status status;
	const struct kfsw_fwu_owner owners[] = {
		{.kind = KFSW_FWU_OWNER_FTP, .peer = 16U},
		{.kind = KFSW_FWU_OWNER_LOCAL, .peer = 1U},
		{.kind = KFSW_FWU_OWNER_LITE, .peer = 16U},
	};

	for (size_t i = 0U; i < ARRAY_SIZE(owners); i++) {
		zassert_ok(kfsw_fwu_session_begin(&owners[i], &image, 0U, &handle, &status));
		unsigned int erases = erase_calls;

		zassert_equal(kfsw_fwu_session_begin(&owners[i], &image, 0U, &ignored, &status),
			      -EBUSY);
		zassert_equal(erase_calls, erases);
		zassert_equal(kfsw_fwu_abort(), -EACCES);
		zassert_ok(kfsw_fwu_session_abort(&handle, &status));
	}
}

ZTEST(services_fwu, test_remote_begin_failure_retains_owner_and_cleanup_retry_errors)
{
	struct kfsw_fwu_image image = digest_image();
	struct kfsw_fwu_handle handle;
	struct kfsw_fwu_handle ignored;
	struct kfsw_fwu_status status;
	uint32_t failures;

	erase_error = -EIO;
	zassert_equal(kfsw_fwu_session_begin(&remote, &image, 15U, &handle, &status), -EIO);
	zassert_equal(status.state, KFSW_FWU_FAILED);
	zassert_equal(status.image.size, image.size);
	zassert_equal(status.handle.owner.peer, remote.peer);
	failures = status.failed;
	zassert_equal(kfsw_fwu_session_begin(&remote, &image, 15U, &ignored, &status), -EBUSY);
	zassert_equal(kfsw_fwu_abort(), -EACCES);
	zassert_equal(kfsw_fwu_session_abort(&handle, &status), -EIO);
	zassert_equal(kfsw_fwu_session_abort(&handle, &status), -EIO);
	zassert_equal(status.failed, failures);
	zassert_false(status.aborted);
	erase_error = 0;
	zassert_ok(kfsw_fwu_session_abort(&handle, &status));
	zassert_true(status.aborted);
	zassert_ok(kfsw_fwu_session_begin(&remote, &image, 16U, &handle, &status));
	zassert_ok(kfsw_fwu_session_abort(&handle, &status));
}

ZTEST(services_fwu, test_digest_verifies_exact_stored_bytes_then_explicit_activation)
{
	struct kfsw_fwu_image image = digest_image();
	struct kfsw_fwu_handle handle;
	struct kfsw_fwu_status status;
	uint8_t readback[513];
	uint32_t completed;

	zassert_ok(kfsw_fwu_session_begin(&remote, &image, 17U, &handle, &status));
	completed = status.completed;
	zassert_equal(kfsw_fwu_session_activate(&handle, &status), -EINVAL);
	zassert_equal(kfsw_fwu_session_verify(&handle, &status), -EAGAIN);
	zassert_ok(kfsw_fwu_session_write(&handle, 0U, test_image, image.size, &status));
	zassert_equal(status.state, KFSW_FWU_RECEIVING);
	zassert_false(status.swap_scheduled);
	zassert_equal(kfsw_fwu_session_activate(&handle, &status), -EINVAL);
	bytes_read = 0U;
	zassert_ok(kfsw_fwu_session_verify(&handle, &status));
	zassert_equal(bytes_read, image.size);
	zassert_equal(status.state, KFSW_FWU_VERIFIED);
	zassert_equal(status.committed, image.size);
	zassert_equal(status.completed, completed);
	zassert_false(status.swap_scheduled);
	zassert_ok(read_slot(0U, readback, sizeof(readback)));
	zassert_mem_equal(readback, test_image, sizeof(readback));
	struct kfsw_fwu_handle replay;
	unsigned int erases = erase_calls;

	zassert_ok(kfsw_fwu_session_begin(&remote, &image, 17U, &replay, &status));
	zassert_equal(replay.generation, handle.generation);
	zassert_equal(status.state, KFSW_FWU_VERIFIED);
	zassert_equal(erase_calls, erases);
	zassert_ok(kfsw_fwu_session_verify(&handle, &status));
	zassert_equal(kfsw_fwu_session_write(&handle, 0U, test_image, 1U, &status), -EINVAL);
	zassert_ok(kfsw_fwu_session_activate(&handle, &status));
	zassert_equal(status.state, KFSW_FWU_READY);
	zassert_equal(status.completed, completed + 1U);
	zassert_ok(kfsw_fwu_session_activate(&handle, &status));
	zassert_equal(status.completed, completed + 1U);
	zassert_ok(kfsw_fwu_session_begin(&remote, &image, 17U, &replay, &status));
	zassert_equal(status.state, KFSW_FWU_READY);
	zassert_equal(erase_calls, erases);
	zassert_ok(kfsw_fwu_session_abort(&handle, &status));
}

ZTEST(services_fwu, test_correct_crc_with_wrong_digest_is_rejected_and_owner_retained)
{
	struct kfsw_fwu_image image = digest_image();
	struct kfsw_fwu_handle handle;
	struct kfsw_fwu_status status;

	image.sha256[0] ^= 1U;
	zassert_ok(kfsw_fwu_session_begin(&remote, &image, 18U, &handle, &status));
	zassert_ok(kfsw_fwu_session_write(&handle, 0U, test_image, image.size, &status));
	erase_error = -EIO;
	zassert_equal(kfsw_fwu_session_verify(&handle, &status), -EILSEQ);
	zassert_equal(status.state, KFSW_FWU_FAILED);
	zassert_equal(status.handle.generation, handle.generation);
	zassert_equal(kfsw_fwu_abort(), -EACCES);
	zassert_equal(kfsw_fwu_session_abort(&handle, &status), -EIO);
	erase_error = 0;
	zassert_ok(kfsw_fwu_session_abort(&handle, &status));
}

ZTEST(services_fwu, test_remote_read_and_flush_failures_retain_handle)
{
	struct kfsw_fwu_image image = digest_image();
	struct kfsw_fwu_handle handle;
	struct kfsw_fwu_status status;

	for (uint32_t attempt = 0U; attempt < 2U; attempt++) {
		zassert_ok(
			kfsw_fwu_session_begin(&remote, &image, 19U + attempt, &handle, &status));
		zassert_ok(kfsw_fwu_session_write(&handle, 0U, test_image, image.size, &status));
		if (attempt == 0U) {
			read_error = -EIO;
		} else {
			write_error = -EIO;
		}
		zassert_equal(kfsw_fwu_session_verify(&handle, &status), -EIO);
		zassert_equal(status.state, KFSW_FWU_FAILED);
		zassert_equal(kfsw_fwu_session_activate(&handle, &status), -EINVAL);
		read_error = 0;
		write_error = 0;
		zassert_ok(kfsw_fwu_session_abort(&handle, &status));
	}
}

ZTEST(services_fwu, test_descriptor_validation_does_not_acquire_or_erase)
{
	struct kfsw_fwu_image image = digest_image();
	struct kfsw_fwu_owner owner = remote;
	struct kfsw_fwu_handle handle = {0};
	struct kfsw_fwu_status status;
	unsigned int erases = erase_calls;

	zassert_equal(kfsw_fwu_session_begin(NULL, &image, 1U, &handle, &status), -EINVAL);
	zassert_equal(kfsw_fwu_session_begin(&owner, NULL, 1U, &handle, &status), -EINVAL);
	zassert_equal(kfsw_fwu_session_begin(&owner, &image, 0U, &handle, &status), -EINVAL);
	owner.kind = (enum kfsw_fwu_owner_kind)99;
	zassert_equal(kfsw_fwu_session_begin(&owner, &image, 1U, &handle, &status), -EINVAL);
	owner.kind = KFSW_FWU_OWNER_FTP;
	zassert_equal(kfsw_fwu_session_begin(&owner, &image, 1U, &handle, &status), -EINVAL);
	owner = remote;
	image.block_size = 0U;
	zassert_equal(kfsw_fwu_session_begin(&owner, &image, 1U, &handle, &status), -EINVAL);
	zassert_equal(handle.generation, 0U);
	zassert_equal(erase_calls, erases);
	zassert_equal(kfsw_fwu_session_write(NULL, 0U, test_image, 1U, &status), -EINVAL);
	zassert_equal(kfsw_fwu_session_verify(NULL, &status), -EINVAL);
	zassert_equal(kfsw_fwu_session_activate(NULL, &status), -EINVAL);
	zassert_equal(kfsw_fwu_session_abort(NULL, &status), -EINVAL);
	zassert_equal(kfsw_fwu_session_status(NULL, &status), -EINVAL);
}

ZTEST(services_fwu, test_implicit_local_handle_cannot_mutate_explicit_local_successor)
{
	const struct kfsw_fwu_owner owner = {.kind = KFSW_FWU_OWNER_LOCAL};
	const struct kfsw_fwu_image image = {.size = 8U};
	struct kfsw_fwu_handle successor;
	struct kfsw_fwu_status status;

	zassert_ok(kfsw_fwu_begin(8U, 0U));
	zassert_ok(kfsw_fwu_abort());
	zassert_ok(kfsw_fwu_session_begin(&owner, &image, 0U, &successor, &status));
	zassert_equal(kfsw_fwu_begin(8U, 0U), -EBUSY);
	zassert_equal(kfsw_fwu_write(0U, test_image, 1U), -ESTALE);
	zassert_equal(kfsw_fwu_verify(), -ESTALE);
	zassert_equal(kfsw_fwu_finish(), -ESTALE);
	zassert_equal(kfsw_fwu_abort(), -ESTALE);
	zassert_ok(kfsw_fwu_session_abort(&successor, &status));
}

ZTEST(services_fwu, test_crc_only_abort_tombstone_rejects_explicit_id_replay)
{
	const struct kfsw_fwu_owner owner = {.kind = KFSW_FWU_OWNER_FTP, .peer = 16U};
	const struct kfsw_fwu_image image = {.size = 8U};
	struct kfsw_fwu_handle handle;
	struct kfsw_fwu_handle ignored;
	struct kfsw_fwu_status status;
	unsigned int erases;

	zassert_ok(kfsw_fwu_session_begin(&owner, &image, 123U, &handle, &status));
	zassert_ok(kfsw_fwu_session_abort(&handle, &status));
	erases = erase_calls;
	zassert_equal(kfsw_fwu_session_begin(&owner, &image, 123U, &ignored, &status), -ESTALE);
	zassert_equal(erase_calls, erases);
	zassert_ok(kfsw_fwu_session_begin(&owner, &image, 0U, &handle, &status));
	zassert_ok(kfsw_fwu_session_abort(&handle, &status));
}

ZTEST_SUITE(services_fwu, NULL, fwu_setup, fwu_before, NULL, NULL);

ZTEST(services_fwu_restart, test_restart_is_idle_with_partial_image)
{
	struct kfsw_fwu_status status;
	static uint8_t readback[PARTIAL_IMAGE_SIZE];

	if (!restart_inspection) {
		ztest_test_skip();
	}

	zassert_ok(kfsw_fwu_get_status(&status));
	zassert_equal(status.state, KFSW_FWU_IDLE);
	zassert_equal(status.received, 0U);
	zassert_false(status.swap_scheduled);
	fill_test_image();
	zassert_ok(read_slot(0U, readback, sizeof(readback)));
	zassert_mem_equal(readback, test_image, sizeof(readback));
}

ZTEST_SUITE(services_fwu_restart, NULL, NULL, NULL, NULL, NULL);
