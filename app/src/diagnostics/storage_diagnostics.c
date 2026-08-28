#include "diagnostics/storage_diagnostics.h"

#include <errno.h>
#include <string.h>

#include <zephyr/fs/fs.h>

int storage_diagnostic_write(const char *path, const char *value)
{
	struct fs_file_t file;
	const size_t value_size = strlen(value);
	ssize_t written;
	int close_result;
	int result;

	fs_file_t_init(&file);
	result = fs_open(&file, path, FS_O_CREATE | FS_O_WRITE | FS_O_TRUNC);
	if (result != 0) {
		return result;
	}

	written = fs_write(&file, value, value_size);
	if (written == (ssize_t)value_size) {
		result = fs_sync(&file);
	} else {
		result = (written < 0) ? (int)written : -EIO;
	}

	close_result = fs_close(&file);
	return (result != 0) ? result : close_result;
}

int storage_diagnostic_read(const char *path, const char *expected)
{
	struct fs_file_t file;
	char value[KFSW_STORAGE_DIAGNOSTIC_VALUE_MAX_SIZE + 1U];
	const size_t expected_size = strlen(expected);
	ssize_t bytes_read;
	int close_result;
	int result = 0;

	if (expected_size > KFSW_STORAGE_DIAGNOSTIC_VALUE_MAX_SIZE) {
		return -EMSGSIZE;
	}

	fs_file_t_init(&file);
	result = fs_open(&file, path, FS_O_READ);
	if (result != 0) {
		return result;
	}

	bytes_read = fs_read(&file, value, sizeof(value));
	if (bytes_read < 0) {
		result = (int)bytes_read;
	} else if ((size_t)bytes_read != expected_size) {
		result = -EIO;
	} else {
		value[bytes_read] = '\0';
		if (memcmp(value, expected, expected_size) != 0) {
			result = -EIO;
		}
	}

	close_result = fs_close(&file);
	return (result != 0) ? result : close_result;
}
