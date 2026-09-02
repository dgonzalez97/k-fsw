#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/shell/shell.h>
#include <zephyr/shell/shell_string_conv.h>
#include <zephyr/sys/util.h>

#include <kfsw/services/ftp.h>

#include "diagnostics/ftp_diagnostics.h"

struct ftp_list_context {
	const struct shell *shell;
	uint32_t entries;
};

static int parse_ftp_node(const struct shell *sh, const char *text, uint16_t *node)
{
	unsigned long parsed;
	int parse_error = 0;

	parsed = shell_strtoul(text, 10, &parse_error);
	if ((parse_error != 0) || (parsed > 16383U)) {
		shell_error(sh, "CSP node must be in range 0..16383");
		return -EINVAL;
	}
	*node = (uint16_t)parsed;
	return 0;
}

static bool print_ftp_entry(const struct kfsw_ftp_entry *entry, void *context)
{
	struct ftp_list_context *list_context = context;
	const char type = (entry->type == KFSW_FTP_ENTRY_DIRECTORY) ? 'd' : 'f';

	shell_print(list_context->shell, "%c %10" PRIu32 " %s", type, entry->size, entry->name);
	list_context->entries++;
	return true;
}

static int print_ftp_error(const struct shell *sh, const char *operation, uint16_t node,
			   const char *path, int result)
{
	if (result == -ENOENT) {
		shell_error(sh, "FTP %s node=%u path=%s: not found", operation, node, path);
	} else if (result == -EINVAL || result == -EBADMSG) {
		shell_error(sh, "FTP %s node=%u path=%s: invalid path/request (%d)", operation,
			    node, path, result);
	} else if (result == -EBUSY) {
		shell_error(sh, "FTP %s node=%u path=%s: server busy", operation, node, path);
	} else if (result == -ENOTSUP) {
		shell_error(sh, "FTP %s node=%u: transfers need two nodes; use a remote node",
			    operation, node);
	} else if (result == -EACCES) {
		shell_error(sh, "FTP %s node=%u path=%s: service or storage not ready", operation,
			    node, path);
	} else {
		shell_error(sh, "FTP %s node=%u path=%s: FAIL (%d)", operation, node, path, result);
	}
	return result;
}

static int ftp_list(const struct shell *sh, uint16_t node, const char *path)
{
	struct ftp_list_context context = {.shell = sh};
	int result;

	shell_print(sh, "FTP list node=%u path=%s", node, (path[0] == '\0') ? "/" : path);
	result = kfsw_ftp_list(node, path, print_ftp_entry, &context);
	if (result != 0) {
		return print_ftp_error(sh, "list", node, path, result);
	}
	shell_print(sh, "FTP list: PASS entries=%" PRIu32, context.entries);
	return 0;
}

static int ftp_stat(const struct shell *sh, uint16_t node, const char *path)
{
	struct kfsw_ftp_stat info;
	int result = kfsw_ftp_stat(node, path, &info);

	if (result != 0) {
		return print_ftp_error(sh, "stat", node, path, result);
	}
	shell_print(sh, "FTP stat node=%u path=%s type=%s bytes=%" PRIu32 " crc32=%08" PRIx32, node,
		    path, (info.type == KFSW_FTP_ENTRY_DIRECTORY) ? "directory" : "file", info.size,
		    info.crc32);
	return 0;
}

static int ftp_mkdir(const struct shell *sh, uint16_t node, const char *path)
{
	int result = kfsw_ftp_mkdir(node, path);

	if (result != 0) {
		return print_ftp_error(sh, "mkdir", node, path, result);
	}
	shell_print(sh, "FTP mkdir node=%u path=%s: PASS", node, path);
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

	shell_print(sh,
		    "FTP %s node=%u source=%s destination=%s: PASS bytes=%" PRIu32
		    " crc32=%08" PRIx32 " duration_ms=%" PRIu32 " throughput_Bps=%" PRIu64,
		    operation, node, source, destination, info.bytes, info.crc32, info.duration_ms,
		    throughput);
	return 0;
}

static int cmd_ftp_list(const struct shell *sh, size_t argc, char **argv)
{
	uint16_t node;
	int result = parse_ftp_node(sh, argv[1], &node);

	return (result == 0) ? ftp_list(sh, node, (argc == 3U) ? argv[2] : "") : result;
}

static int cmd_ftp_stat(const struct shell *sh, size_t argc, char **argv)
{
	uint16_t node;
	int result;

	ARG_UNUSED(argc);
	result = parse_ftp_node(sh, argv[1], &node);
	return (result == 0) ? ftp_stat(sh, node, argv[2]) : result;
}

static int cmd_ftp_mkdir(const struct shell *sh, size_t argc, char **argv)
{
	uint16_t node;
	int result;

	ARG_UNUSED(argc);
	result = parse_ftp_node(sh, argv[1], &node);
	return (result == 0) ? ftp_mkdir(sh, node, argv[2]) : result;
}

static int cmd_ftp_get(const struct shell *sh, size_t argc, char **argv)
{
	uint16_t node;
	int result;

	ARG_UNUSED(argc);
	result = parse_ftp_node(sh, argv[1], &node);
	return (result == 0) ? ftp_transfer(sh, false, node, argv[2], argv[3]) : result;
}

static int cmd_ftp_put(const struct shell *sh, size_t argc, char **argv)
{
	uint16_t node;
	int result;

	ARG_UNUSED(argc);
	result = parse_ftp_node(sh, argv[1], &node);
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
		shell_error(sh, "FTP generate path=%s: FAIL (%d)", argv[1], result);
		return result;
	}
	shell_print(sh, "FTP generate path=%s: PASS bytes=%lu crc32=%08" PRIx32, argv[1], size,
		    crc32);
	return 0;
}

static int cmd_ftp_verify(const struct shell *sh, size_t argc, char **argv)
{
	int result;

	ARG_UNUSED(argc);
	result = ftp_diagnostic_compare(argv[1], argv[2]);
	if (result != 0) {
		shell_error(sh, "FTP verify first=%s second=%s: FAIL (%d)", argv[1], argv[2],
			    result);
		return result;
	}
	shell_print(sh, "FTP verify first=%s second=%s: PASS", argv[1], argv[2]);
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(
	ftp_commands,
	SHELL_CMD_ARG(generate, NULL,
		      "Create deterministic local data: generate <path> <bytes 0..32768>.",
		      cmd_ftp_generate, 3, 0),
	SHELL_CMD_ARG(get, NULL, "Download from a remote node: get <node> <remote> <local>.",
		      cmd_ftp_get, 4, 0),
	SHELL_CMD_ARG(list, NULL, "List a directory: list <node> [path]; <node> may be this node.",
		      cmd_ftp_list, 2, 1),
	SHELL_CMD_ARG(ls, NULL, "List a directory: ls <node> [path]; <node> may be this node.",
		      cmd_ftp_list, 2, 1),
	SHELL_CMD_ARG(mkdir, NULL,
		      "Create a directory: mkdir <node> <path>; <node> may be this node.",
		      cmd_ftp_mkdir, 3, 0),
	SHELL_CMD_ARG(put, NULL, "Upload to a remote node: put <node> <local> <remote>.",
		      cmd_ftp_put, 4, 0),
	SHELL_CMD_ARG(stat, NULL,
		      "Show metadata: stat <node> <path>; <node> may be this node.", cmd_ftp_stat,
		      3, 0),
	SHELL_CMD_ARG(verify, NULL, "Compare two local files: verify <first> <second>.",
		      cmd_ftp_verify, 3, 0),
	SHELL_SUBCMD_SET_END);

static int print_ftp_usage(const struct shell *sh)
{
	shell_error(sh, "Usage: ftp <command> [arguments]");
	shell_error(sh, "   or: ftp <node> <ls|list|stat|mkdir|put|get> [paths]");
	shell_help(sh);
	return -EINVAL;
}

static int cmd_ftp_compat(const struct shell *sh, size_t argc, char **argv)
{
	uint16_t node;
	int result;

	if (argc == 1U) {
		shell_help(sh);
		return SHELL_CMD_HELP_PRINTED;
	}
	if (argc < 3U) {
		return print_ftp_usage(sh);
	}
	result = parse_ftp_node(sh, argv[1], &node);
	if (result != 0) {
		return result;
	}
	if (((strcmp(argv[2], "ls") == 0) || (strcmp(argv[2], "list") == 0)) &&
	    ((argc == 3U) || (argc == 4U))) {
		return ftp_list(sh, node, (argc == 4U) ? argv[3] : "");
	}
	if ((strcmp(argv[2], "stat") == 0) && (argc == 4U)) {
		return ftp_stat(sh, node, argv[3]);
	}
	if ((strcmp(argv[2], "mkdir") == 0) && (argc == 4U)) {
		return ftp_mkdir(sh, node, argv[3]);
	}
	if ((strcmp(argv[2], "put") == 0) && (argc == 5U)) {
		return ftp_transfer(sh, true, node, argv[3], argv[4]);
	}
	if ((strcmp(argv[2], "get") == 0) && (argc == 5U)) {
		return ftp_transfer(sh, false, node, argv[3], argv[4]);
	}
	return print_ftp_usage(sh);
}

SHELL_CMD_ARG_REGISTER(ftp, &ftp_commands,
		       "K-FSW file transfer: ftp <command> ... or ftp <node> <command> ...",
		       cmd_ftp_compat, 1, 4);
