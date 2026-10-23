#include <cmdline.h>
#include <errno.h>
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
static int write_error;
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
	return erase_error != 0 ? erase_error : __real_flash_area_flatten(area, offset, size);
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
	(void)kfsw_fwu_abort();
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

ZTEST(services_fwu, test_finish_twice_is_rejected)
{
	uint32_t crc = crc32_ieee(test_image, 1024U);

	zassert_ok(transfer_whole_image(1024U, crc, 256U));
	zassert_equal(kfsw_fwu_finish(), -EINVAL,
		      "a completed transfer must not be offered a second time");
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
