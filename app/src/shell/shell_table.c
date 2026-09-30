#include <errno.h>
#include <stdint.h>

#include <zephyr/shell/shell.h>

#include <kfsw/services/table.h>

static void print_report(const struct shell *sh, const struct kfsw_table_report *report)
{
	shell_print(sh, "table: %u", report->table);
	shell_print(sh, "entries: %u accepted: %u", report->entries, report->accepted);
	if (report->failed_entry != 0U) {
		shell_error(sh, "refused at entry %u, offset 0x%02x (%d)", report->failed_entry,
			    report->failed_offset, report->reason);
	}
}

static int cmd_table_show(const struct shell *sh, size_t argc, char **argv)
{
	struct kfsw_table_status status;
	int result;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	result = kfsw_table_get_status(&status);
	if (result != 0) {
		shell_error(sh, "Table status unavailable: %d", result);
		return result;
	}

	shell_print(sh, "state: %s", kfsw_table_state_name((enum kfsw_table_state)status.state));
	shell_print(sh, "last: table %u, %u entries", status.table, status.entries);
	shell_print(sh, "file: %s", (status.path[0] != '\0') ? status.path : "(none)");
	shell_print(sh, "loads: %u rejections: %u reverts: %u", status.loads, status.rejections,
		    status.reverts);
	return 0;
}

static int cmd_table_check(const struct shell *sh, size_t argc, char **argv)
{
	struct kfsw_table_report report;
	int result;

	ARG_UNUSED(argc);

	result = kfsw_table_validate(argv[1], &report);
	print_report(sh, &report);
	if (result != 0) {
		shell_error(sh, "Table file refused: %d", result);
		return result;
	}

	shell_print(sh, "Table file would be accepted whole; nothing was applied");
	return 0;
}

static int cmd_table_load(const struct shell *sh, size_t argc, char **argv)
{
	struct kfsw_table_report report;
	int result;

	ARG_UNUSED(argc);

	result = kfsw_table_load(argv[1], &report);
	print_report(sh, &report);
	if (result != 0) {
		shell_error(sh, "Table file refused, nothing applied: %d", result);
		return result;
	}

	shell_print(sh, "Table %u loaded, %u entries; revert with 'table revert'", report.table,
		    report.accepted);
	return 0;
}

static int cmd_table_revert(const struct shell *sh, size_t argc, char **argv)
{
	int result;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	result = kfsw_table_revert();
	if (result != 0) {
		shell_error(sh, "Nothing to revert: %d", result);
		return result;
	}

	shell_print(sh, "Previous values are back");
	return 0;
}

static int cmd_table_dump(const struct shell *sh, size_t argc, char **argv)
{
	uint16_t entries = 0U;
	unsigned long table;
	int parse_error = 0;
	int result;

	ARG_UNUSED(argc);

	table = shell_strtoul(argv[1], 10, &parse_error);
	if ((parse_error != 0) || (table == 0UL) || (table > 99UL)) {
		shell_error(sh, "Table must be in range 1..99");
		return -EINVAL;
	}

	result = kfsw_table_dump((uint8_t)table, argv[2], &entries);
	if (result != 0) {
		shell_error(sh, "Table dump failed: %d", result);
		return result;
	}

	shell_print(sh, "Table %lu written to %s, %u entries", table, argv[2], entries);
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(
	table_commands,
	SHELL_CMD_ARG(check, NULL, "Check a table file without applying it: check <path>.",
		      cmd_table_check, 2, 0),
	SHELL_CMD_ARG(dump, NULL, "Write a table out as a file: dump <table> <path>.",
		      cmd_table_dump, 3, 0),
	SHELL_CMD_ARG(load, NULL, "Adopt a table file, all of it or none: load <path>.",
		      cmd_table_load, 2, 0),
	SHELL_CMD_ARG(revert, NULL, "Put back the values the last load replaced.", cmd_table_revert,
		      1, 0),
	SHELL_CMD_ARG(show, NULL, "Show what is loaded and the counters.", cmd_table_show, 1, 0),
	SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(table, &table_commands, "Parameter tables uploaded as files.", NULL);
