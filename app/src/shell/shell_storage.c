#include <errno.h>
#include <inttypes.h>
#include <string.h>

#include <zephyr/fs/fs.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/util.h>

#include <kfsw/platform/storage.h>

#include "diagnostics/storage_diagnostics.h"

#define KFSW_STORAGE_TEST_PATH KFSW_STORAGE_MOUNT_POINT "/.storage-test"
#define KFSW_STORAGE_PERSISTENCE_PATH KFSW_STORAGE_MOUNT_POINT "/.persistence-test"

static int cmd_storage_info(const struct shell *sh, size_t argc, char **argv)
{
	struct kfsw_storage_info info;
	int result;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	result = kfsw_storage_get_info(&info);
	if (result != 0) {
		shell_error(sh, "Storage info: FAIL (%d)", result);
		return result;
	}

	shell_print(sh, "K-FSW storage");
	shell_print(sh, "filesystem: %s", info.filesystem);
	shell_print(sh, "backend: %s", info.backend);
	shell_print(sh, "mount_point: %s", info.mount_point);
	shell_print(sh, "ready: %s", info.ready ? "yes" : "no");
	shell_print(sh, "total_bytes: %" PRIu64, info.total_bytes);
	shell_print(sh, "free_bytes: %" PRIu64, info.free_bytes);
	return 0;
}

static int storage_basic_test(const struct shell *sh)
{
	static const char initial_value[] = "kfsw-storage-create";
	static const char overwritten_value[] = "kfsw-storage-overwrite";
	int result;

	result = storage_diagnostic_write(KFSW_STORAGE_TEST_PATH, initial_value);
	if (result == 0) {
		result = storage_diagnostic_write(KFSW_STORAGE_TEST_PATH, overwritten_value);
	}
	if (result == 0) {
		result = storage_diagnostic_read(KFSW_STORAGE_TEST_PATH, overwritten_value);
	}
	if (result == 0) {
		result = fs_unlink(KFSW_STORAGE_TEST_PATH);
	}

	if (result != 0) {
		(void)fs_unlink(KFSW_STORAGE_TEST_PATH);
		shell_error(sh, "Storage test: FAIL (%d)", result);
		return result;
	}

	shell_print(sh, "Storage test: PASS");
	return 0;
}

static int storage_persistence_test(const struct shell *sh, const char *operation,
				    const char *value)
{
	int result;

	if ((value[0] == '\0') || (strlen(value) > KFSW_STORAGE_DIAGNOSTIC_VALUE_MAX_SIZE)) {
		shell_error(sh, "Storage persistence value must contain 1..%u characters",
			    KFSW_STORAGE_DIAGNOSTIC_VALUE_MAX_SIZE);
		return -EMSGSIZE;
	}

	if (strcmp(operation, "write") == 0) {
		result = storage_diagnostic_write(KFSW_STORAGE_PERSISTENCE_PATH, value);
		if (result == 0) {
			shell_print(sh, "Storage persistence write: PASS");
			return 0;
		}
	} else if (strcmp(operation, "read") == 0) {
		result = storage_diagnostic_read(KFSW_STORAGE_PERSISTENCE_PATH, value);
		if (result == 0) {
			result = fs_unlink(KFSW_STORAGE_PERSISTENCE_PATH);
		}
		if (result == 0) {
			shell_print(sh, "Storage persistence read: PASS");
			return 0;
		}
	} else {
		shell_error(sh, "Usage: storage test [write|read <value>]");
		return -EINVAL;
	}

	shell_error(sh, "Storage persistence %s: FAIL (%d)", operation, result);
	return result;
}

static int cmd_storage_test(const struct shell *sh, size_t argc, char **argv)
{
	if (!kfsw_storage_is_ready()) {
		shell_error(sh, "Storage test: FAIL (not ready)");
		return -EACCES;
	}

	if (argc == 1U) {
		return storage_basic_test(sh);
	}
	if (argc == 3U) {
		return storage_persistence_test(sh, argv[1], argv[2]);
	}

	shell_error(sh, "Usage: storage test [write|read <value>]");
	return -EINVAL;
}

SHELL_STATIC_SUBCMD_SET_CREATE(
	storage_commands,
	SHELL_CMD_ARG(info, NULL, "Show K-FSW filesystem storage status.", cmd_storage_info, 1,
		      0),
	SHELL_CMD_ARG(test, NULL, "Run storage test: test [write|read <value>].",
		      cmd_storage_test, 1, 2),
	SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(storage, &storage_commands, "K-FSW filesystem storage commands.", NULL);
