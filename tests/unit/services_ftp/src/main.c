#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/fs/fs.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/ztest.h>

#include <kfsw/comms/csp.h>
#include <kfsw/platform/storage.h>
#include <kfsw/services/ftp.h>

#include "ftp_internal.h"

#define STORAGE_PARTITION_NODE DT_CHOSEN(kfsw_storage_partition)
#define TEST_FINAL_PATH KFSW_FTP_STORAGE_ROOT "/build/atomic.bin"
#define TEST_TEMP_PATH TEST_FINAL_PATH ".part"

static void erase_storage_partition(void)
{
	const struct flash_area *area;

	zassert_ok(flash_area_open(DT_FIXED_PARTITION_ID(STORAGE_PARTITION_NODE), &area));
	zassert_ok(flash_area_flatten(area, 0, area->fa_size));
	flash_area_close(area);
}

static void write_file(const char *path, const uint8_t *data, size_t size)
{
	struct fs_file_t file;

	fs_file_t_init(&file);
	zassert_ok(fs_open(&file, path, FS_O_CREATE | FS_O_WRITE | FS_O_TRUNC));
	zassert_equal(fs_write(&file, data, size), (ssize_t)size);
	zassert_ok(fs_sync(&file));
	zassert_ok(fs_close(&file));
}

static void assert_file(const char *path, const uint8_t *expected, size_t expected_size)
{
	struct fs_file_t file;
	uint8_t actual[32];
	ssize_t size;

	zassert_true(expected_size <= sizeof(actual));
	fs_file_t_init(&file);
	zassert_ok(fs_open(&file, path, FS_O_READ));
	size = fs_read(&file, actual, sizeof(actual));
	zassert_equal(size, (ssize_t)expected_size);
	zassert_mem_equal(actual, expected, expected_size);
	zassert_ok(fs_close(&file));
}

static void *ftp_setup(void)
{
	erase_storage_partition();
	zassert_ok(kfsw_storage_init());
	zassert_ok(kfsw_storage_mount());
	zassert_ok(kfsw_ftp_init());
	zassert_ok(kfsw_csp_init());
	zassert_ok(kfsw_csp_start());
	zassert_ok(kfsw_ftp_start());
	return NULL;
}

ZTEST(services_ftp, test_path_validation_and_resolution)
{
	char resolved[KFSW_FTP_FULL_PATH_SIZE];
	char overlong[KFSW_FTP_MAX_PATH_SIZE + 2U];

	zassert_ok(kfsw_ftp_validate_path("telemetry.bin", false));
	zassert_ok(kfsw_ftp_validate_path("/flash/sample.txt", false));
	zassert_ok(kfsw_ftp_validate_path("", true));
	zassert_ok(kfsw_ftp_resolve_path("/flash/sample.txt", false, resolved, sizeof(resolved)));
	zassert_equal(strcmp(resolved, KFSW_FTP_STORAGE_ROOT "/flash/sample.txt"), 0);

	zassert_equal(kfsw_ftp_validate_path("", false), -EINVAL);
	zassert_equal(kfsw_ftp_validate_path("/", false), -EINVAL);
	zassert_equal(kfsw_ftp_validate_path("../params/parameters.dat", false), -EINVAL);
	zassert_equal(kfsw_ftp_validate_path("a/../b", false), -EINVAL);
	zassert_equal(kfsw_ftp_validate_path("a/./b", false), -EINVAL);
	zassert_equal(kfsw_ftp_validate_path("a//b", false), -EINVAL);
	zassert_equal(kfsw_ftp_validate_path("a/", false), -EINVAL);
	zassert_equal(kfsw_ftp_validate_path("a\\b", false), -EINVAL);
	zassert_equal(kfsw_ftp_validate_path("a\x1f"
					     "b",
					     false),
		      -EINVAL);

	memset(overlong, 'a', sizeof(overlong));
	overlong[sizeof(overlong) - 1U] = '\0';
	zassert_equal(kfsw_ftp_validate_path(overlong, false), -ENAMETOOLONG);
}

ZTEST(services_ftp, test_protocol_round_trip_and_portable_encoding)
{
	static const uint8_t data[] = {0x11, 0x22, 0x33};
	const struct kfsw_ftp_message message = {
		.opcode = KFSW_FTP_OP_PUT_DATA,
		.request_id = 0x12345678U,
		.offset = 0x01020304U,
		.total_size = 0x11223344U,
		.crc32 = 0xaabbccddU,
		.data_size = sizeof(data),
		.data = data,
	};
	struct kfsw_ftp_message decoded;
	uint8_t encoded[64];
	size_t encoded_size;

	zassert_ok(kfsw_ftp_protocol_encode(encoded, sizeof(encoded), &message, &encoded_size));
	zassert_equal(encoded_size, KFSW_FTP_PROTOCOL_HEADER_SIZE + sizeof(data));
	zassert_equal(encoded[0], KFSW_FTP_PROTOCOL_VERSION);
	zassert_equal(encoded[1], KFSW_FTP_OP_PUT_DATA);
	zassert_equal(sys_get_be32(&encoded[4]), message.request_id);
	zassert_equal(sys_get_be32(&encoded[8]), message.offset);
	zassert_equal(sys_get_be32(&encoded[12]), message.total_size);
	zassert_equal(sys_get_be32(&encoded[16]), message.crc32);
	zassert_equal(sys_get_be16(&encoded[22]), sizeof(data));

	zassert_ok(kfsw_ftp_protocol_decode(encoded, encoded_size, &decoded));
	zassert_equal(decoded.opcode, message.opcode);
	zassert_equal(decoded.request_id, message.request_id);
	zassert_equal(decoded.offset, message.offset);
	zassert_equal(decoded.total_size, message.total_size);
	zassert_equal(decoded.crc32, message.crc32);
	zassert_equal(decoded.data_size, sizeof(data));
	zassert_mem_equal(decoded.data, data, sizeof(data));
}

ZTEST(services_ftp, test_malformed_protocol_is_rejected)
{
	uint8_t encoded[KFSW_FTP_PROTOCOL_HEADER_SIZE + 2U] = {0};
	struct kfsw_ftp_message decoded;

	encoded[0] = KFSW_FTP_PROTOCOL_VERSION;
	encoded[1] = KFSW_FTP_OP_LIST_REQUEST;
	zassert_equal(kfsw_ftp_protocol_decode(encoded, sizeof(encoded), &decoded), -EMSGSIZE);

	zassert_equal(
		kfsw_ftp_protocol_decode(encoded, KFSW_FTP_PROTOCOL_HEADER_SIZE - 1U, &decoded),
		-EMSGSIZE);
	encoded[0] = KFSW_FTP_PROTOCOL_VERSION + 1U;
	zassert_equal(kfsw_ftp_protocol_decode(encoded, KFSW_FTP_PROTOCOL_HEADER_SIZE, &decoded),
		      -EPROTONOSUPPORT);
	encoded[0] = KFSW_FTP_PROTOCOL_VERSION;
	sys_put_be16(KFSW_FTP_MAX_PATH_SIZE + 1U, &encoded[20]);
	zassert_equal(kfsw_ftp_protocol_decode(encoded, KFSW_FTP_PROTOCOL_HEADER_SIZE, &decoded),
		      -EMSGSIZE);
	sys_put_be16(2U, &encoded[20]);
	encoded[KFSW_FTP_PROTOCOL_HEADER_SIZE] = 'a';
	encoded[KFSW_FTP_PROTOCOL_HEADER_SIZE + 1U] = '\0';
	zassert_equal(kfsw_ftp_protocol_decode(encoded, sizeof(encoded), &decoded), -EBADMSG);
}

ZTEST(services_ftp, test_file_crc_and_missing_file)
{
	static const uint8_t contents[] = "123456789";
	struct kfsw_ftp_workspace workspace;
	uint32_t file_size;
	uint32_t crc32;

	write_file(KFSW_FTP_STORAGE_ROOT "/build/crc.bin", contents, sizeof(contents) - 1U);
	zassert_ok(kfsw_ftp_file_crc(KFSW_FTP_STORAGE_ROOT "/build/crc.bin", &workspace, &file_size,
				     &crc32));
	zassert_equal(file_size, sizeof(contents) - 1U);
	zassert_equal(crc32, 0xcbf43926U);
	zassert_equal(kfsw_ftp_file_crc(KFSW_FTP_STORAGE_ROOT "/build/missing.bin", &workspace,
					&file_size, &crc32),
		      -ENOENT);
}

ZTEST(services_ftp, test_failed_commit_preserves_existing_file)
{
	static const uint8_t original[] = "known-good";
	static const uint8_t partial[] = "incomplete";
	struct fs_dirent entry;

	write_file(TEST_FINAL_PATH, original, sizeof(original) - 1U);
	write_file(TEST_TEMP_PATH, partial, sizeof(partial) - 1U);
	zassert_equal(kfsw_ftp_commit_temporary(TEST_FINAL_PATH, TEST_TEMP_PATH,
						sizeof(partial) - 1U, 0x12345678U, sizeof(partial),
						0x12345678U),
		      -EILSEQ);
	assert_file(TEST_FINAL_PATH, original, sizeof(original) - 1U);
	zassert_equal(fs_stat(TEST_TEMP_PATH, &entry), -ENOENT);
}

ZTEST(services_ftp, test_successful_commit_atomically_replaces_file)
{
	static const uint8_t original[] = "old";
	static const uint8_t replacement[] = "complete replacement";
	struct fs_dirent entry;

	write_file(TEST_FINAL_PATH, original, sizeof(original) - 1U);
	write_file(TEST_TEMP_PATH, replacement, sizeof(replacement) - 1U);
	zassert_ok(kfsw_ftp_commit_temporary(TEST_FINAL_PATH, TEST_TEMP_PATH,
					     sizeof(replacement) - 1U, 0x89abcdefU,
					     sizeof(replacement) - 1U, 0x89abcdefU));
	assert_file(TEST_FINAL_PATH, replacement, sizeof(replacement) - 1U);
	zassert_equal(fs_stat(TEST_TEMP_PATH, &entry), -ENOENT);
}

ZTEST(services_ftp, test_public_argument_validation_and_lifecycle)
{
	struct kfsw_ftp_transfer_result transfer;

	zassert_true(kfsw_ftp_is_started());
	zassert_equal(kfsw_ftp_stat(2U, "file", NULL), -EINVAL);
	zassert_equal(kfsw_ftp_list(2U, "", NULL, NULL), -EINVAL);
	zassert_equal(kfsw_ftp_put(2U, "local", "remote", NULL), -EINVAL);
	zassert_equal(kfsw_ftp_get(2U, "remote", "local", NULL), -EINVAL);
	zassert_equal(kfsw_ftp_put(2U, "../params/parameters.dat", "remote", &transfer), -EINVAL);
	zassert_ok(kfsw_ftp_init());
}

ZTEST_SUITE(services_ftp, NULL, ftp_setup, NULL, NULL, NULL);
