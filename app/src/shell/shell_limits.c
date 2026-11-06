#include <errno.h>
#include <stdbool.h>
#include <stdint.h>

#include <zephyr/shell/shell.h>

#include <kfsw/services/limits.h>

static int cmd_limits_status(const struct shell *sh, size_t argc, char **argv)
{
	struct kfsw_limits_status status;
	const char *state = "manual";

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	kfsw_limits_get_status(&status);
	if (!kfsw_limits_enabled) {
		state = "disabled";
	} else if (status.running) {
		state = "sweeping";
	}
	shell_print(sh, "state: %s", state);
	shell_print(sh, "rules: %u", status.rules);
	shell_print(sh, "interval: %u ms", kfsw_limits_interval_ms);
	shell_print(sh, "sweeps: %u fires: %u failures: %u", status.sweeps, status.fires,
		    status.failures);
	if (status.last_fired_index == KFSW_LIMITS_NO_RULE) {
		shell_print(sh, "last_fired: none");
	} else {
		shell_print(sh, "last_fired: rule %u at %u ms", status.last_fired_index,
			    status.last_fired_ms);
	}
	shell_print(sh, "parse_errors: %u", status.parse_errors);
	if (status.error_line != 0U) {
		shell_print(sh, "last_error: line %u: %s", status.error_line, status.error);
	}
	return 0;
}

static bool print_rule(const struct kfsw_limits_rule *rule, void *context)
{
	const struct shell *sh = context;

	shell_print(sh, "%3u  %-24s %-2s %-12s %5u %5u  %-7s %s", rule->index, rule->parameter,
		    kfsw_limits_cmp_text(rule->cmp), rule->bound_text, rule->dwell, rule->node,
		    rule->latched ? "fired" : "armed", rule->action);
	return true;
}

static int cmd_limits_list(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(sh, "%3s  %-24s %-2s %-12s %5s %5s  %-7s %s", "idx", "parameter", "op", "bound",
		    "dwell", "node", "state", "action");
	kfsw_limits_visit_rules(print_rule, (void *)sh);
	return 0;
}

static int cmd_limits_load(const struct shell *sh, size_t argc, char **argv)
{
	const char *name = (argc > 1) ? argv[1] : CONFIG_KFSW_LIMITS_FILE;
	struct kfsw_limits_status status;
	int result = kfsw_limits_load(name);

	if (result == -EINVAL) {
		kfsw_limits_get_status(&status);
		if (status.error_line != 0U) {
			shell_error(sh, "%s refused, line %u: %s", name, status.error_line,
				    status.error);
			return result;
		}
	}
	if (result != 0) {
		shell_error(sh, "Load failed: %d", result);
		return result;
	}

	kfsw_limits_get_status(&status);
	shell_print(sh, "Loaded %u rules from %s", status.rules, name);
	return 0;
}

static int cmd_limits_sweep(const struct shell *sh, size_t argc, char **argv)
{
	int rules;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	rules = kfsw_limits_sweep();
	if (rules < 0) {
		shell_error(sh, "Sweep refused: %d", rules);
		return rules;
	}
	shell_print(sh, "Evaluated %d rules", rules);
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(limits_commands,
	SHELL_CMD_ARG(list, NULL, "List the rules in force", cmd_limits_list, 1, 0),
	SHELL_CMD_ARG(load, NULL, "Load a rule file: load [file]", cmd_limits_load, 1, 1),
	SHELL_CMD_ARG(status, NULL, "Show counters and the last load result", cmd_limits_status,
		      1, 0),
	SHELL_CMD_ARG(sweep, NULL, "Evaluate the rules now", cmd_limits_sweep, 1, 0),
	SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(limits, &limits_commands, "Parameter limits.", NULL);
