#include <string.h>

#include <zephyr/devicetree.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>
#include <zephyr/sys/crc.h>
#include <zephyr/ztest.h>

#include <kfsw/services/fwu.h>
#include <kfsw/services/scrub.h>

#define SLOT0_ID DT_FIXED_PARTITION_ID(DT_NODELABEL(slot0_partition))
#define SLOT1_ID DT_FIXED_PARTITION_ID(DT_NODELABEL(slot1_partition))

/* The bootloader's image header on flash, little endian: the magic at 0, the
 * header size at 8, the image size at 12. The service reads the same bytes.
 */
#define IMAGE_HEADER_MAGIC 0x96f3b83dUL
#define IMAGE_HEADER_BYTES 32U
#define BODY_SIZE 512U
/* The simulated part writes in 8-byte blocks, as the real one does. */
#define WRITE_BLOCK 8U

static void write_image(uint8_t area_id, uint32_t offset, uint8_t fill)
{
	const struct flash_area *area;
	uint8_t header[IMAGE_HEADER_BYTES] = {0};
	uint8_t body[BODY_SIZE];

	sys_put_le32(IMAGE_HEADER_MAGIC, header);
	sys_put_le16(IMAGE_HEADER_BYTES, &header[8]);
	sys_put_le32(BODY_SIZE, &header[12]);
	memset(body, fill, sizeof(body));
	zassert_ok(flash_area_open(area_id, &area));
	zassert_ok(flash_area_flatten(area, 0, area->fa_size));
	zassert_ok(flash_area_write(area, offset, header, sizeof(header)));
	zassert_ok(flash_area_write(area, offset + sizeof(header), body, sizeof(body)));
	flash_area_close(area);
}

/* Flash goes from one to zero without an erase, which is what a decayed bit
 * does to a programmed image. The write covers a whole block because that is
 * the smallest the part accepts.
 */
static void clear_one_block(uint8_t area_id, uint32_t offset)
{
	const struct flash_area *area;
	uint8_t block[WRITE_BLOCK] = {0};

	zassert_ok(flash_area_open(area_id, &area));
	zassert_ok(flash_area_write(area, ROUND_DOWN(offset, WRITE_BLOCK), block, sizeof(block)));
	flash_area_close(area);
}

static void erase_region(uint8_t area_id)
{
	const struct flash_area *area;

	zassert_ok(flash_area_open(area_id, &area));
	zassert_ok(flash_area_flatten(area, 0, area->fa_size));
	flash_area_close(area);
}

static struct kfsw_scrub_result sweep(enum kfsw_scrub_region region)
{
	struct kfsw_scrub_result result;

	zassert_ok(kfsw_scrub_sweep_region(region));
	zassert_ok(kfsw_scrub_get_result(region, &result));
	return result;
}

/* The references are what survives a reset in a real node, so each case starts
 * without them.
 */
extern uint32_t kfsw_scrub_references[KFSW_SCRUB_REGIONS];

static void reset_regions(void *fixture)
{
	ARG_UNUSED(fixture);

	memset(kfsw_scrub_references, 0, sizeof(uint32_t) * KFSW_SCRUB_REGIONS);
	erase_region(SLOT0_ID);
	erase_region(SLOT1_ID);
	(void)kfsw_scrub_sweep();
}

ZTEST(scrub, test_erased_region_is_empty_not_ok)
{
	struct kfsw_scrub_result result = sweep(KFSW_SCRUB_SLOT0);

	zassert_equal(result.state, KFSW_SCRUB_EMPTY, "state %u", result.state);
	zassert_equal(result.bytes, 0U);
}

ZTEST(scrub, test_an_image_without_a_reference_is_unknown)
{
	struct kfsw_scrub_result result;

	write_image(SLOT0_ID, 0U, 0xA5U);
	result = sweep(KFSW_SCRUB_SLOT0);

	zassert_equal(result.state, KFSW_SCRUB_UNKNOWN, "state %u", result.state);
	/* The header says how much to read, so the erased tail is not swept. */
	zassert_equal(result.bytes, IMAGE_HEADER_BYTES + BODY_SIZE, "bytes %u", result.bytes);
	zassert_not_equal(result.measured, 0U);
	zassert_equal(result.reference, 0U);
}

ZTEST(scrub, test_learning_makes_the_next_sweep_ok)
{
	struct kfsw_scrub_result result;

	write_image(SLOT0_ID, 0U, 0xA5U);
	zassert_ok(kfsw_scrub_learn(KFSW_SCRUB_SLOT0));

	result = sweep(KFSW_SCRUB_SLOT0);
	zassert_equal(result.state, KFSW_SCRUB_OK, "state %u", result.state);
	zassert_equal(result.reference, result.measured);
}

ZTEST(scrub, test_a_changed_byte_is_a_mismatch_counted_once)
{
	struct kfsw_scrub_status before;
	struct kfsw_scrub_status after;
	struct kfsw_scrub_result result;

	write_image(SLOT0_ID, 0U, 0xA5U);
	zassert_ok(kfsw_scrub_learn(KFSW_SCRUB_SLOT0));
	kfsw_scrub_get_status(&before);

	clear_one_block(SLOT0_ID, IMAGE_HEADER_BYTES + WRITE_BLOCK);
	result = sweep(KFSW_SCRUB_SLOT0);
	zassert_equal(result.state, KFSW_SCRUB_MISMATCH, "state %u", result.state);
	zassert_not_equal(result.measured, result.reference);

	/* Latched: the same decayed image is one finding, not one per sweep. */
	result = sweep(KFSW_SCRUB_SLOT0);
	zassert_equal(result.state, KFSW_SCRUB_MISMATCH);
	kfsw_scrub_get_status(&after);
	zassert_equal(after.mismatches, before.mismatches + 1U, "counted %u",
		      after.mismatches - before.mismatches);
}

ZTEST(scrub, test_the_secondary_image_is_read_where_fwu_writes_it)
{
	struct kfsw_scrub_result result;

	zassert_not_equal(kfsw_fwu_slot_write_offset(), 0U,
			  "this test needs the offset FWU writes at");

	write_image(SLOT1_ID, 0U, 0x5AU);
	result = sweep(KFSW_SCRUB_SLOT1);
	zassert_equal(result.state, KFSW_SCRUB_EMPTY,
		      "an image at the partition start is not "
		      "where the bootloader looks");

	write_image(SLOT1_ID, kfsw_fwu_slot_write_offset(), 0x5AU);
	result = sweep(KFSW_SCRUB_SLOT1);
	zassert_equal(result.state, KFSW_SCRUB_UNKNOWN, "state %u", result.state);
}

ZTEST(scrub, test_a_region_this_build_lacks_reports_absent)
{
	struct kfsw_scrub_result result;

	zassert_equal(kfsw_scrub_sweep_region(KFSW_SCRUB_GOLDEN), -ENODEV);
	zassert_ok(kfsw_scrub_get_result(KFSW_SCRUB_GOLDEN, &result));
	zassert_equal(result.state, KFSW_SCRUB_ABSENT, "state %u", result.state);
	zassert_equal(kfsw_scrub_learn(KFSW_SCRUB_GOLDEN), -ENODEV);
}

ZTEST(scrub, test_an_unknown_region_is_refused)
{
	struct kfsw_scrub_result result;

	zassert_equal(kfsw_scrub_sweep_region(KFSW_SCRUB_REGIONS), -EINVAL);
	zassert_equal(kfsw_scrub_get_result(KFSW_SCRUB_REGIONS, &result), -EINVAL);
}

ZTEST(scrub, test_a_sweep_counts_and_timestamps_itself)
{
	struct kfsw_scrub_status before;
	struct kfsw_scrub_status after;

	kfsw_scrub_get_status(&before);
	zassert_true(kfsw_scrub_sweep() > 0);
	kfsw_scrub_get_status(&after);

	zassert_equal(after.sweeps, before.sweeps + 1U);
	zassert_equal(after.regions, 2U, "slot0 and slot1, not golden");
}

ZTEST_SUITE(scrub, NULL, NULL, reset_regions, NULL, NULL);
