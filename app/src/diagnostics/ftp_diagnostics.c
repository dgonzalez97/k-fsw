#include "diagnostics/ftp_diagnostics.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/fs/fs.h>
#include <zephyr/sys/crc.h>
#include <zephyr/sys/util.h>

#include <kfsw/services/ftp.h>

static int resolve_ftp_local_path(const char *path, char *full_path, size_t full_path_size)
{
	const char *relative = (path[0] == '/') ? &path[1] : path;
	int result = kfsw_ftp_validate_path(path, false);
	int length;

	if (result != 0) {
		return result;
	}
	length = snprintf(full_path, full_path_size, "%s/%s", KFSW_FTP_STORAGE_ROOT, relative);
	return ((length >= 0) && ((size_t)length < full_path_size)) ? 0 : -ENAMETOOLONG;
}

int ftp_diagnostic_generate(const char *path, uint32_t size, uint32_t *crc32)
{
	struct fs_file_t file;
	uint8_t data[64];
	char full_path[128];
	uint32_t offset = 0U;
	uint32_t crc = 0U;
	int close_result;
	int result;

	result = resolve_ftp_local_path(path, full_path, sizeof(full_path));

	fs_file_t_init(&file);
	if (result == 0) {
		result = fs_open(&file, full_path, FS_O_CREATE | FS_O_WRITE | FS_O_TRUNC);
	}
	while ((result == 0) && (offset < size)) {
		const size_t chunk_size = MIN(sizeof(data), size - offset);

		for (size_t index = 0U; index < chunk_size; index++) {
			data[index] = (uint8_t)(((offset + index) * 31U + 7U) & UINT8_MAX);
		}
		if (fs_write(&file, data, chunk_size) != (ssize_t)chunk_size) {
			result = -EIO;
			break;
		}
		crc = crc32_ieee_update(crc, data, chunk_size);
		offset += (uint32_t)chunk_size;
	}
	if (result == 0) {
		result = fs_sync(&file);
	}
	if (file.mp != NULL) {
		close_result = fs_close(&file);
		if (result == 0) {
			result = close_result;
		}
	}
	if (result == 0) {
		*crc32 = crc;
	}
	return result;
}

int ftp_diagnostic_compare(const char *first_path, const char *second_path)
{
	struct fs_file_t first;
	struct fs_file_t second;
	uint8_t first_data[64];
	uint8_t second_data[64];
	char first_full_path[128];
	char second_full_path[128];
	int result;

	result = resolve_ftp_local_path(first_path, first_full_path, sizeof(first_full_path));
	if (result == 0) {
		result = resolve_ftp_local_path(second_path, second_full_path,
						sizeof(second_full_path));
	}
	fs_file_t_init(&first);
	fs_file_t_init(&second);
	if (result == 0) {
		result = fs_open(&first, first_full_path, FS_O_READ);
	}
	if (result == 0) {
		result = fs_open(&second, second_full_path, FS_O_READ);
	}
	while (result == 0) {
		const ssize_t first_size = fs_read(&first, first_data, sizeof(first_data));
		const ssize_t second_size = fs_read(&second, second_data, sizeof(second_data));

		if ((first_size < 0) || (second_size < 0)) {
			result = (first_size < 0) ? (int)first_size : (int)second_size;
			break;
		}
		if ((first_size != second_size) ||
		    ((first_size > 0) &&
		     (memcmp(first_data, second_data, (size_t)first_size) != 0))) {
			result = -EILSEQ;
			break;
		}
		if (first_size == 0) {
			break;
		}
	}
	if (second.mp != NULL) {
		const int close_result = fs_close(&second);

		if (result == 0) {
			result = close_result;
		}
	}
	if (first.mp != NULL) {
		const int close_result = fs_close(&first);

		if (result == 0) {
			result = close_result;
		}
	}
	return result;
}
