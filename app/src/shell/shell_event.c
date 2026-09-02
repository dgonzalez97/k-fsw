#include <inttypes.h>
#include <stdint.h>

#include <zephyr/shell/shell.h>
#include <zephyr/sys/util.h>

#include <kfsw/services/event.h>

/* Local view of the event record. Payloads are shown as bytes, not decoded:
 * their meaning belongs to the producing component and to ground tooling.
 */

struct event_print_context {
	const struct shell *shell;
	uint32_t shown;
};

static void print_payload(const struct shell *sh, const struct kfsw_event_record *record)
{
	char text[(KFSW_EVENT_MAX_PAYLOAD_SIZE * 2U) + 1U];

	for (uint8_t index = 0U; index < record->payload_size; index++) {
		(void)snprintk(&text[index * 2U], 3U, "%02x", record->payload[index]);
	}
	text[record->payload_size * 2U] = '\0';
	shell_print(sh, "%8" PRIu32 " %10" PRIu64 " %-8s %-8s %5u %s", record->sequence,
		    record->monotonic_us / 1000U,
		    kfsw_event_source_name((enum kfsw_event_source)record->source),
		    kfsw_event_severity_name((enum kfsw_event_severity)record->severity),
		    record->id, text);
}

static bool print_event(const struct kfsw_event_record *record, void *context)
{
	struct event_print_context *print_context = context;

	print_payload(print_context->shell, record);
	print_context->shown++;
	return true;
}

static int cmd_event_list(const struct shell *sh, size_t argc, char **argv)
{
	struct event_print_context context = {.shell = sh};

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(sh, "     SEQ    TIME_MS SOURCE   SEVERITY    ID PAYLOAD");
	kfsw_event_visit(print_event, &context);
	shell_print(sh, "Events listed: %" PRIu32, context.shown);
	return 0;
}

static int cmd_event_stats(const struct shell *sh, size_t argc, char **argv)
{
	struct kfsw_event_stats stats;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	kfsw_event_get_stats(&stats);
	shell_print(sh, "K-FSW events");
	shell_print(sh, "held: %u/%u", stats.held, stats.capacity);
	shell_print(sh, "recorded: %" PRIu32, stats.recorded);
	shell_print(sh, "overwritten: %" PRIu32, stats.overwritten);
	shell_print(sh, "rejected: %" PRIu32, stats.rejected);
	return 0;
}

static int cmd_event_clear(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	kfsw_event_clear();
	shell_print(sh, "Event record cleared");
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(
	event_commands,
	SHELL_CMD_ARG(clear, NULL, "Discard held records; counters are kept.", cmd_event_clear, 1,
		      0),
	SHELL_CMD_ARG(list, NULL, "Show held records, oldest first.", cmd_event_list, 1, 0),
	SHELL_CMD_ARG(stats, NULL, "Show record counters.", cmd_event_stats, 1, 0),
	SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(event, &event_commands, "K-FSW event record.", NULL);
