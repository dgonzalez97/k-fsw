#include "commands/command_definitions.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/sys/util.h>

/* Attributes this file's messages, so its level can be raised alone. */
#define KFSW_LOG_MODULE KFSW_LOG_MODULE_COMMAND
#include <kfsw/services/log.h>

#if CONFIG_KFSW_STORAGE
#include <kfsw/platform/storage.h>
#endif
#if CONFIG_KFSW_EVENT
#include <kfsw/services/event.h>
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
#define KFSW_COMMAND_ID_EVENT_STATS 4U
#define KFSW_COMMAND_ID_EVENT_TAIL 5U

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

#if CONFIG_KFSW_EVENT
static int command_event_stats(const struct kfsw_command_arg *args, size_t arg_count,
			       const struct kfsw_command_source *source,
			       struct kfsw_command_result *result)
{
	struct kfsw_event_stats stats;

	ARG_UNUSED(args);
	ARG_UNUSED(arg_count);
	ARG_UNUSED(source);

	kfsw_event_get_stats(&stats);
	(void)snprintf(result->detail, sizeof(result->detail),
		       "held=%u/%u recorded=%u overwritten=%u rejected=%u", stats.held,
		       stats.capacity, stats.recorded, stats.overwritten, stats.rejected);
	result->status = KFSW_COMMAND_OK;
	return 0;
}

/*
 * One record per call, addressed by age. Reading the record back is what makes
 * an unattended node answerable, so it is reachable the same way any other
 * command is rather than needing its own protocol.
 */
static int command_event_tail(const struct kfsw_command_arg *args, size_t arg_count,
			      const struct kfsw_command_source *source,
			      struct kfsw_command_result *result)
{
	struct kfsw_event_record record;
	char payload_text[(KFSW_EVENT_MAX_PAYLOAD_SIZE * 2U) + 1U];
	int outcome;

	ARG_UNUSED(arg_count);
	ARG_UNUSED(source);

	if (args[0].value.u32 > UINT16_MAX) {
		result->status = KFSW_COMMAND_INVALID_ARGUMENT;
		return -EINVAL;
	}
	outcome = kfsw_event_get((uint16_t)args[0].value.u32, &record);
	if (outcome != 0) {
		result->status = KFSW_COMMAND_FAILED;
		(void)snprintf(result->detail, sizeof(result->detail), "no record at age %u",
			       args[0].value.u32);
		return outcome;
	}
	for (uint8_t index = 0U; index < record.payload_size; index++) {
		(void)snprintf(&payload_text[index * 2U], 3U, "%02x", record.payload[index]);
	}
	payload_text[record.payload_size * 2U] = '\0';

	(void)snprintf(result->detail, sizeof(result->detail), "seq=%u t=%ums %s/%u sev=%u %s",
		       record.sequence, (unsigned int)(record.monotonic_us / 1000U),
		       kfsw_event_source_name((enum kfsw_event_source)record.source), record.id,
		       record.severity, payload_text);
	result->status = KFSW_COMMAND_OK;
	return 0;
}

static const enum kfsw_command_type event_tail_args[] = {KFSW_COMMAND_TYPE_U32};
#endif /* CONFIG_KFSW_EVENT */

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
#if CONFIG_KFSW_EVENT
	{
		.id = KFSW_COMMAND_ID_EVENT_STATS,
		.name = "event_stats",
		.help = "Report event record counters.",
		.handler = command_event_stats,
	},
	{
		.id = KFSW_COMMAND_ID_EVENT_TAIL,
		.name = "event_tail",
		.help = "Read one recorded event by age, newest is 0.",
		.arg_count = 1U,
		.arg_types = event_tail_args,
		.handler = command_event_tail,
	},
#endif
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
