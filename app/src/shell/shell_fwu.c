#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdlib.h>

#include <zephyr/shell/shell.h>
#include <zephyr/sys/util.h>

#include <kfsw/services/fwu.h>
#if CONFIG_KFSW_FWU_LITE_CSP
#include <kfsw/services/fwu_lite.h>
#endif

static int parse_u32(const struct shell *sh, const char *text, const char *what, uint32_t *value)
{
	char *end = NULL;
	unsigned long parsed;

	parsed = strtoul(text, &end, 0);
	if ((end == text) || (*end != '\0') || (parsed > UINT32_MAX)) {
		shell_error(sh, "Invalid %s: %s", what, text);
		return -EINVAL;
	}

	*value = (uint32_t)parsed;
	return 0;
}

static int cmd_fwu_status(const struct shell *sh, size_t argc, char **argv)
{
	struct kfsw_fwu_status status;
	int result;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	result = kfsw_fwu_get_status(&status);
	if (result != 0) {
		shell_error(sh, "firmware update status unavailable (%d)", result);
		return result;
	}

	shell_print(sh, "K-FSW firmware update");
	shell_print(sh, "target: %s", status.target_bound ? "bound" : "absent");
	shell_print(sh, "state: %s", kfsw_fwu_state_name((enum kfsw_fwu_state)status.state));
	shell_print(sh, "max_image_bytes: %" PRIu32, kfsw_fwu_max_image_size());
	shell_print(sh, "write_offset: %" PRIu32, kfsw_fwu_slot_write_offset());
	shell_print(sh, "total_size: %" PRIu32, status.total_size);
	shell_print(sh, "received: %" PRIu32, status.received);
	shell_print(sh, "expected_crc32: %08" PRIx32, status.expected_crc32);
	shell_print(sh, "actual_crc32: %08" PRIx32, status.actual_crc32);
	shell_print(sh, "swap_scheduled: %s", status.swap_scheduled ? "yes" : "no");
	shell_print(sh, "started: %" PRIu32, status.started);
	shell_print(sh, "completed: %" PRIu32, status.completed);
	shell_print(sh, "failed: %" PRIu32, status.failed);
	return 0;
}

static int cmd_fwu_begin(const struct shell *sh, size_t argc, char **argv)
{
	uint32_t total_size;
	uint32_t crc32;
	int result;

	ARG_UNUSED(argc);

	result = parse_u32(sh, argv[1], "size", &total_size);
	if (result != 0) {
		return result;
	}
	result = parse_u32(sh, argv[2], "crc32", &crc32);
	if (result != 0) {
		return result;
	}

	result = kfsw_fwu_begin(total_size, crc32);
	if (result != 0) {
		shell_error(sh, "firmware update begin failed (%d)", result);
		return result;
	}

	shell_print(sh, "Firmware update ready for %" PRIu32 " bytes", total_size);
	return 0;
}

static int cmd_fwu_finish(const struct shell *sh, size_t argc, char **argv)
{
	struct kfsw_fwu_status status;
	int result;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	result = kfsw_fwu_finish();
	if (result != 0) {
		shell_error(sh, "firmware update finish failed (%d)", result);
		return result;
	}

	(void)kfsw_fwu_get_status(&status);
	shell_print(sh, "Firmware update accepted; swap scheduled: %s",
		    status.swap_scheduled ? "yes" : "no");
	shell_print(sh, "Reboot to try it. It reverts unless it is confirmed.");
	return 0;
}

static int cmd_fwu_abort(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	(void)kfsw_fwu_abort();
	shell_print(sh, "Firmware update aborted; the slot is erased");
	return 0;
}

#if CONFIG_KFSW_FWU_LITE_CSP
static int cmd_fwu_send(const struct shell *sh, size_t argc, char **argv)
{
	uint32_t blocks_resent = 0U;
	uint32_t node;
	int result;

	ARG_UNUSED(argc);

	result = parse_u32(sh, argv[1], "node", &node);
	if (result != 0) {
		return result;
	}

	shell_print(sh, "Sending %s to node %" PRIu32 "; this takes minutes over a slow link",
		    argv[2], node);

	result = kfsw_fwu_lite_send_file((uint16_t)node, argv[2], &blocks_resent);
	if (result != 0) {
		shell_error(sh, "firmware upload failed (%d) after %" PRIu32 " resent block(s)",
			    result, blocks_resent);
		return result;
	}

	shell_print(sh, "Image accepted and verified; %" PRIu32 " block(s) resent", blocks_resent);
	shell_print(sh, "Run: fwu flash %" PRIu32, node);
	return 0;
}

static int cmd_fwu_flash(const struct shell *sh, size_t argc, char **argv)
{
	uint32_t node;
	int result;

	ARG_UNUSED(argc);

	result = parse_u32(sh, argv[1], "node", &node);
	if (result != 0) {
		return result;
	}

	result = kfsw_fwu_lite_start_flashing((uint16_t)node);
	if (result != 0) {
		shell_error(sh, "start flashing failed (%d)", result);
		return result;
	}

	shell_print(sh, "Node %" PRIu32 " scheduled a swap; reboot it to try the image", node);
	shell_print(sh, "It reverts unless it is confirmed after booting.");
	return 0;
}
#endif

SHELL_STATIC_SUBCMD_SET_CREATE(
	fwu_commands,
	SHELL_CMD_ARG(abort, NULL, "Abandon a transfer and erase the slot.", cmd_fwu_abort, 1, 0),
	SHELL_CMD_ARG(begin, NULL, "Start a transfer: begin <size> <crc32>.", cmd_fwu_begin, 3,
		      0),
	SHELL_CMD_ARG(finish, NULL, "Verify the image and offer it to the bootloader.",
		      cmd_fwu_finish, 1, 0),
#if CONFIG_KFSW_FWU_LITE_CSP
	SHELL_CMD_ARG(flash, NULL, "Ask a node to boot its image: flash <node>.", cmd_fwu_flash,
		      2, 0),
	SHELL_CMD_ARG(send, NULL, "Send an image: send <node> <path>.", cmd_fwu_send, 3, 0),
#endif
	SHELL_CMD_ARG(status, NULL, "Show update state and slot geometry.", cmd_fwu_status, 1, 0),
	SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(fwu, &fwu_commands, "K-FSW firmware update.", NULL);
