#include <errno.h>
#include <inttypes.h>
#include <stdint.h>

#include <zephyr/shell/shell.h>
#include <zephyr/sys/util.h>

#include <kfsw/services/fwu.h>
#include <kfsw/services/fwu_lite.h>

#include "shell_remote.h"

static void print_status(const struct shell *sh, const struct kfsw_fwu_status *status)
{
	char digest[KFSW_FWU_SHA256_SIZE * 2U + 1U];

	shell_print(sh, "state: %s", kfsw_fwu_state_name(status->state));
	shell_print(sh, "transfer_id: %" PRIu32, status->handle.transfer_id);
	shell_print(sh, "generation: %" PRIu32, status->handle.generation);
	shell_print(sh, "total_size: %" PRIu32, status->total_size);
	shell_print(sh, "received: %" PRIu32, status->received);
	shell_print(sh, "committed: %" PRIu32, status->committed);
	shell_print(sh, "expected_crc32: %08" PRIx32, status->expected_crc32);
	shell_print(sh, "actual_crc32: %08" PRIx32, status->actual_crc32);
	shell_print(sh, "swap_scheduled: %s", status->swap_scheduled ? "yes" : "no");
	if (status->image.digest_present) {
		(void)bin2hex(status->image.sha256, sizeof(status->image.sha256), digest,
			      sizeof(digest));
		shell_print(sh, "sha256: %s", digest);
	}
}

static int cmd_status(const struct shell *sh, size_t argc, char **argv)
{
	struct kfsw_fwu_status status;
	uint16_t node;
	int result;

	if (argc == 1U) {
		result = kfsw_fwu_get_status(&status);
	} else {
		result = kfsw_shell_parse_node(sh, argv[1], &node);
		if (result != 0) {
			return result;
		}
		result = kfsw_fwu_lite_remote_status(node, NULL, &status);
		if (result == -ENOENT) {
			shell_print(sh, "Node %u has no owned transfer", node);
			return 0;
		}
	}
	if (result != 0) {
		shell_error(sh, "Firmware upload status failed (%d)", result);
		return result;
	}
	print_status(sh, &status);
	return 0;
}

static int cmd_send(const struct shell *sh, size_t argc, char **argv)
{
	struct kfsw_fwu_status receipt = {0};
	uint32_t resent = 0U;
	uint16_t node;
	int result;

	ARG_UNUSED(argc);
	result = kfsw_shell_parse_node(sh, argv[1], &node);
	if (result != 0) {
		return result;
	}
	shell_print(sh, "Sending %s to node %u", argv[2], node);
	result = kfsw_fwu_lite_send_file(node, argv[2], &receipt, &resent);
	if (result != 0) {
		shell_error(sh, "Upload stopped (%d); %" PRIu32 " block(s) resent", result, resent);
		if (receipt.handle.generation != 0U) {
			print_status(sh, &receipt);
		}
		shell_print(
			sh,
			"Receiver progress retained; repeat send with the same image to resume");
		return result;
	}
	shell_print(sh,
		    "Upload complete; no validation or activation requested; %" PRIu32
		    " block(s) resent",
		    resent);
	print_status(sh, &receipt);
	shell_print(sh, "Run: fwu_lite validate %u", node);
	return 0;
}

static int resolve_transfer(const struct shell *sh, const char *text, uint16_t *node,
			    struct kfsw_fwu_status *status)
{
	int result = kfsw_shell_parse_node(sh, text, node);

	if (result == 0) {
		result = kfsw_fwu_lite_remote_status(*node, NULL, status);
		if (result != 0) {
			shell_error(sh, "Could not resolve this node's owned transfer (%d)",
				    result);
		}
	}
	return result;
}

static int cmd_validate(const struct shell *sh, size_t argc, char **argv)
{
	struct kfsw_fwu_status status;
	uint16_t node;
	int result;

	ARG_UNUSED(argc);
	result = resolve_transfer(sh, argv[1], &node, &status);
	if (result == 0) {
		const struct kfsw_fwu_handle handle = status.handle;

		result = kfsw_fwu_lite_remote_verify(node, &handle, &status);
	}
	if (result != 0) {
		shell_error(sh, "Stored image validation failed (%d)", result);
		return result;
	}
	shell_print(sh, "Stored image verified; activation not requested");
	print_status(sh, &status);
	return 0;
}

static int cmd_flash(const struct shell *sh, size_t argc, char **argv)
{
	struct kfsw_fwu_status status;
	uint16_t node;
	int result;

	ARG_UNUSED(argc);
	result = resolve_transfer(sh, argv[1], &node, &status);
	if (result == 0) {
		const struct kfsw_fwu_handle handle = status.handle;

		result = kfsw_fwu_lite_remote_activate(node, &handle, &status);
	}
	if (result != 0) {
		shell_error(sh, "Image activation failed (%d)", result);
		return result;
	}
	shell_print(sh, "Node %u image ready; swap scheduled: %s", node,
		    status.swap_scheduled ? "yes" : "no");
	shell_print(sh, "No reset requested; check target boot behavior before resetting");
	return 0;
}

static int cmd_abort(const struct shell *sh, size_t argc, char **argv)
{
	struct kfsw_fwu_status status;
	uint16_t node;
	int result;

	ARG_UNUSED(argc);
	result = resolve_transfer(sh, argv[1], &node, &status);
	if (result == 0) {
		const struct kfsw_fwu_handle handle = status.handle;

		result = kfsw_fwu_lite_remote_abort(node, &handle, &status);
	}
	if (result != 0) {
		shell_error(sh, "Owned transfer cleanup failed (%d)", result);
		return result;
	}
	shell_print(sh, "Owned transfer aborted; target slot erased");
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(
	fwu_lite_commands,
	SHELL_CMD_ARG(abort, NULL, "Erase this sender's transfer: abort <node>", cmd_abort, 2, 0),
	SHELL_CMD_ARG(flash, NULL, "Activate a validated image: flash <node>", cmd_flash, 2, 0),
	SHELL_CMD_ARG(send, NULL, "Upload or resume: send <node> <path>", cmd_send, 3, 0),
	SHELL_CMD_ARG(status, NULL, "Show local or owned remote progress: status [node]", cmd_status,
		      1, 1),
	SHELL_CMD_ARG(validate, NULL, "Validate stored bytes: validate <node>", cmd_validate, 2, 0),
	SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(fwu_lite, &fwu_lite_commands, "Resumable firmware upload.", NULL);
