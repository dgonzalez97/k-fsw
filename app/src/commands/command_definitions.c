#include "commands/command_definitions.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/sys/util.h>

#define KFSW_LOG_MODULE KFSW_LOG_MODULE_COMMAND
#if CONFIG_KFSW_LASTWORDS
#include <kfsw/platform/lastwords.h>
#include <kfsw/services/boot.h>
#endif
#include <kfsw/services/log.h>

#if CONFIG_KFSW_PARAM
#include "../parameters/tables.h"
#endif

#if CONFIG_KFSW_STORAGE
#include <kfsw/platform/storage.h>
#endif
#if CONFIG_KFSW_HK
#include <kfsw/services/hk.h>

#include "../hk_entries.h"
#endif

#if CONFIG_KFSW_EVENT
#include <kfsw/services/event.h>
#endif

/*
 * Application commands: noop, info (read-only) and reboot (mutating).
 */

#define KFSW_COMMAND_ID_NOOP 1U
#define KFSW_COMMAND_ID_INFO 2U
#define KFSW_COMMAND_ID_REBOOT 3U
#define KFSW_COMMAND_ID_EVENT_STATS 4U
#define KFSW_COMMAND_ID_EVENT_TAIL 5U
#define KFSW_COMMAND_ID_HK_DEFINE 6U
#define KFSW_COMMAND_ID_HK_PERIOD 7U
#define KFSW_COMMAND_ID_HK_CLEAR 8U

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
#if CONFIG_KFSW_LASTWORDS
	/* Leave a note before the reset. */
#if CONFIG_KFSW_PARAM
	kfsw_lastwords_write(KFSW_LASTWORDS_COMMANDED, 0U, k_uptime_get_32(),
			     kfsw_boot_get_count());
#else
	kfsw_lastwords_write(KFSW_LASTWORDS_COMMANDED, 0U, k_uptime_get_32(), 0U);
#endif
#endif
	sys_reboot(SYS_REBOOT_COLD);
}

static K_WORK_DELAYABLE_DEFINE(reboot_work, reboot_work_handler);

/* Text, so 0000 keeps four characters and 0007 doesn't match 7. */
static const enum kfsw_command_type reboot_arg_types[] = {KFSW_COMMAND_TYPE_TEXT};

static int command_reboot(const struct kfsw_command_arg *args, size_t arg_count,
			  const struct kfsw_command_source *source,
			  struct kfsw_command_result *result)
{
	ARG_UNUSED(arg_count);

	/* The pin is checked on the node that restarts. */
	/* Without the parameter service the compiled default is the pin. */
#if CONFIG_KFSW_PARAM
	if (!kfsw_system_reboot_pin_matches(args[0].value.text)) {
#else
	if (strcmp(args[0].value.text, "0000") != 0) {
#endif
		kfsw_log_warning("Reboot refused: node %u quoted the wrong pin", source->node);
		result->status = KFSW_COMMAND_DENIED;
		(void)snprintf(result->detail, sizeof(result->detail), "wrong pin");
		return -EACCES;
	}

	/* Reset after a short delay so the reply is sent first. */
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

/* Read event records by age. */
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

#if CONFIG_KFSW_HK
/*
 * Housekeeping commands, so reports can be set up from the ground. The entries
 * are one text argument in the shell syntax, because a command has at most four
 * arguments.
 */
static int command_hk_define(const struct kfsw_command_arg *args, size_t arg_count,
			     const struct kfsw_command_source *source,
			     struct kfsw_command_result *result)
{
	struct kfsw_hk_entry entries[CONFIG_KFSW_HK_ENTRIES];
	size_t count = 0U;
	int outcome;

	ARG_UNUSED(arg_count);
	ARG_UNUSED(source);

	if (args[0].value.u32 > UINT8_MAX) {
		result->status = KFSW_COMMAND_INVALID_ARGUMENT;
		return -EINVAL;
	}

	outcome =
		kfsw_app_hk_parse_entries(args[1].value.text, entries, ARRAY_SIZE(entries), &count);
	if (outcome != 0) {
		result->status = KFSW_COMMAND_INVALID_ARGUMENT;
		if (outcome == -E2BIG) {
			(void)snprintf(result->detail, sizeof(result->detail),
				       "more than %u entries", (unsigned int)ARRAY_SIZE(entries));
		} else {
			/* A text argument is at most 64 bytes, so a long list arrives cut
			 * and fails here; quote it back so the ground can see that.
			 */
			(void)snprintf(result->detail, sizeof(result->detail),
				       "cannot read '%s' as [node:]table:offset",
				       args[1].value.text);
		}
		return outcome;
	}

	outcome = kfsw_hk_define((uint8_t)args[0].value.u32, entries, count);
	if (outcome == KFSW_HK_APPLIED_UNSAVED) {
		struct kfsw_hk_stats stats;

		kfsw_hk_get_stats(&stats);
		result->status = KFSW_COMMAND_FAILED;
		(void)snprintf(result->detail, sizeof(result->detail),
			       "applied in RAM; save failed: %d", stats.last_save_error);
		return outcome;
	}
	if (outcome != 0) {
		result->status = KFSW_COMMAND_FAILED;
		(void)snprintf(result->detail, sizeof(result->detail), "define report %u: %d",
			       args[0].value.u32, outcome);
		return outcome;
	}
	(void)snprintf(result->detail, sizeof(result->detail), "report %u defines %u values",
		       args[0].value.u32, (unsigned int)count);
	return 0;
}

static int command_hk_period(const struct kfsw_command_arg *args, size_t arg_count,
			     const struct kfsw_command_source *source,
			     struct kfsw_command_result *result)
{
	int outcome;

	ARG_UNUSED(arg_count);
	ARG_UNUSED(source);

	if (args[0].value.u32 > UINT8_MAX) {
		result->status = KFSW_COMMAND_INVALID_ARGUMENT;
		return -EINVAL;
	}
	outcome = kfsw_hk_set_period((uint8_t)args[0].value.u32, args[1].value.u32);
	if (outcome == KFSW_HK_APPLIED_UNSAVED) {
		struct kfsw_hk_stats stats;

		kfsw_hk_get_stats(&stats);
		result->status = KFSW_COMMAND_FAILED;
		(void)snprintf(result->detail, sizeof(result->detail),
			       "applied in RAM; save failed: %d", stats.last_save_error);
		return outcome;
	}
	if (outcome != 0) {
		result->status = KFSW_COMMAND_FAILED;
		(void)snprintf(result->detail, sizeof(result->detail), "period for report %u: %d",
			       args[0].value.u32, outcome);
		return outcome;
	}
	(void)snprintf(result->detail, sizeof(result->detail), "report %u every %u ms",
		       args[0].value.u32, args[1].value.u32);
	return 0;
}

static int command_hk_clear(const struct kfsw_command_arg *args, size_t arg_count,
			    const struct kfsw_command_source *source,
			    struct kfsw_command_result *result)
{
	int outcome;

	ARG_UNUSED(arg_count);
	ARG_UNUSED(source);

	if (args[0].value.u32 > UINT8_MAX) {
		result->status = KFSW_COMMAND_INVALID_ARGUMENT;
		return -EINVAL;
	}
	outcome = kfsw_hk_clear((uint8_t)args[0].value.u32);
	if (outcome == KFSW_HK_APPLIED_UNSAVED) {
		struct kfsw_hk_stats stats;

		kfsw_hk_get_stats(&stats);
		result->status = KFSW_COMMAND_FAILED;
		(void)snprintf(result->detail, sizeof(result->detail),
			       "applied in RAM; save failed: %d", stats.last_save_error);
		return outcome;
	}
	if (outcome != 0) {
		result->status = KFSW_COMMAND_FAILED;
		(void)snprintf(result->detail, sizeof(result->detail), "clear report %u: %d",
			       args[0].value.u32, outcome);
		return outcome;
	}
	(void)snprintf(result->detail, sizeof(result->detail), "report %u cleared",
		       args[0].value.u32);
	return 0;
}

static const enum kfsw_command_type hk_define_args[] = {KFSW_COMMAND_TYPE_U32,
							KFSW_COMMAND_TYPE_TEXT};
static const enum kfsw_command_type hk_period_args[] = {KFSW_COMMAND_TYPE_U32,
							KFSW_COMMAND_TYPE_U32};
static const enum kfsw_command_type hk_clear_args[] = {KFSW_COMMAND_TYPE_U32};
#endif /* CONFIG_KFSW_HK */

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
#if CONFIG_KFSW_HK
	{
		.id = KFSW_COMMAND_ID_HK_DEFINE,
		.name = "hk_define",
		.help = "Name what a report collects: <report> \"[node:]table:offset ...\".",
		.arg_count = 2U,
		.arg_types = hk_define_args,
		.handler = command_hk_define,
	},
	{
		.id = KFSW_COMMAND_ID_HK_PERIOD,
		.name = "hk_period",
		.help = "Collect repeatedly: hk_period <report> <ms>, 0 to stop.",
		.arg_count = 2U,
		.arg_types = hk_period_args,
		.handler = command_hk_period,
	},
	{
		.id = KFSW_COMMAND_ID_HK_CLEAR,
		.name = "hk_clear",
		.help = "Forget a report: hk_clear <report>.",
		.arg_count = 1U,
		.arg_types = hk_clear_args,
		.handler = command_hk_clear,
	},
#endif
#if CONFIG_REBOOT
	{
		.id = KFSW_COMMAND_ID_REBOOT,
		.name = "reboot",
		.help = "Reset this node after a short delay: reboot <pin>.",
		.flags = KFSW_COMMAND_FLAG_MUTATING,
		.arg_count = 1U,
		.arg_types = reboot_arg_types,
		.handler = command_reboot,
	},
#endif
};

const struct kfsw_command_definition_set kfsw_app_command_definitions = {
	.commands = app_commands,
	.count = ARRAY_SIZE(app_commands),
};
