#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/shell/shell.h>
#include <zephyr/sys/util.h>

#include <kfsw/services/health.h>

static int cmd_health_status(const struct shell *sh, size_t argc, char **argv)
{
	struct kfsw_health_status status;
	int result;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	result = kfsw_health_get_status(&status);
	if (result != 0) {
		shell_error(sh, "health status unavailable (%d)", result);
		return result;
	}

	shell_print(sh, "K-FSW health");
	shell_print(sh, "state: %s", kfsw_health_state_name((enum kfsw_health_state)status.state));
	shell_print(sh, "feeding: %s", status.feeding ? "yes" : "no");
	shell_print(sh, "components: %u", status.count);
	shell_print(sh, "faults: %" PRIu32, status.faults);
	shell_print(sh, "feeds: %" PRIu32, status.feeds);
	if (status.faulted_by[0] != '\0') {
		shell_print(sh, "faulted_by: %s", status.faulted_by);
	}
	return 0;
}

static int cmd_health_list(const struct shell *sh, size_t argc, char **argv)
{
	struct kfsw_health_component component;
	uint8_t shown = 0U;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(sh, "%-16s %10s %10s %8s %s", "NAME", "DEADLINE", "SINCE", "REPORTS", "STATE");

	for (uint8_t index = 0U; index < KFSW_HEALTH_MAX_COMPONENTS; index++) {
		if (kfsw_health_get_component(index, &component) != 0) {
			continue;
		}
		shell_print(sh, "%-16s %10" PRIu32 " %10" PRIu32 " %8" PRIu32 " %s", component.name,
			    component.deadline_ms, component.since_report_ms, component.reports,
			    component.overdue ? "overdue" : "ok");
		shown++;
	}

	shell_print(sh, "Components listed: %u", shown);
	return 0;
}

static int cmd_health_watch(const struct shell *sh, size_t argc, char **argv)
{
	uint8_t handle = 0U;
	unsigned long deadline;
	char *end = NULL;
	int result;

	/* Registering something that nothing reports will take the board down
	 * within its deadline plus a watchdog timeout. That is the point of it
	 * -- it is how the recovery chain is exercised with a real fault rather
	 * than a simulated one -- but it is not something to reach by accident,
	 * so it is spelled out.
	 */
	if ((argc != 4U) || (strcmp(argv[3], "confirm") != 0)) {
		shell_error(sh, "This registers a component that nothing reports.");
		shell_error(sh, "The board will reset once its deadline passes.");
		shell_error(sh, "Run: health watch <name> <deadline_ms> confirm");
		return -EINVAL;
	}

	deadline = strtoul(argv[2], &end, 0);
	if ((end == argv[2]) || (*end != '\0') || (deadline == 0UL) || (deadline > UINT32_MAX)) {
		shell_error(sh, "Invalid deadline: %s", argv[2]);
		return -EINVAL;
	}

	result = kfsw_health_register(argv[1], (uint32_t)deadline, &handle);
	if (result != 0) {
		shell_error(sh, "could not watch %s (%d)", argv[1], result);
		return result;
	}

	shell_print(sh, "Watching %s with a %lu ms deadline; nothing is reporting it", argv[1],
		    deadline);
	shell_print(sh, "Report it with: health report %u", handle);
	return 0;
}

static int cmd_health_report(const struct shell *sh, size_t argc, char **argv)
{
	unsigned long handle;
	char *end = NULL;
	int result;

	ARG_UNUSED(argc);

	handle = strtoul(argv[1], &end, 0);
	if ((end == argv[1]) || (*end != '\0') || (handle > UINT8_MAX)) {
		shell_error(sh, "Invalid handle: %s", argv[1]);
		return -EINVAL;
	}

	result = kfsw_health_report((uint8_t)handle);
	if (result != 0) {
		shell_error(sh, "could not report handle %lu (%d)", handle, result);
		return result;
	}

	shell_print(sh, "Reported");
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(
	health_commands,
	SHELL_CMD_ARG(list, NULL, "Show each watched component and its deadline.",
		      cmd_health_list, 1, 0),
	SHELL_CMD_ARG(report, NULL, "Report a component alive: report <handle>.",
		      cmd_health_report, 2, 0),
	SHELL_CMD_ARG(status, NULL, "Show whether the watchdog is being fed, and why.",
		      cmd_health_status, 1, 0),
	SHELL_CMD_ARG(watch, NULL,
		      "Watch a component nothing reports: watch <name> <ms> confirm.",
		      cmd_health_watch, 1, 3),
	SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(health, &health_commands, "K-FSW health monitoring.", NULL);
