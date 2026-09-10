#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/shell/shell.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>

#include <kfsw/services/hk.h>
#include <kfsw/services/parameter.h>

#include "hk_entries.h"

/* Thin, like every adapter here: parse, call the service, print. */

static int parse_u32(const struct shell *sh, const char *text, uint32_t *out, const char *what)
{
	char *end;
	unsigned long parsed = strtoul(text, &end, 0);

	if ((end == text) || (*end != '\0') || (parsed > UINT32_MAX)) {
		shell_error(sh, "Invalid %s: %s", what, text);
		return -EINVAL;
	}
	*out = (uint32_t)parsed;
	return 0;
}

/* The parse lives in hk_entries.c because the command service needs the same
 * one; only the complaint is the shell's.
 */
static int parse_entry(const struct shell *sh, const char *text, struct kfsw_hk_entry *entry)
{
	if (kfsw_app_hk_parse_entry(text, entry) != 0) {
		shell_error(sh, "Invalid entry '%s': use [node:]table:offset", text);
		return -EINVAL;
	}
	return 0;
}

static int cmd_hk_define(const struct shell *sh, size_t argc, char **argv)
{
	struct kfsw_hk_entry entries[CONFIG_KFSW_HK_ENTRIES];
	uint32_t report;
	size_t count = argc - 2U;
	int result;

	result = parse_u32(sh, argv[1], &report, "report");
	if (result != 0) {
		return result;
	}
	if (count > ARRAY_SIZE(entries)) {
		shell_error(sh, "A report holds at most %u values",
			    (unsigned int)ARRAY_SIZE(entries));
		return -E2BIG;
	}

	for (size_t index = 0U; index < count; index++) {
		result = parse_entry(sh, argv[index + 2U], &entries[index]);
		if (result != 0) {
			return result;
		}
	}

	result = kfsw_hk_define((uint8_t)report, entries, count);
	if (result != 0) {
		shell_error(sh, "define report %u: %d", report, result);
		return result;
	}
	shell_print(sh, "report %u defines %u values", report, (unsigned int)count);
	return 0;
}

static int cmd_hk_clear(const struct shell *sh, size_t argc, char **argv)
{
	uint32_t report;
	int result;

	ARG_UNUSED(argc);
	result = parse_u32(sh, argv[1], &report, "report");
	if (result != 0) {
		return result;
	}
	result = kfsw_hk_clear((uint8_t)report);
	if (result != 0) {
		shell_error(sh, "clear report %u: %d", report, result);
		return result;
	}
	shell_print(sh, "report %u cleared", report);
	return 0;
}

static int cmd_hk_show(const struct shell *sh, size_t argc, char **argv)
{
	struct kfsw_hk_stats stats;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	kfsw_hk_get_stats(&stats);
	shell_print(sh, "reports defined: %u", stats.reports);
	shell_print(sh, "collections: %u", stats.collections);
	shell_print(sh, "failed collections: %u", stats.failures);
	shell_print(sh, "absent values: %u", stats.entries_failed);
	shell_print(sh, "samples overwritten: %u", stats.overwritten);
	shell_print(sh, "last collection: %u", stats.last_seconds);

	for (uint8_t report = 0U; report < CONFIG_KFSW_HK_REPORTS; report++) {
		struct kfsw_hk_entry entries[CONFIG_KFSW_HK_ENTRIES];
		size_t count = ARRAY_SIZE(entries);
		uint32_t period = 0U;
		uint16_t depth = 0U;

		if (kfsw_hk_get_definition(report, entries, &count) != 0) {
			continue;
		}
		(void)kfsw_hk_get_period(report, &period);
		(void)kfsw_hk_depth(report, &depth);
		shell_print(sh, "report %u: %u values, period %u ms, %u samples held", report,
			    (unsigned int)count, period, depth);
		for (size_t index = 0U; index < count; index++) {
			shell_print(sh, "  node %u  table %u  offset 0x%02x", entries[index].node,
				    (unsigned int)(entries[index].param_id >> 8),
				    (unsigned int)(entries[index].param_id & 0xFFU));
		}
	}
	return 0;
}

static int cmd_hk_collect(const struct shell *sh, size_t argc, char **argv)
{
	uint32_t report;
	int result;

	ARG_UNUSED(argc);
	result = parse_u32(sh, argv[1], &report, "report");
	if (result != 0) {
		return result;
	}
	result = kfsw_hk_collect((uint8_t)report);
	if (result != 0) {
		shell_error(sh, "collect report %u: %d", report, result);
		return result;
	}
	shell_print(sh, "report %u collected", report);
	return 0;
}

static int cmd_hk_get(const struct shell *sh, size_t argc, char **argv)
{
	static struct kfsw_hk_sample sample;
	uint32_t report;
	uint32_t wanted = 1U;
	int result;

	result = parse_u32(sh, argv[1], &report, "report");
	if (result != 0) {
		return result;
	}
	if (argc > 2U) {
		result = parse_u32(sh, argv[2], &wanted, "count");
		if (result != 0) {
			return result;
		}
	}

	for (uint32_t age = 0U; age < wanted; age++) {
		char line[3 * 32 + 1];
		size_t used = 0U;

		result = kfsw_hk_get((uint8_t)report, (uint16_t)age, &sample);
		if (result != 0) {
			if (age == 0U) {
				shell_error(sh, "report %u has nothing at age %u", report, age);
			}
			break;
		}

		/* Both flags are named rather than left in a hex byte: a reader
		 * who has to decode 0x02 to find out the timestamp is missing
		 * will read the zero as a date instead.
		 */
		shell_print(sh, "seq %u  at %u%s  %u values%s  %u bytes", sample.sequence,
			    sample.seconds,
			    ((sample.flags & KFSW_HK_FLAG_CLOCK_UNSET) != 0U) ? " (no clock)" : "",
			    sample.entry_count,
			    ((sample.flags & KFSW_HK_FLAG_INCOMPLETE) != 0U) ? " (incomplete)" : "",
			    sample.length);

		/* The values as they go out, so a bench can compare a frame
		 * against the parameters it was built from.
		 */
		for (size_t index = KFSW_HK_HEADER_SIZE; index < sample.length; index++) {
			if ((used + 3U) >= sizeof(line)) {
				break;
			}
			used += (size_t)snprintk(&line[used], sizeof(line) - used, "%02x",
						 sample.data[index]);
		}
		line[used] = '\0';
		shell_print(sh, "  %s", line);
	}
	return 0;
}

static int cmd_hk_period(const struct shell *sh, size_t argc, char **argv)
{
	uint32_t report;
	uint32_t period;
	int result;

	ARG_UNUSED(argc);
	result = parse_u32(sh, argv[1], &report, "report");
	if (result != 0) {
		return result;
	}
	result = parse_u32(sh, argv[2], &period, "period");
	if (result != 0) {
		return result;
	}
	result = kfsw_hk_set_period((uint8_t)report, period);
	if (result != 0) {
		shell_error(sh, "period for report %u: %d", report, result);
		return result;
	}
	shell_print(sh, "report %u every %u ms", report, period);
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(
	hk_commands,
	SHELL_CMD_ARG(define, NULL, "Name what a report collects: define <report> [node:]table:offset ...",
		      cmd_hk_define, 3, CONFIG_KFSW_HK_ENTRIES),
	SHELL_CMD_ARG(clear, NULL, "Forget a report: clear <report>.", cmd_hk_clear, 2, 0),
	SHELL_CMD_ARG(show, NULL, "Show the reports and the counters.", cmd_hk_show, 1, 0),
	SHELL_CMD_ARG(collect, NULL, "Collect now: collect <report>.", cmd_hk_collect, 2, 0),
	SHELL_CMD_ARG(get, NULL, "Read samples back: get <report> [count].", cmd_hk_get, 2, 1),
	SHELL_CMD_ARG(period, NULL, "Collect repeatedly: period <report> <ms>, 0 to stop.",
		      cmd_hk_period, 3, 0),
	SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(hk, &hk_commands, "Housekeeping: collect a set of values in one pass.", NULL);
