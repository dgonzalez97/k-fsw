#include <errno.h>
#include <string.h>
#include <zephyr/fs/fs.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/ztest.h>
#include <kfsw/platform/storage.h>
#include <kfsw/services/command.h>
#include <kfsw/services/fbo.h>
#include <kfsw/services/limits.h>
#include <kfsw/services/parameter.h>

#define RULE_FILE "t.txt"
#define RULE_PATH "/kfsw/ftp/limits/" RULE_FILE

static uint32_t counter;
static int16_t temperature;
static float ratio;
static char label[8];
static atomic_t commands;

static const struct kfsw_param_definition test_definitions[] = {
	{
		.offset = 0x00U,
		.type = KFSW_PARAM_U32,
		.name = "t_counter",
		.value = &counter,
	},
	{
		.offset = 0x04U,
		.type = KFSW_PARAM_I16,
		.name = "t_temp",
		.value = &temperature,
	},
	{
		.offset = 0x08U,
		.type = KFSW_PARAM_FLOAT,
		.name = "t_ratio",
		.value = &ratio,
	},
	{
		.offset = 0x10U,
		.type = KFSW_PARAM_STRING,
		.capacity = sizeof(label),
		.name = "t_label",
		.value = label,
	},
};

static const struct kfsw_param_definition_set test_set = {
	.table = 45U,
	.name = "limtest",
	.definitions = test_definitions,
	.count = ARRAY_SIZE(test_definitions),
};

static int handler(const struct kfsw_command_arg *args, size_t count,
		   const struct kfsw_command_source *source, struct kfsw_command_result *result)
{
	ARG_UNUSED(args);
	ARG_UNUSED(count);
	ARG_UNUSED(source);
	atomic_inc(&commands);
	result->status = KFSW_COMMAND_OK;
	return 0;
}

static void write_rules(const char *text)
{
	struct fs_file_t file;

	fs_file_t_init(&file);
	zassert_ok(fs_open(&file, RULE_PATH, FS_O_CREATE | FS_O_WRITE | FS_O_TRUNC));
	zassert_equal(fs_write(&file, text, strlen(text)), strlen(text));
	zassert_ok(fs_close(&file));
}

static int load(const char *text)
{
	write_rules(text);
	return kfsw_limits_load(RULE_FILE);
}

static struct kfsw_limits_status status(void)
{
	struct kfsw_limits_status current;

	kfsw_limits_get_status(&current);
	return current;
}

static uint16_t queued(void)
{
	struct kfsw_fbo_schedule_status schedule;

	zassert_ok(kfsw_fbo_schedule_get_status(&schedule));
	return schedule.entries;
}

static bool capture(const struct kfsw_limits_rule *rule, void *context)
{
	memcpy(context, rule, sizeof(*rule));
	return false;
}

static struct kfsw_limits_rule first_rule(void)
{
	struct kfsw_limits_rule rule = {0};

	kfsw_limits_visit_rules(capture, &rule);
	return rule;
}

static void expect_refused(const char *text, uint16_t line, const char *reason)
{
	struct kfsw_limits_status before = status();
	struct kfsw_limits_status after;

	zassert_equal(load(text), -EINVAL);
	after = status();
	zassert_equal(after.error_line, line, "line %u", after.error_line);
	zassert_str_equal(after.error, reason);
	zassert_equal(after.parse_errors, before.parse_errors + 1U);
	zassert_equal(after.rules, before.rules, "the previous set must stay in force");
}

static void *setup(void)
{
	static const enum kfsw_command_type level_args[] = {KFSW_COMMAND_TYPE_U32};
	static const struct kfsw_command_definition definitions[] = {
		{.id = 1, .name = "count", .handler = handler},
		{
			.id = 2,
			.name = "level",
			.arg_count = 1,
			.arg_types = level_args,
			.handler = handler,
		},
	};
	static const struct kfsw_command_definition_set commands_set = {
		.commands = definitions,
		.count = ARRAY_SIZE(definitions),
	};
	static const struct kfsw_param_definition_set *const parameter_sets[] = {
		&test_set, &kfsw_limits_param_definitions};
	const struct kfsw_command_definition_set *command_sets[] = {&commands_set};
	const struct flash_area *area;

	zassert_ok(
		flash_area_open(DT_FIXED_PARTITION_ID(DT_CHOSEN(kfsw_storage_partition)), &area));
	zassert_ok(flash_area_flatten(area, 0, area->fa_size));
	flash_area_close(area);
	zassert_ok(kfsw_storage_init());
	zassert_ok(kfsw_storage_mount());
	zassert_ok(fs_mkdir("/kfsw/ftp"));
	zassert_ok(fs_mkdir("/kfsw/ftp/limits"));
	zassert_ok(kfsw_param_init(parameter_sets, ARRAY_SIZE(parameter_sets)));
	zassert_ok(kfsw_command_init(command_sets, ARRAY_SIZE(command_sets)));
	zassert_ok(kfsw_fbo_init());
	return NULL;
}

static void before(void *fixture)
{
	ARG_UNUSED(fixture);
	counter = 0U;
	temperature = 0;
	ratio = 0.0f;
	kfsw_limits_enabled = 1U;
	(void)kfsw_fbo_schedule_clear();
	zassert_ok(load("# nothing\n"));
}

ZTEST_SUITE(limits, NULL, setup, before, NULL, NULL);

ZTEST(limits, test_init_without_a_file_is_not_a_failure)
{
	zassert_ok(kfsw_limits_init());
	zassert_equal(status().rules, 0U);
	zassert_false(status().running, "an interval of zero schedules no sweep");
}

ZTEST(limits, test_a_valid_file_loads_one_rule)
{
	struct kfsw_limits_rule rule;

	zassert_ok(load("# parameter cmp bound dwell node action\n"
			"t_counter > 10 2 0 count\n"));
	zassert_equal(status().rules, 1U);
	zassert_equal(status().error_line, 0U);

	rule = first_rule();
	zassert_str_equal(rule.parameter, "t_counter");
	zassert_equal(rule.cmp, KFSW_LIMITS_GT);
	zassert_equal(rule.bound.u32, 10U);
	zassert_equal(rule.dwell, 2U);
	zassert_equal(rule.node, 0U);
}

ZTEST(limits, test_the_action_keeps_its_arguments_and_spaces)
{
	zassert_ok(load("t_counter >= 5 1 0   level   7  \n"));
	zassert_str_equal(first_rule().action, "level   7");
}

ZTEST(limits, test_a_missing_file_or_a_path_is_refused)
{
	zassert_equal(kfsw_limits_load("absent.txt"), -ENOENT);
	zassert_equal(kfsw_limits_load("../t.txt"), -EINVAL);
	zassert_equal(kfsw_limits_load(NULL), -EINVAL);
}

ZTEST(limits, test_an_unknown_parameter_leaves_the_previous_set_in_force)
{
	zassert_ok(load("t_counter > 10 1 0 count\n"));
	expect_refused("t_counter > 10 1 0 count\n"
		       "# second line is fine\n"
		       "nosuch > 1 1 0 count\n",
		       3U, "unknown parameter");
	zassert_str_equal(first_rule().parameter, "t_counter");
}

ZTEST(limits, test_a_malformed_comparison_names_its_line)
{
	expect_refused("# header\n\nt_counter >> 3 1 0 count\n", 3U, "unknown comparison");
}

ZTEST(limits, test_an_action_that_is_not_a_command_is_a_parse_error)
{
	expect_refused("t_counter > 1 1 0 selfdestruct\n", 1U,
		       "action is not a registered command");
	expect_refused("t_counter > 1 1 0 level\n", 1U,
		       "action argument is missing or of the wrong type");
	expect_refused("t_counter > 1 1 0 level seven\n", 1U,
		       "action argument is missing or of the wrong type");
	expect_refused("t_counter > 1 1 0 level 1 2\n", 1U,
		       "action has more arguments than the command takes");
}

ZTEST(limits, test_fields_are_checked_against_the_parameter)
{
	expect_refused("t_label > 1 1 0 count\n", 1U, "parameter is not a number");
	expect_refused("t_ratio == 1.0 1 0 count\n", 1U,
		       "equality is refused for a float parameter");
	expect_refused("t_counter > 1.5 1 0 count\n", 1U,
		       "bound does not fit the type of the parameter");
	expect_refused("t_counter > -1 1 0 count\n", 1U,
		       "bound does not fit the type of the parameter");
	expect_refused("t_counter > 1 0 0 count\n", 1U, "dwell must be 1 to 255");
	expect_refused("t_counter > 1 256 0 count\n", 1U, "dwell must be 1 to 255");
	expect_refused("t_counter > 1 1 x count\n", 1U, "node is not an address");
	expect_refused("t_counter > 1 1 0\n", 1U,
		       "expected parameter, comparison, bound, dwell, node and an action");
	zassert_ok(load("t_temp < -5 1 0 count\nt_ratio > 0.5 1 0 count\n"));
	zassert_equal(status().rules, 2U);
}

ZTEST(limits, test_more_rules_than_the_build_holds_is_refused)
{
	expect_refused("t_counter > 1 1 0 count\nt_counter > 2 1 0 count\n"
		       "t_counter > 3 1 0 count\nt_counter > 4 1 0 count\n"
		       "t_counter > 5 1 0 count\n",
		       5U, "more rules than this build holds");
}

ZTEST(limits, test_a_crossing_fires_once_until_the_comparison_goes_false)
{
	uint32_t fires;

	zassert_ok(load("t_counter > 10 1 0 count\n"));
	fires = status().fires;

	counter = 5U;
	zassert_equal(kfsw_limits_sweep(), 1);
	zassert_equal(status().fires, fires);

	counter = 11U;
	zassert_equal(kfsw_limits_sweep(), 1);
	zassert_equal(status().fires, fires + 1U);
	zassert_equal(status().last_fired_index, 0U);
	zassert_equal(queued(), 1U);
	zassert_true(first_rule().latched);

	zassert_equal(kfsw_limits_sweep(), 1);
	zassert_equal(kfsw_limits_sweep(), 1);
	zassert_equal(status().fires, fires + 1U, "latched, so it must not fire again");

	counter = 3U;
	(void)kfsw_limits_sweep();
	zassert_false(first_rule().latched);
	(void)kfsw_fbo_schedule_clear();

	counter = 12U;
	(void)kfsw_limits_sweep();
	zassert_equal(status().fires, fires + 2U);
}

ZTEST(limits, test_a_dwell_of_three_fires_on_the_third_sweep)
{
	uint32_t fires;

	zassert_ok(load("t_counter > 10 3 0 count\n"));
	fires = status().fires;

	counter = 20U;
	(void)kfsw_limits_sweep();
	(void)kfsw_limits_sweep();
	zassert_equal(status().fires, fires, "two sweeps are not three");

	counter = 0U;
	(void)kfsw_limits_sweep();
	zassert_equal(first_rule().streak, 0U, "a sweep inside the bound breaks the run");

	counter = 20U;
	(void)kfsw_limits_sweep();
	(void)kfsw_limits_sweep();
	zassert_equal(status().fires, fires);
	(void)kfsw_limits_sweep();
	zassert_equal(status().fires, fires + 1U);
}

ZTEST(limits, test_signed_and_float_parameters_compare_in_their_own_type)
{
	uint32_t fires;

	zassert_ok(load("t_temp < -5 1 0 count\nt_ratio >= 0.5 1 0 level 3\n"));
	fires = status().fires;

	temperature = 0;
	ratio = 0.25f;
	(void)kfsw_limits_sweep();
	zassert_equal(status().fires, fires);

	temperature = -6;
	ratio = 0.5f;
	(void)kfsw_limits_sweep();
	zassert_equal(status().fires, fires + 2U);
}

ZTEST(limits, test_disabling_stops_the_sweeps_and_keeps_the_rules)
{
	struct kfsw_limits_status before;

	zassert_ok(load("t_counter > 10 2 0 count\n"));
	counter = 20U;
	(void)kfsw_limits_sweep();
	before = status();

	kfsw_limits_enabled = 0U;
	zassert_equal(kfsw_limits_sweep(), -EACCES);
	zassert_equal(status().sweeps, before.sweeps);
	zassert_equal(status().rules, 1U, "disabling must not unload the rules");
	zassert_equal(first_rule().streak, 0U, "a disabled sweep breaks the run");

	kfsw_limits_enabled = 1U;
	zassert_equal(kfsw_limits_sweep(), 1);
	zassert_equal(status().fires, before.fires, "dwell starts again after enabling");
	(void)kfsw_limits_sweep();
	zassert_equal(status().fires, before.fires + 1U);
}

ZTEST(limits, test_the_table_reports_what_the_service_counted)
{
	struct kfsw_param_value value;

	zassert_ok(load("t_counter > 10 1 0 count\n"));
	zassert_ok(kfsw_param_get("limits_rules", &value));
	zassert_equal(value.scalar.u8, 1U);

	counter = 11U;
	(void)kfsw_limits_sweep();
	zassert_ok(kfsw_param_get("limits_fires", &value));
	zassert_equal(value.scalar.u32, status().fires);
	zassert_ok(kfsw_param_get("limits_last_fired", &value));
	zassert_equal(value.scalar.u8, 0U);
}
