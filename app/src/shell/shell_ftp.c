#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/shell/shell.h>
#include <zephyr/shell/shell_string_conv.h>
#include <zephyr/sys/util.h>

#include <kfsw/comms/csp.h>
#include <kfsw/services/fbo.h>
#include <kfsw/services/ftp.h>

#include "diagnostics/ftp_diagnostics.h"
#include "shell_command.h"
#include "shell_remote.h"

struct ftp_list_context {
	const struct shell *shell;
	const char *path;
	uint32_t entries;
};

/* A file in one of these directories is something more specific than a file. */
static const struct {
	const char *directory;
	const char *kind;
} file_kinds[] = {
	{KFSW_FTP_BOOT_PATH, "img"},
	{KFSW_FTP_HK_PATH, "hk"},
	{KFSW_FBO_FTP_PATH, "proc"},
};

/* Without a node, ls, stat and mkdir act on this node. */
static int optional_node(const struct shell *sh, bool given, const char *text, uint16_t *node)
{
	struct kfsw_csp_info info;

	if (given) {
		return kfsw_shell_parse_node(sh, text, node);
	}
	kfsw_csp_get_info(&info);
	*node = info.address;
	return 0;
}

static bool is_number(const char *text)
{
	int parse_error = 0;

	(void)shell_strtoul(text, 10, &parse_error);
	return parse_error == 0;
}

/* The kind of an entry in directory, whose first size bytes are the virtual path. */
static const char *entry_kind(enum kfsw_ftp_entry_type type, const char *directory, size_t size)
{
	if (type == KFSW_FTP_ENTRY_DIRECTORY) {
		return "dir";
	}
	while ((size > 0U) && (directory[0] == '/')) {
		directory++;
		size--;
	}
	while ((size > 0U) && (directory[size - 1U] == '/')) {
		size--;
	}
	for (size_t i = 0U; i < ARRAY_SIZE(file_kinds); i++) {
		if ((strlen(file_kinds[i].directory) == size) &&
		    (strncmp(directory, file_kinds[i].directory, size) == 0)) {
			return file_kinds[i].kind;
		}
	}
	return "file";
}

static bool print_ftp_entry(const struct kfsw_ftp_entry *entry, void *context)
{
	struct ftp_list_context *list_context = context;
	const char *path = list_context->path;

	shell_print(list_context->shell, "%-4s %10" PRIu32 " %s",
		    entry_kind(entry->type, path, strlen(path)), entry->size, entry->name);
	list_context->entries++;
	return true;
}

static int print_ftp_error(const struct shell *sh, const char *operation, uint16_t node,
			   const char *path, int result)
{
	if (result == -ENOENT) {
		shell_error(sh, "FTP %s %u %s: not found", operation, node, path);
	} else if (result == -EINVAL || result == -EBADMSG) {
		shell_error(sh, "FTP %s %u %s: invalid path/request (%d)", operation, node, path,
			    result);
	} else if (result == -ENOSPC) {
		shell_error(sh, "FTP %s %u %s: not enough free space", operation, node, path);
	} else if (result == -EBUSY) {
		shell_error(sh, "FTP %s %u %s: server busy", operation, node, path);
	} else if (result == -ENOTSUP) {
		shell_error(sh, "FTP %s %u: transfers need two nodes; use a remote node", operation,
			    node);
	} else if (result == -EACCES) {
		shell_error(sh, "FTP %s %u %s: service or storage not ready", operation, node,
			    path);
	} else if (kfsw_shell_no_answer(result)) {
		char what[16];

		(void)snprintf(what, sizeof(what), "ftp %s", operation);
		(void)kfsw_shell_remote_failed(sh, what, node, result);
	} else {
		shell_error(sh, "FTP %s %u %s: FAIL (%d)", operation, node, path, result);
	}
	return result;
}

static int ftp_list(const struct shell *sh, uint16_t node, const char *path)
{
	struct ftp_list_context context = {.shell = sh, .path = path};
	int result;

	shell_print(sh, "FTP ls %u %s", node, (path[0] == '\0') ? "/" : path);
	result = kfsw_ftp_list(node, path, print_ftp_entry, &context);
	if (result != 0) {
		return print_ftp_error(sh, "ls", node, path, result);
	}
	shell_print(sh, "entries: %" PRIu32, context.entries);
	return 0;
}

static int ftp_stat(const struct shell *sh, uint16_t node, const char *path)
{
	const char *name = strrchr(path, '/');
	const size_t directory_size = (name == NULL) ? 0U : (size_t)(name - path);
	struct kfsw_ftp_stat info;
	int result = kfsw_ftp_stat(node, path, &info);

	if (result != 0) {
		return print_ftp_error(sh, "stat", node, path, result);
	}
	shell_print(sh, "FTP stat %u %s", node, path);
	shell_print(sh, "type: %s", entry_kind(info.type, path, directory_size));
	shell_print(sh, "bytes: %" PRIu32, info.size);
	shell_print(sh, "crc32: %08" PRIx32, info.crc32);
	return 0;
}

static int ftp_mkdir(const struct shell *sh, uint16_t node, const char *path)
{
	int result = kfsw_ftp_mkdir(node, path);

	if (result != 0) {
		return print_ftp_error(sh, "mkdir", node, path, result);
	}
	shell_print(sh, "FTP mkdir %u %s: PASS", node, path);
	return 0;
}

static int ftp_transfer(const struct shell *sh, bool upload, uint16_t node, const char *source,
			const char *destination)
{
	struct kfsw_ftp_transfer_result info;
	const char *operation = upload ? "put" : "get";
	int result;

	result = upload ? kfsw_ftp_put(node, source, destination, &info)
			: kfsw_ftp_get(node, source, destination, &info);
	if (result != 0) {
		return print_ftp_error(sh, operation, node, upload ? destination : source, result);
	}

	const uint64_t throughput =
		(info.duration_ms == 0U) ? 0U : ((uint64_t)info.bytes * 1000U) / info.duration_ms;

	shell_print(sh, "FTP %s %u %s -> %s: PASS", operation, node, source, destination);
	shell_print(sh, "bytes: %" PRIu32, info.bytes);
	shell_print(sh, "crc32: %08" PRIx32, info.crc32);
	shell_print(sh, "duration_ms: %" PRIu32, info.duration_ms);
	shell_print(sh, "throughput_Bps: %" PRIu64, throughput);
	return 0;
}

static int cmd_ftp_ls(const struct shell *sh, size_t argc, char **argv)
{
	const bool given = (argc == 3U) || ((argc == 2U) && is_number(argv[1]));
	uint16_t node;
	int result = optional_node(sh, given, argv[1], &node);

	if (result != 0) {
		return result;
	}
	return ftp_list(sh, node, (argc > (given ? 2U : 1U)) ? argv[argc - 1U] : "");
}

static int cmd_ftp_stat(const struct shell *sh, size_t argc, char **argv)
{
	uint16_t node;
	int result = optional_node(sh, argc == 3U, argv[1], &node);

	return (result == 0) ? ftp_stat(sh, node, argv[argc - 1U]) : result;
}

static int cmd_ftp_mkdir(const struct shell *sh, size_t argc, char **argv)
{
	uint16_t node;
	int result = optional_node(sh, argc == 3U, argv[1], &node);

	return (result == 0) ? ftp_mkdir(sh, node, argv[argc - 1U]) : result;
}

static int cmd_ftp_get(const struct shell *sh, size_t argc, char **argv)
{
	uint16_t node;
	int result;

	ARG_UNUSED(argc);
	result = kfsw_shell_parse_node(sh, argv[1], &node);
	return (result == 0) ? ftp_transfer(sh, false, node, argv[2], argv[3]) : result;
}

static int cmd_ftp_put(const struct shell *sh, size_t argc, char **argv)
{
	uint16_t node;
	int result;

	ARG_UNUSED(argc);
	result = kfsw_shell_parse_node(sh, argv[1], &node);
	return (result == 0) ? ftp_transfer(sh, true, node, argv[2], argv[3]) : result;
}

static int cmd_ftp_generate(const struct shell *sh, size_t argc, char **argv)
{
	unsigned long size;
	uint32_t crc32;
	int parse_error = 0;
	int result;

	ARG_UNUSED(argc);
	size = shell_strtoul(argv[2], 10, &parse_error);
	if ((parse_error != 0) || (size > 32768U)) {
		shell_error(sh, "FTP fixture size must be in range 0..32768");
		return -EINVAL;
	}
	result = ftp_diagnostic_generate(argv[1], (uint32_t)size, &crc32);
	if (result != 0) {
		shell_error(sh, "FTP generate %s: FAIL (%d)", argv[1], result);
		return result;
	}
	shell_print(sh, "FTP generate %s: PASS", argv[1]);
	shell_print(sh, "bytes: %lu", size);
	shell_print(sh, "crc32: %08" PRIx32, crc32);
	return 0;
}

static int cmd_ftp_verify(const struct shell *sh, size_t argc, char **argv)
{
	int result;

	ARG_UNUSED(argc);
	result = ftp_diagnostic_compare(argv[1], argv[2]);
	if (result != 0) {
		shell_error(sh, "FTP verify %s %s: FAIL (%d)", argv[1], argv[2], result);
		return result;
	}
	shell_print(sh, "FTP verify %s %s: PASS", argv[1], argv[2]);
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(
	ftp_commands,
	SHELL_CMD_ARG(generate, NULL,
		      "Create deterministic local data: generate <path> <bytes 0..32768>.",
		      cmd_ftp_generate, 3, 0),
	SHELL_CMD_ARG(get, NULL, "Download from a remote node: get <node> <remote> <local>.",
		      cmd_ftp_get, 4, 0),
	SHELL_CMD_ARG(ls, NULL, "List a directory: ls [node] [path].", cmd_ftp_ls, 1, 2),
	SHELL_CMD_ARG(mkdir, NULL, "Create a directory: mkdir [node] <path>.", cmd_ftp_mkdir, 2,
		      1),
	SHELL_CMD_ARG(put, NULL, "Upload to a remote node: put <node> <local> <remote>.",
		      cmd_ftp_put, 4, 0),
	SHELL_CMD_ARG(stat, NULL, "Show metadata: stat [node] <path>.", cmd_ftp_stat, 2, 1),
	SHELL_CMD_ARG(verify, NULL, "Compare two local files: verify <first> <second>.",
		      cmd_ftp_verify, 3, 0),
	SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(ftp, &ftp_commands, "File transfer. Without a node, this node.", NULL);
