#include <errno.h>
#include <string.h>
#include <zephyr/fs/fs.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/ztest.h>
#include <zephyr/fff.h>
#include <stdio.h>
#include <kfsw/comms/csp.h>
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
	zassert_ok(kfsw_csp_init());
	zassert_ok(kfsw_storage_init());
	zassert_ok(kfsw_storage_mount());
	zassert_ok(fs_mkdir("/kfsw/ftp"));
	zassert_ok(fs_mkdir("/kfsw/ftp/limits"));
	zassert_ok(kfsw_param_init(parameter_sets, ARRAY_SIZE(parameter_sets)));
	zassert_ok(kfsw_command_init(command_sets, ARRAY_SIZE(command_sets)));
	zassert_ok(kfsw_fbo_init());
	return NULL;
}

static void reset_remote(void);

static void before(void *fixture)
{
	ARG_UNUSED(fixture);
	reset_remote();
	kfsw_limits_interval_ms = 0U;
	counter = 0U;
	temperature = 0;
	ratio = 0.0f;
	kfsw_limits_enabled = 1U;
	(void)kfsw_fbo_schedule_clear();
	zassert_ok(load("# nothing\n"));
}

ZTEST_SUITE(limits_remote, NULL, setup, before, NULL, NULL);

DEFINE_FFF_GLOBALS;
FAKE_VALUE_FUNC(int, __wrap_kfsw_param_remote_get_many_until, uint16_t, const char *const *, size_t,
		struct kfsw_param_value *, int64_t);

static enum kfsw_param_type remote_type;
static uint32_t remote_value;
static bool exhaust_deadline;
static int64_t offered_ms[20];
static k_tid_t read_thread;
static K_SEM_DEFINE(remote_entered, 0, 1);

static int read_values(uint16_t node, const char *const *names, size_t count,
		       struct kfsw_param_value *values, int64_t deadline)
{
	ARG_UNUSED(node);
	unsigned int call = __wrap_kfsw_param_remote_get_many_until_fake.call_count - 1U;

	offered_ms[call] = deadline - k_uptime_get();
	read_thread = k_current_get();
	if (exhaust_deadline) {
		k_sleep(K_TIMEOUT_ABS_MS(deadline));
		return -ETIMEDOUT;
	}
	for (size_t i = 0; i < count; i++) {
		zassert_true(names[i][0] != '\0');
		values[i].type = remote_type;
		values[i].scalar.u32 = remote_value;
		if (remote_type == KFSW_PARAM_FLOAT) {
			values[i].scalar.f32 = (float)remote_value;
		}
	}
	k_sem_give(&remote_entered);
	return 0;
}

static void reset_remote(void)
{
	RESET_FAKE(__wrap_kfsw_param_remote_get_many_until);
	FFF_RESET_HISTORY();
	remote_type = KFSW_PARAM_U32;
	remote_value = 11U;
	exhaust_deadline = false;
	memset(offered_ms, 0, sizeof(offered_ms));
	read_thread = NULL;
	k_sem_reset(&remote_entered);
	__wrap_kfsw_param_remote_get_many_until_fake.custom_fake = read_values;
}

ZTEST(limits_remote, test_prefix_and_action_node_are_separate)
{
	zassert_ok(load("12:t_x > 5 1 0 count\n"));
	struct kfsw_limits_rule rule = first_rule();

	zassert_equal(rule.read_node, 12U);
	zassert_equal(rule.node, 0U);
	zassert_equal(rule.type, KFSW_PARAM_INVALID);
	zassert_str_equal(rule.parameter, "t_x");
	zassert_equal(__wrap_kfsw_param_remote_get_many_until_fake.call_count, 0U);
}

ZTEST(limits_remote, test_zero_and_own_address_are_local)
{
	const char *texts[] = {"0:t_counter > 10 1 0 count\n", "7:t_counter > 10 1 0 count\n",
			       "t_counter > 10 1 0 count\n"};

	counter = 11U;
	for (size_t i = 0; i < ARRAY_SIZE(texts); i++) {
		(void)kfsw_fbo_schedule_clear();
		zassert_ok(load(texts[i]));
		zassert_equal(first_rule().read_node, 0U);
		uint32_t fires = status().fires;
		zassert_equal(kfsw_limits_sweep(), 1);
		zassert_equal(status().fires, fires + 1U);
	}
	zassert_equal(__wrap_kfsw_param_remote_get_many_until_fake.call_count, 0U);
}

ZTEST(limits_remote, test_bad_prefix_refuses_the_whole_file)
{
	const char *prefixes[] = {"16383:", "16384:", "abc:t_x", "-1:t_x", ":x", "12:", "1:2:x"};
	char text[128];

	zassert_ok(load("t_counter > 10 1 0 count\n"));
	for (size_t i = 0; i < ARRAY_SIZE(prefixes); i++) {
		/* Numeric prefixes with no name and prefixes with another colon. */
		(void)snprintf(text, sizeof(text), "%s > 5 1 0 count\n", prefixes[i]);
		zassert_equal(load(text), -EINVAL, "%s", text);
		zassert_str_equal(first_rule().parameter, "t_counter");
	}
	expect_refused("12:t_x >\n", 1U,
		       "expected parameter, comparison, bound, dwell, node and an action");
	zassert_equal(load("16383:t_x > 5 1 0 count\n"), -EINVAL);
	zassert_equal(load("16384:t_x > 5 1 0 count\n"), -EINVAL);
	zassert_equal(load("12:t_x == 1.5 1 0 count\n"), -EINVAL);
	zassert_equal(load("12:t_x != 1.5 1 0 count\n"), -EINVAL);
	zassert_equal(load("12:t_x > bad 1 0 count\n"), -EINVAL);
}

ZTEST(limits_remote, test_remote_comparison_dwell_latch_and_rearm)
{
	zassert_ok(load("12:t_x > 5 2 0 count\n"));
	uint32_t fires = status().fires;

	zassert_equal(kfsw_limits_sweep(), 1);
	zassert_equal(status().fires, fires);
	zassert_equal(first_rule().type, KFSW_PARAM_U32);
	zassert_equal(kfsw_limits_sweep(), 1);
	zassert_equal(status().fires, fires + 1U);
	zassert_equal(queued(), 1U);
	zassert_true(first_rule().latched);
	(void)kfsw_limits_sweep();
	zassert_equal(status().fires, fires + 1U);
	remote_value = 0U;
	(void)kfsw_limits_sweep();
	zassert_false(first_rule().latched);
	(void)kfsw_fbo_schedule_clear();
	remote_value = 11U;
	(void)kfsw_limits_sweep();
	(void)kfsw_limits_sweep();
	zassert_equal(status().fires, fires + 2U);
}

ZTEST(limits_remote, test_timeout_fires_nothing_and_publishes_failures)
{
	zassert_ok(load("12:t_x > 5 1 0 count\n"));
	struct kfsw_limits_status old = status();
	struct kfsw_param_value value;

	__wrap_kfsw_param_remote_get_many_until_fake.custom_fake = NULL;
	__wrap_kfsw_param_remote_get_many_until_fake.return_val = -ETIMEDOUT;
	for (unsigned int i = 1; i <= 2; i++) {
		zassert_equal(kfsw_limits_sweep(), 1);
		zassert_equal(status().fires, old.fires);
		zassert_equal(queued(), 0U);
		zassert_equal(status().failures, old.failures + i);
		zassert_ok(kfsw_param_get("limits_remote_failures", &value));
		zassert_equal(value.type, KFSW_PARAM_U32);
		zassert_equal(value.scalar.u32, old.remote_failures + i);
		zassert_ok(kfsw_param_get("limits_failed_by", &value));
		zassert_equal(value.type, KFSW_PARAM_U16);
		zassert_equal(value.scalar.u16, 12U);
	}
	printk("timeout: fires unchanged, queued=0, remote_failures +2, failed_by=12\n");
}

ZTEST(limits_remote, test_failed_read_keeps_latch_and_breaks_dwell)
{
	zassert_ok(load("12:t_x > 5 2 0 count\n"));
	uint32_t fires = status().fires;

	(void)kfsw_limits_sweep();
	__wrap_kfsw_param_remote_get_many_until_fake.custom_fake = NULL;
	__wrap_kfsw_param_remote_get_many_until_fake.return_val = -EIO;
	(void)kfsw_limits_sweep();
	zassert_equal(first_rule().streak, 0U);
	__wrap_kfsw_param_remote_get_many_until_fake.custom_fake = read_values;
	(void)kfsw_limits_sweep();
	zassert_equal(status().fires, fires);
	(void)kfsw_limits_sweep();
	zassert_equal(status().fires, fires + 1U);
	(void)kfsw_fbo_schedule_clear();
	__wrap_kfsw_param_remote_get_many_until_fake.custom_fake = NULL;
	(void)kfsw_limits_sweep();
	zassert_true(first_rule().latched);
	__wrap_kfsw_param_remote_get_many_until_fake.custom_fake = read_values;
	(void)kfsw_limits_sweep();
	(void)kfsw_limits_sweep();
	zassert_equal(status().fires, fires + 1U);
	zassert_equal(queued(), 0U);
	zassert_equal(status().failed_by, 0U);
}

ZTEST(limits_remote, test_three_rules_are_one_group)
{
	zassert_ok(load("12:a > 5 1 0 count\n12:b > 5 1 0 count\n12:c > 5 1 0 count\n"));
	zassert_equal(kfsw_limits_sweep(), 3);
	zassert_equal(__wrap_kfsw_param_remote_get_many_until_fake.call_count, 1U);
	zassert_equal(__wrap_kfsw_param_remote_get_many_until_fake.arg2_val, 3U);
	zassert_equal(__wrap_kfsw_param_remote_get_many_until_fake.arg0_val, 12U);
}

static void many_rules(size_t count, bool distinct_nodes)
{
	char text[1024];
	size_t used = 0U;

	for (size_t i = 0; i < count; i++) {
		used += (size_t)snprintf(text + used, sizeof(text) - used, "%u:r%u > 5 1 0 count\n",
					 distinct_nodes ? (unsigned int)i + 20U : 12U,
					 (unsigned int)i);
	}
	zassert_ok(load(text));
}

ZTEST(limits_remote, test_seventeen_rules_use_two_bounded_groups)
{
	many_rules(17U, false);
	zassert_equal(kfsw_limits_sweep(), 17);
	zassert_equal(__wrap_kfsw_param_remote_get_many_until_fake.call_count, 2U);
	zassert_equal(__wrap_kfsw_param_remote_get_many_until_fake.arg2_history[0], 16U);
	zassert_equal(__wrap_kfsw_param_remote_get_many_until_fake.arg2_history[1], 1U);
}

ZTEST(limits_remote, test_three_dead_nodes_each_get_a_deadline)
{
	many_rules(3U, true);
	exhaust_deadline = true;
	kfsw_limits_interval_ms = 300U;
	int64_t start = k_uptime_get();

	zassert_equal(kfsw_limits_sweep(), 3);
	int64_t elapsed = k_uptime_get() - start;
	zassert_equal(__wrap_kfsw_param_remote_get_many_until_fake.call_count, 3U);
	for (unsigned int i = 0; i < 3; i++) {
		zassert_equal(__wrap_kfsw_param_remote_get_many_until_fake.arg0_history[i],
			      20U + i);
		zassert_between_inclusive(offered_ms[i], 10, 55);
	}
	zassert_true(elapsed <= 155, "150 ms share plus 5 ms scheduling tolerance: %lld", elapsed);
	printk("three dead nodes: %lld ms, offered %lld/%lld/%lld ms\n", elapsed, offered_ms[0],
	       offered_ms[1], offered_ms[2]);
}

ZTEST(limits_remote, test_unfinished_rules_resume_next_sweep)
{
	many_rules(8U, true);
	exhaust_deadline = true;
	kfsw_limits_interval_ms = 100U;
	uint32_t failures = status().remote_failures;
	int evaluated = kfsw_limits_sweep();

	zassert_true(evaluated > 0 && evaluated < 8);
	zassert_equal(status().remote_failures - failures, evaluated);
	unsigned int calls = __wrap_kfsw_param_remote_get_many_until_fake.call_count;
	(void)kfsw_limits_sweep();
	zassert_equal(__wrap_kfsw_param_remote_get_many_until_fake.arg0_history[calls],
		      20U + (unsigned int)evaluated);
}

ZTEST(limits_remote, test_late_binding_refuses_float_equality_and_unfit_bound)
{
	const char *texts[] = {"12:t_x == 5 1 0 count\n", "12:t_x != 5 1 0 count\n",
			       "12:t_x > 256 1 0 count\n", "12:t_x > 1 1 0 count\n",
			       "12:t_x != 1 1 0 count\n"};
	const enum kfsw_param_type types[] = {KFSW_PARAM_FLOAT, KFSW_PARAM_FLOAT, KFSW_PARAM_U8,
					      KFSW_PARAM_STRING, KFSW_PARAM_INVALID};

	for (size_t i = 0; i < ARRAY_SIZE(texts); i++) {
		zassert_ok(load(texts[i]));
		struct kfsw_limits_status old = status();
		remote_type = types[i];
		(void)kfsw_limits_sweep();
		zassert_equal(status().fires, old.fires);
		zassert_equal(status().failures, old.failures + 1U);
		zassert_equal(status().remote_failures, old.remote_failures);
		zassert_equal(first_rule().streak, 0U);
		zassert_equal(first_rule().type, KFSW_PARAM_INVALID);
		zassert_equal(queued(), 0U);
	}
}

ZTEST(limits_remote, test_type_change_rebinds_the_bound)
{
	zassert_ok(load("12:t_x > 5 2 0 count\n"));
	(void)kfsw_limits_sweep();
	zassert_equal(first_rule().bound.u32, 5U);
	remote_type = KFSW_PARAM_FLOAT;
	(void)kfsw_limits_sweep();
	zassert_equal(first_rule().type, KFSW_PARAM_FLOAT);
	zassert_equal(first_rule().bound.f32, 5.0f);
	zassert_true(first_rule().latched);
}

ZTEST(limits_remote, test_failed_by_is_watchable_next_sweep_and_clears_on_recovery)
{
	zassert_ok(load("12:t_x > 5 1 0 count\nlimits_failed_by == 12 1 0 level 3\n"));
	uint32_t fires = status().fires;

	__wrap_kfsw_param_remote_get_many_until_fake.custom_fake = NULL;
	__wrap_kfsw_param_remote_get_many_until_fake.return_val = -ETIMEDOUT;
	(void)kfsw_limits_sweep();
	zassert_equal(status().fires, fires);
	(void)kfsw_limits_sweep();
	zassert_equal(status().fires, fires + 1U);
	__wrap_kfsw_param_remote_get_many_until_fake.custom_fake = read_values;
	remote_value = 0U;
	(void)kfsw_limits_sweep();
	zassert_equal(status().failed_by, 0U);
	(void)kfsw_fbo_schedule_clear();
	(void)kfsw_limits_sweep();
	__wrap_kfsw_param_remote_get_many_until_fake.custom_fake = NULL;
	(void)kfsw_limits_sweep();
	(void)kfsw_limits_sweep();
	zassert_equal(status().fires, fires + 2U);
	zassert_ok(load("# empty\n"));
	zassert_equal(status().failed_by, 0U);
}

ZTEST(limits_remote, test_periodic_remote_read_uses_a_dedicated_queue)
{
	zassert_ok(kfsw_limits_init());
	zassert_ok(load("12:t_x > 5 1 0 count\n"));
	zassert_ok(kfsw_limits_set_interval_ms(100U));
	zassert_ok(k_sem_take(&remote_entered, K_SECONDS(1)));
	zassert_ok(kfsw_limits_set_interval_ms(0U));
	/* Wait for the handler to finish before the next test resets its fakes. */
	k_sleep(K_MSEC(20));
	zassert_not_equal(read_thread, &k_sys_work_q.thread);
	zassert_not_equal(read_thread, k_current_get());
}

ZTEST(limits_remote, test_group_failure_counts_each_rule)
{
	zassert_ok(load("12:a > 5 1 0 count\n12:b > 5 1 0 count\n12:c > 5 1 0 count\n"));
	struct kfsw_limits_status old = status();

	__wrap_kfsw_param_remote_get_many_until_fake.custom_fake = NULL;
	__wrap_kfsw_param_remote_get_many_until_fake.return_val = -ETIMEDOUT;
	zassert_equal(kfsw_limits_sweep(), 3);
	zassert_equal(status().failures, old.failures + 3U);
	zassert_equal(status().remote_failures, old.remote_failures + 3U);
	zassert_equal(status().fires, old.fires);
	zassert_equal(queued(), 0U);
}
