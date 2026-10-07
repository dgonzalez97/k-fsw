#include <errno.h>
#include <string.h>
#include <zephyr/fs/fs.h>
#include <zephyr/ztest.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/crc.h>
#include <kfsw/services/parameter.h>
#include <kfsw/platform/storage.h>
#include <kfsw/services/boot.h>

#define RECORD_PATH KFSW_STORAGE_MOUNT_POINT "/boot-trial.dat"
#define TEMP_PATH KFSW_STORAGE_MOUNT_POINT "/boot-trial.tmp"

static const uint8_t image_a[KFSW_BOOT_IMAGE_ID_SIZE] = {1U};
static const uint8_t image_b[KFSW_BOOT_IMAGE_ID_SIZE] = {2U};

static void check(uint32_t attempts, uint8_t reason, bool valid)
{
	struct kfsw_boot_diagnostics value;

	kfsw_boot_get_diagnostics(&value);
	zassert_equal(value.attempts, attempts);
	zassert_equal(value.revert_reason, reason);
	zassert_equal(value.valid, valid);
}

static void write_bad_file(const char *path)
{
	struct fs_file_t file;

	fs_file_t_init(&file);
	zassert_ok(fs_open(&file, path, FS_O_CREATE | FS_O_WRITE | FS_O_TRUNC));
	zassert_equal(fs_write(&file, "bad", 3U), 3);
	zassert_ok(fs_close(&file));
}

static void test_record_errors(void)
{
	struct fs_file_t file;
	uint8_t record[80];

	zassert_ok(kfsw_boot_record_image(image_a, false, true));
	fs_file_t_init(&file);
	zassert_ok(fs_open(&file, RECORD_PATH, FS_O_READ));
	zassert_equal(fs_read(&file, record, sizeof(record)), sizeof(record));
	zassert_ok(fs_close(&file));
	/* Correct length and magic, but incorrect CRC. */
	record[4] ^= 1U;
	zassert_ok(fs_open(&file, RECORD_PATH, FS_O_WRITE | FS_O_TRUNC));
	zassert_equal(fs_write(&file, record, sizeof(record)), sizeof(record));
	zassert_ok(fs_close(&file));
	zassert_equal(kfsw_boot_record_image(image_a, false, true), -EBADMSG);
	check(UINT32_MAX, UINT8_MAX, false);
	record[4] ^= 1U;
	/* Saturate below the reserved invalid value. */
	sys_put_be32(UINT32_MAX - 1U, &record[36]);
	sys_put_be32(crc32_ieee(record, 76U), &record[76]);
	zassert_ok(fs_open(&file, RECORD_PATH, FS_O_WRITE | FS_O_TRUNC));
	zassert_equal(fs_write(&file, record, sizeof(record)), sizeof(record));
	zassert_ok(fs_close(&file));
	zassert_ok(kfsw_boot_record_image(image_a, false, true));
	check(UINT32_MAX - 1U, KFSW_BOOT_REVERT_NONE, true);
	/* A nonempty directory blocks the temporary path; preserve the good file. */
	zassert_ok(fs_mkdir(TEMP_PATH));
	write_bad_file(TEMP_PATH "/block");
	zassert_true(kfsw_boot_record_image(image_b, true, false) < 0);
	check(UINT32_MAX, UINT8_MAX, false);
	zassert_ok(fs_unlink(TEMP_PATH "/block"));
	zassert_ok(fs_unlink(TEMP_PATH));
	zassert_ok(kfsw_boot_record_image(image_a, false, true));
	check(UINT32_MAX - 1U, KFSW_BOOT_REVERT_NONE, true);
	zassert_ok(kfsw_boot_record_image(image_a, true, false));
	zassert_ok(kfsw_boot_record_image(image_b, false, false));
	check(1U, KFSW_BOOT_REVERT_NONE, true);
	zassert_ok(kfsw_boot_record_image(image_a, true, false));
	check(0U, KFSW_BOOT_REVERT_REPLACED_UNKNOWN, true);
	zassert_ok(fs_unlink(RECORD_PATH));
}

ZTEST(boot_diagnostics, test_write_restart_recover_verify)
{
	struct fs_dirent entry;
	const struct kfsw_param_definition_set *sets[] = {&kfsw_boot_param_definitions};
	struct kfsw_param_value value = {.type = KFSW_PARAM_U32, .scalar.u32 = 0U};

	zassert_ok(kfsw_param_init(sets, ARRAY_SIZE(sets)));
	zassert_ok(kfsw_param_get("boot_attempts", &value));
	zassert_equal(value.scalar.u32, UINT32_MAX);
	zassert_equal(kfsw_param_set("boot_attempts", &value), -EACCES);
	zassert_ok(kfsw_param_get("boot_revert_reason", &value));
	zassert_equal(value.scalar.u8, UINT8_MAX);
	zassert_equal(kfsw_param_set("boot_revert_reason", &value), -EACCES);
	zassert_ok(kfsw_param_get("boot_trial_valid", &value));
	zassert_equal(value.scalar.u8, 0U);
	zassert_equal(kfsw_param_set("boot_trial_valid", &value), -EACCES);

	zassert_equal(kfsw_boot_diagnostics_start(), -ENOTSUP);
	check(UINT32_MAX, UINT8_MAX, false);
	zassert_ok(kfsw_storage_init());
	zassert_ok(kfsw_storage_mount());

	if (fs_stat(RECORD_PATH, &entry) == 0) {
		/* The restart runner invokes a new process against the same flash file. */
		zassert_ok(kfsw_boot_record_image(image_a, false, true));
		check(2U, KFSW_BOOT_REVERT_NONE, true);
		zassert_ok(kfsw_boot_record_image(image_b, true, false));
		check(0U, KFSW_BOOT_REVERT_UNCONFIRMED_REPLACED, true);
		zassert_ok(kfsw_storage_unmount());
		zassert_ok(kfsw_storage_mount());
		zassert_ok(kfsw_boot_record_image(image_b, true, false));
		check(0U, KFSW_BOOT_REVERT_UNCONFIRMED_REPLACED, true);
		zassert_ok(kfsw_boot_record_image(image_b, false, true));
		check(1U, KFSW_BOOT_REVERT_UNCONFIRMED_REPLACED, true);
		zassert_ok(kfsw_boot_record_image(image_b, true, false));
		check(0U, KFSW_BOOT_REVERT_UNCONFIRMED_REPLACED, true);
		printk("BOOT TRIAL RECOVER VERIFIED\n");
	} else {
		zassert_equal(kfsw_boot_record_image(NULL, false, true), -EINVAL);
		zassert_ok(kfsw_storage_unmount());
		zassert_equal(kfsw_boot_record_image(image_a, false, true), -ENODEV);
		check(UINT32_MAX, UINT8_MAX, false);
		zassert_ok(kfsw_storage_mount());
		write_bad_file(RECORD_PATH);
		zassert_equal(kfsw_boot_record_image(image_a, false, true), -EBADMSG);
		check(UINT32_MAX, UINT8_MAX, false);
		zassert_ok(fs_unlink(RECORD_PATH));
		test_record_errors();
		zassert_ok(kfsw_boot_record_image(image_a, false, true));
		check(1U, KFSW_BOOT_REVERT_NONE, true);
		/* An interrupted replacement leaves the previous good snapshot intact. */
		write_bad_file(TEMP_PATH);
		printk("BOOT TRIAL WRITE VERIFIED\n");
	}
	zassert_ok(kfsw_storage_unmount());
}

ZTEST_SUITE(boot_diagnostics, NULL, NULL, NULL, NULL, NULL);
