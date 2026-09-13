#include <errno.h>
#include <string.h>

#include <bootutil/image.h>
#include <zephyr/fs/fs.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/crc.h>
#include <zephyr/sys/util.h>
#include <zephyr/ztest.h>

#include <kfsw/services/fwu.h>

#define PRIMARY "/kfsw/boot/firmware_1.bin"
#define SECONDARY "/kfsw/boot/firmware_2.bin"
#define HEADER_SIZE 64U
#define BODY_SIZE 401U
#define PROTECTED_SIZE 12U
#define TLV_SIZE 40U
#define IMAGE_SIZE (HEADER_SIZE + BODY_SIZE + PROTECTED_SIZE + TLV_SIZE)

static uint8_t image[ROUND_UP(IMAGE_SIZE, 8U)];

static void *setup(void)
{
	zassert_ok(kfsw_fwu_files_mount());
	return NULL;
}

static void before(void *fixture)
{
	const struct flash_area *area;
	const uint32_t protected_offset = HEADER_SIZE + BODY_SIZE;
	const uint32_t tlv_offset = protected_offset + PROTECTED_SIZE;

	ARG_UNUSED(fixture);
	zassert_ok(kfsw_fwu_abort());
	zassert_ok(flash_area_open(DT_FIXED_PARTITION_ID(DT_NODELABEL(slot0_partition)), &area));
	zassert_ok(flash_area_flatten(area, 0, area->fa_size));
	flash_area_close(area);

	memset(image, 0xff, sizeof(image));
	memset(image, 0, HEADER_SIZE);
	sys_put_le32(IMAGE_MAGIC, &image[0]);
	sys_put_le16(HEADER_SIZE, &image[8]);
	sys_put_le16(PROTECTED_SIZE, &image[10]);
	sys_put_le32(BODY_SIZE, &image[12]);
	for (size_t i = HEADER_SIZE; i < IMAGE_SIZE; i++) {
		image[i] = (uint8_t)(i * 31U);
	}
	sys_put_le16(IMAGE_TLV_PROT_INFO_MAGIC, &image[protected_offset]);
	sys_put_le16(PROTECTED_SIZE, &image[protected_offset + 2U]);
	sys_put_le16(IMAGE_TLV_SEC_CNT, &image[protected_offset + 4U]);
	sys_put_le16(4U, &image[protected_offset + 6U]);
	sys_put_le16(IMAGE_TLV_INFO_MAGIC, &image[tlv_offset]);
	sys_put_le16(TLV_SIZE, &image[tlv_offset + 2U]);
	sys_put_le16(IMAGE_TLV_SHA256, &image[tlv_offset + 4U]);
	sys_put_le16(32U, &image[tlv_offset + 6U]);
}

static void write_raw(uint8_t slot)
{
	const struct flash_area *area;

	zassert_ok(flash_area_open(slot, &area));
	zassert_ok(flash_area_write(area, 0, image, sizeof(image)));
	flash_area_close(area);
}

static void upload(void)
{
	zassert_ok(kfsw_fwu_begin(IMAGE_SIZE, crc32_ieee(image, IMAGE_SIZE)));
	zassert_ok(kfsw_fwu_write(0, image, IMAGE_SIZE));
}

static void check_contents(const char *path)
{
	struct fs_file_t file;
	struct fs_dirent entry;
	uint8_t readback[IMAGE_SIZE];

	zassert_ok(fs_stat(path, &entry));
	zassert_equal(entry.size, IMAGE_SIZE);
	fs_file_t_init(&file);
	zassert_ok(fs_open(&file, path, FS_O_READ));
	zassert_equal(fs_read(&file, readback, sizeof(readback)), IMAGE_SIZE);
	zassert_mem_equal(readback, image, IMAGE_SIZE);
	zassert_equal(fs_read(&file, readback, 1U), 0);
	zassert_equal(fs_tell(&file), IMAGE_SIZE);
	zassert_ok(fs_seek(&file, -7, FS_SEEK_END));
	zassert_equal(fs_read(&file, readback, sizeof(readback)), 7);
	zassert_mem_equal(readback, &image[IMAGE_SIZE - 7U], 7);
	zassert_equal(fs_seek(&file, 1, FS_SEEK_END), -EINVAL);
	zassert_equal(fs_seek(&file, -1, FS_SEEK_SET), -EINVAL);
	zassert_equal(fs_seek(&file, (off_t)(INT64_MAX >> (64 - sizeof(off_t) * 8)), FS_SEEK_CUR),
		      -EINVAL);
	zassert_equal(fs_tell(&file), IMAGE_SIZE);
	zassert_ok(fs_close(&file));
}

ZTEST(services_fwu_files, test_primary_has_exact_image_extent)
{
	write_raw(DT_FIXED_PARTITION_ID(DT_NODELABEL(slot0_partition)));
	check_contents(PRIMARY);
}

ZTEST(services_fwu_files, test_verified_upload_is_readable_before_scheduling)
{
	struct fs_dirent entry;
	struct kfsw_fwu_status status;

	upload();
	zassert_equal(fs_stat(SECONDARY, &entry), -EBUSY);
	zassert_ok(kfsw_fwu_verify());
	zassert_ok(kfsw_fwu_get_status(&status));
	zassert_equal(status.state, KFSW_FWU_VERIFIED);
	zassert_false(status.swap_scheduled);
	check_contents(SECONDARY);
	zassert_ok(kfsw_fwu_finish());
	check_contents(SECONDARY);
}

ZTEST(services_fwu_files, test_previous_image_at_slot_base_is_readable)
{
	write_raw(DT_FIXED_PARTITION_ID(DT_NODELABEL(slot1_partition)));
	check_contents(SECONDARY);
}

ZTEST(services_fwu_files, test_reader_blocks_erase_until_last_close)
{
	struct fs_file_t first;
	struct fs_file_t second;

	upload();
	zassert_ok(kfsw_fwu_verify());
	fs_file_t_init(&first);
	fs_file_t_init(&second);
	zassert_ok(fs_open(&first, SECONDARY, FS_O_READ));
	zassert_ok(fs_open(&second, SECONDARY, FS_O_READ));
	zassert_equal(kfsw_fwu_begin(1U, 0U), -EBUSY);
	zassert_equal(kfsw_fwu_abort(), -EBUSY);
	zassert_equal(kfsw_fwu_finish(), -EBUSY);
	zassert_ok(fs_close(&first));
	zassert_equal(kfsw_fwu_abort(), -EBUSY);
	zassert_ok(fs_close(&second));
	zassert_ok(kfsw_fwu_abort());
}

ZTEST(services_fwu_files, test_bad_bounds_are_not_files)
{
	struct fs_dirent entry;

	sys_put_le32(UINT32_MAX, &image[12]);
	write_raw(DT_FIXED_PARTITION_ID(DT_NODELABEL(slot0_partition)));
	zassert_equal(fs_stat(PRIMARY, &entry), -EBADMSG);
}

ZTEST(services_fwu_files, test_bad_tlv_record_is_not_a_file)
{
	struct fs_dirent entry;

	sys_put_le16(UINT16_MAX, &image[HEADER_SIZE + BODY_SIZE + PROTECTED_SIZE + 6U]);
	write_raw(DT_FIXED_PARTITION_ID(DT_NODELABEL(slot0_partition)));
	zassert_equal(fs_stat(PRIMARY, &entry), -EBADMSG);
}

ZTEST(services_fwu_files, test_empty_slot_and_unknown_name_are_absent)
{
	struct fs_dirent entry;

	zassert_equal(fs_stat(PRIMARY, &entry), -ENOENT);
	zassert_equal(fs_stat("/kfsw/boot/firmware_3.bin", &entry), -ENOENT);
}

ZTEST(services_fwu_files, test_directory_lists_only_complete_images)
{
	struct fs_dir_t dir;
	struct fs_dirent entry;

	write_raw(DT_FIXED_PARTITION_ID(DT_NODELABEL(slot0_partition)));
	upload();
	fs_dir_t_init(&dir);
	zassert_ok(fs_opendir(&dir, "/kfsw/boot"));
	zassert_ok(fs_readdir(&dir, &entry));
	zassert_str_equal(entry.name, "firmware_1.bin");
	zassert_equal(entry.size, IMAGE_SIZE);
	zassert_ok(fs_readdir(&dir, &entry));
	zassert_equal(entry.name[0], '\0');
	zassert_ok(fs_closedir(&dir));
}

ZTEST(services_fwu_files, test_slot_files_reject_writes)
{
	struct fs_file_t file;

	write_raw(DT_FIXED_PARTITION_ID(DT_NODELABEL(slot0_partition)));
	fs_file_t_init(&file);
	zassert_equal(fs_open(&file, PRIMARY, FS_O_WRITE), -EROFS);
	zassert_equal(fs_open(&file, PRIMARY, FS_O_CREATE | FS_O_WRITE), -EROFS);
	check_contents(PRIMARY);
}

ZTEST_SUITE(services_fwu_files, NULL, setup, before, NULL, NULL);
