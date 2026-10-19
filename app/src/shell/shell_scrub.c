#include <errno.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/shell/shell.h>

#include <kfsw/services/scrub.h>

static const char *state_name(uint8_t state)
{
	switch (state) {
	case KFSW_SCRUB_EMPTY:
		return "empty";
	case KFSW_SCRUB_UNKNOWN:
		return "unknown";
	case KFSW_SCRUB_OK:
		return "ok";
	case KFSW_SCRUB_MISMATCH:
		return "mismatch";
	case KFSW_SCRUB_UNREADABLE:
		return "unreadable";
	case KFSW_SCRUB_ABSENT:
	default:
		return "absent";
	}
}

static bool print_region(const struct kfsw_scrub_result *result, void *context)
{
	const struct shell *sh = context;

	shell_print(sh, "%-8s  %8u  %08x  %08x  %s", result->name, result->bytes, result->reference,
		    result->measured, state_name(result->state));
	return true;
}

static void print_table(const struct shell *sh)
{
	shell_print(sh, "region       bytes  referenc  measured  state");
	shell_print(sh, "--------  --------  --------  --------  -----");
	kfsw_scrub_visit_regions(print_region, (void *)sh);
}

static int parse_region(const struct shell *sh, const char *text, enum kfsw_scrub_region *region)
{
	static const char *const names[KFSW_SCRUB_REGIONS] = {"slot0", "slot1", "golden"};

	for (unsigned int id = 0U; id < KFSW_SCRUB_REGIONS; id++) {
		if (strcmp(text, names[id]) == 0) {
			*region = (enum kfsw_scrub_region)id;
			return 0;
		}
	}
	shell_error(sh, "unknown region %s; one of slot0, slot1, golden", text);
	return -EINVAL;
}

static int cmd_scrub_status(const struct shell *sh, size_t argc, char **argv)
{
	struct kfsw_scrub_status status;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	kfsw_scrub_get_status(&status);
	shell_print(sh, "state: %s", status.running ? "sweeping" : "stopped");
	shell_print(sh, "regions: %u", status.regions);
	shell_print(sh, "sweeps: %u mismatches: %u", status.sweeps, status.mismatches);
	shell_print(sh, "last_sweep_ms: %u", status.last_sweep_ms);
	print_table(sh);
	return 0;
}

static int cmd_scrub_sweep(const struct shell *sh, size_t argc, char **argv)
{
	enum kfsw_scrub_region region;
	int result;

	if (argc == 2U) {
		result = parse_region(sh, argv[1], &region);
		if (result != 0) {
			return result;
		}
		result = kfsw_scrub_sweep_region(region);
		if (result != 0) {
			shell_error(sh, "scrub sweep %s failed (%d)", argv[1], result);
			return result;
		}
	} else {
		result = kfsw_scrub_sweep();
		if (result < 0) {
			shell_error(sh, "scrub sweep failed (%d)", result);
			return result;
		}
	}
	print_table(sh);
	return 0;
}

static int cmd_scrub_learn(const struct shell *sh, size_t argc, char **argv)
{
	enum kfsw_scrub_region region;
	int result;

	ARG_UNUSED(argc);

	result = parse_region(sh, argv[1], &region);
	if (result != 0) {
		return result;
	}
	result = kfsw_scrub_learn(region);
	if (result != 0) {
		shell_error(sh, "scrub learn %s failed (%d)", argv[1], result);
		return result;
	}
	print_table(sh);
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(
	scrub_commands,
	SHELL_CMD_ARG(status, NULL, "Counters and what each region last reported", cmd_scrub_status,
		      1, 0),
	SHELL_CMD_ARG(sweep, NULL, "Re-read every region, or one: sweep [slot0|slot1|golden]",
		      cmd_scrub_sweep, 1, 1),
	SHELL_CMD_ARG(learn, NULL, "Take what is on flash now as the reference: learn <region>",
		      cmd_scrub_learn, 2, 0),
	SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(scrub, &scrub_commands, "Resident image checksums.", NULL);
