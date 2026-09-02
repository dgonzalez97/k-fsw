#include "commands/command_definitions.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/sys/util.h>

#include <kfsw/services/log.h>

#if CONFIG_KFSW_STORAGE
#include <kfsw/platform/storage.h>
#endif

/*
 * Application-owned commands. These exist to prove the mechanism end to end
 * from both front ends: one with no arguments and no effect, one read-only
 * with a payload, and one mutating.
 *
 * Remote parameter access deliberately has no command here. The parameter
 * service already owns that path over CSP, and a second route to the same
 * operation would split its validation.
 */

#define KFSW_COMMAND_ID_NOOP 1U
#define KFSW_COMMAND_ID_INFO 2U
#define KFSW_COMMAND_ID_REBOOT 3U

/* Give the reply time to leave before the reset takes the link down. */
#define KFSW_COMMAND_REBOOT_DELAY_MS 500U

static int command_noop(const struct kfsw_command_arg *args, size_t arg_count,
			const struct kfsw_command_source *source,
			struct kfsw_command_result *result)
{
	ARG_UNUSED(args);
	ARG_UNUSED(arg_count);

	result->status = KFSW_COMMAND_OK;
	(void)snprintf(result->detail, sizeof(result->detail), "noop from node %u", source->node);
	return 0;
}

static int command_info(const struct kfsw_command_arg *args, size_t arg_count,
			const struct kfsw_command_source *source,
			struct kfsw_command_result *result)
{
	ARG_UNUSED(args);
	ARG_UNUSED(arg_count);
	ARG_UNUSED(source);

#if CONFIG_KFSW_STORAGE
	struct kfsw_storage_info storage;

	kfsw_storage_get_info(&storage);
	(void)snprintf(result->detail, sizeof(result->detail),
		       "uptime_ms=%u storage=%s free_bytes=%" PRIu64, k_uptime_get_32(),
		       storage.ready ? "ready" : "down", storage.free_bytes);
#else
	(void)snprintf(result->detail, sizeof(result->detail), "uptime_ms=%u storage=absent",
		       k_uptime_get_32());
#endif
	result->status = KFSW_COMMAND_OK;
	return 0;
}

#if CONFIG_REBOOT
static void reboot_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	kfsw_log_warning("Rebooting on command");
	sys_reboot(SYS_REBOOT_COLD);
}

static K_WORK_DELAYABLE_DEFINE(reboot_work, reboot_work_handler);

static int command_reboot(const struct kfsw_command_arg *args, size_t arg_count,
			  const struct kfsw_command_source *source,
			  struct kfsw_command_result *result)
{
	ARG_UNUSED(args);
	ARG_UNUSED(arg_count);

	/*
	 * Rebooting inside the handler would drop the connection before the
	 * caller learned the command was accepted, so the reset is deferred
	 * just long enough for the reply to be sent.
	 */
	if (k_work_schedule(&reboot_work, K_MSEC(KFSW_COMMAND_REBOOT_DELAY_MS)) < 0) {
		result->status = KFSW_COMMAND_BUSY;
		return -EBUSY;
	}
	kfsw_log_warning("Reboot requested by node %u", source->node);
	result->status = KFSW_COMMAND_OK;
	(void)snprintf(result->detail, sizeof(result->detail), "rebooting in %u ms",
		       KFSW_COMMAND_REBOOT_DELAY_MS);
	return 0;
}
#endif /* CONFIG_REBOOT */

static const struct kfsw_command_definition app_commands[] = {
	{
		.id = KFSW_COMMAND_ID_NOOP,
		.name = "noop",
		.help = "Round trip with no effect.",
		.handler = command_noop,
	},
	{
		.id = KFSW_COMMAND_ID_INFO,
		.name = "info",
		.help = "Report uptime and storage state.",
		.handler = command_info,
	},
#if CONFIG_REBOOT
	{
		.id = KFSW_COMMAND_ID_REBOOT,
		.name = "reboot",
		.help = "Reset this node after a short delay.",
		.flags = KFSW_COMMAND_FLAG_MUTATING,
		.handler = command_reboot,
	},
#endif
};

const struct kfsw_command_definition_set kfsw_app_command_definitions = {
	.commands = app_commands,
	.count = ARRAY_SIZE(app_commands),
};
