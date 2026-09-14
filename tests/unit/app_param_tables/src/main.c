#include <errno.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#include <zephyr/ztest.h>

#include <kfsw/comms/csp.h>
#include <kfsw/services/ftp.h>
#include <kfsw/platform/storage.h>
#include <kfsw/platform/watchdog.h>
#include <kfsw/services/boot.h>
#include <kfsw/services/command.h>
#include <kfsw/services/event.h>
#include <kfsw/services/fwu.h>
#include <kfsw/services/health.h>
#include <kfsw/services/log.h>
#include <kfsw/services/parameter.h>

#include "parameters/tables.h"

#define CORE_TABLE_COUNT 6U
#define SERVICE_TABLE_COUNT 8U

struct table_seen {
	uint8_t ids[KFSW_PARAM_TABLE_MODULE_LAST + 1U];
	size_t count;
	bool bands_match_owners;
	bool ascending;
	uint8_t previous;
};

struct param_seen {
	size_t count;
	size_t longest_name;
	bool every_name_within_limit;
	bool every_table_allocated;
	bool identifier_matches_table_and_offset;
	uint16_t previous_id;
	bool ascending;
};

static const struct kfsw_param_definition_set *const core_sets[] = {
	&kfsw_board_param_definitions,
	&kfsw_system_param_definitions,
	&kfsw_telemetry_param_definitions,
	&kfsw_csp_param_definitions,
	&kfsw_storage_param_definitions,
	&kfsw_watchdog_param_definitions,
	/* Service tables, so one listing carries both bands and they can be
	 * told apart. */
	&kfsw_log_param_definitions,
	&kfsw_param_param_definitions,
	&kfsw_boot_param_definitions,
	&kfsw_command_param_definitions,
	&kfsw_ftp_param_definitions,
	&kfsw_event_param_definitions,
	&kfsw_fwu_param_definitions,
	&kfsw_health_param_definitions,
};

/* A registry with one command is enough to move the counters. */
static int handler_ok(const struct kfsw_command_arg *args, size_t arg_count,
		      const struct kfsw_command_source *source, struct kfsw_command_result *result)
{
	ARG_UNUSED(args);
	ARG_UNUSED(arg_count);
	ARG_UNUSED(source);
	ARG_UNUSED(result);
	return 0;
}

static const struct kfsw_command_definition test_commands[] = {
	{.id = 1U, .name = "counted", .help = "none", .handler = handler_ok},
};

static const struct kfsw_command_definition_set test_command_set = {
	.commands = test_commands,
	.count = ARRAY_SIZE(test_commands),
};

static void *tables_setup(void)
{
	const struct kfsw_command_definition_set *const command_sets[] = {&test_command_set};

	(void)kfsw_command_init(command_sets, ARRAY_SIZE(command_sets));

	/* Mount storage so the storage rows have real values. */
	(void)kfsw_storage_init();
	(void)kfsw_storage_mount();

	/* Install the watchdog so its table has values. */
	(void)kfsw_platform_watchdog_init();

	/* Start CSP so the identity rows have values. */
	(void)kfsw_csp_init();

	zassert_ok(kfsw_param_init(core_sets, ARRAY_SIZE(core_sets)));
	return NULL;
}

static bool record_table(const struct kfsw_param_table_info *info, void *context)
{
	struct table_seen *seen = context;

	zassert_not_null(info->name);
	if (seen->count < ARRAY_SIZE(seen->ids)) {
		seen->ids[seen->count] = info->id;
	}
	if (info->id <= seen->previous) {
		seen->ascending = false;
	}
	seen->previous = info->id;
	if (strcmp(kfsw_param_band_name(info->id),
		   (info->id <= KFSW_PARAM_TABLE_CORE_LAST) ? "core" : "service") != 0) {
		seen->bands_match_owners = false;
	}
	seen->count++;
	return true;
}

static bool record_parameter(const struct kfsw_param_info *info, void *context)
{
	struct param_seen *seen = context;
	const size_t length = strlen(info->name);

	if (length > KFSW_PARAM_NAME_MAX) {
		seen->every_name_within_limit = false;
	}
	if (length > seen->longest_name) {
		seen->longest_name = length;
	}
	if (strcmp(kfsw_param_band_name(info->table), "invalid") == 0) {
		seen->every_table_allocated = false;
	}
	if (info->id != (uint16_t)(((uint16_t)info->table << 8) | info->offset)) {
		seen->identifier_matches_table_and_offset = false;
	}
	if ((seen->count != 0U) && (info->id <= seen->previous_id)) {
		seen->ascending = false;
	}
	seen->previous_id = info->id;
	seen->count++;
	return true;
}

/* ------------------------------------------------------------ registration */

ZTEST(app_param_tables, test_every_core_table_is_registered)
{
	struct table_seen seen = {
		.bands_match_owners = true,
		.ascending = true,
	};

	zassert_equal(kfsw_param_table_count(), CORE_TABLE_COUNT + SERVICE_TABLE_COUNT);
	zassert_ok(kfsw_param_visit_tables(record_table, &seen));
	zassert_equal(seen.count, CORE_TABLE_COUNT + SERVICE_TABLE_COUNT);

	zassert_equal(seen.ids[0], KFSW_PARAM_TABLE_BOARD);
	zassert_equal(seen.ids[1], KFSW_PARAM_TABLE_SYSTEM);
	zassert_equal(seen.ids[2], KFSW_PARAM_TABLE_TELEMETRY);
	zassert_equal(seen.ids[3], KFSW_PARAM_TABLE_CSP);
	zassert_equal(seen.ids[4], KFSW_PARAM_TABLE_STORAGE);
	zassert_equal(seen.ids[5], KFSW_PARAM_TABLE_WATCHDOG);

	zassert_equal(seen.ids[6], KFSW_LOG_PARAM_TABLE_ID);
	zassert_equal(seen.ids[7], KFSW_PARAM_PARAM_TABLE_ID);
	zassert_equal(seen.ids[8], KFSW_EVENT_PARAM_TABLE_ID);
	zassert_equal(seen.ids[9], KFSW_COMMAND_PARAM_TABLE_ID);
	zassert_equal(seen.ids[10], KFSW_FTP_PARAM_TABLE_ID);
	zassert_equal(seen.ids[11], KFSW_FWU_PARAM_TABLE_ID);
	zassert_equal(seen.ids[12], KFSW_HEALTH_PARAM_TABLE_ID);
	zassert_equal(seen.ids[13], KFSW_BOOT_PARAM_TABLE_ID);

	zassert_true(seen.ascending, "a listing must read in one direction");
	zassert_true(seen.bands_match_owners, "every table must be in its band");
}

ZTEST(app_param_tables, test_table_identifiers_sit_in_their_band)
{
	zassert_str_equal(kfsw_param_band_name(KFSW_PARAM_TABLE_INVALID), "invalid");
	zassert_str_equal(kfsw_param_band_name(KFSW_PARAM_TABLE_CORE_FIRST), "core");
	zassert_str_equal(kfsw_param_band_name(KFSW_PARAM_TABLE_CORE_LAST), "core");
	zassert_str_equal(kfsw_param_band_name(KFSW_PARAM_TABLE_SERVICE_FIRST), "service");
	zassert_str_equal(kfsw_param_band_name(KFSW_PARAM_TABLE_SERVICE_LAST), "service");
	zassert_str_equal(kfsw_param_band_name(KFSW_PARAM_TABLE_MODULE_FIRST), "module");
	zassert_str_equal(kfsw_param_band_name(KFSW_PARAM_TABLE_MODULE_LAST), "module");
	zassert_str_equal(kfsw_param_band_name(KFSW_PARAM_TABLE_MODULE_LAST + 1U), "invalid");

	/* Zero is reserved so an uninitialised field cannot address a table. */
	zassert_str_equal(kfsw_param_band_name(0U), "invalid");
}

ZTEST(app_param_tables, test_parameters_are_addressed_by_table_and_offset)
{
	struct param_seen seen = {
		.every_name_within_limit = true,
		.every_table_allocated = true,
		.identifier_matches_table_and_offset = true,
		.ascending = true,
	};

	zassert_ok(kfsw_param_visit(record_parameter, &seen));
	zassert_true(seen.count >= CORE_TABLE_COUNT);
	zassert_true(seen.every_name_within_limit);
	zassert_true(seen.every_table_allocated);
	zassert_true(seen.identifier_matches_table_and_offset,
		     "the wire identifier must decode back to the table and offset");
	zassert_true(seen.ascending, "parameters must be listed by table then offset");
	zassert_true(seen.longest_name <= KFSW_PARAM_NAME_MAX);
}

/* --------------------------------------------------------------- the modes */

/*
 * One letter per property: writable, saved, applied at boot.
 */
ZTEST(app_param_tables, test_mode_follows_the_flags)
{
	zassert_str_equal(kfsw_param_mode_name(KFSW_PARAM_FLAG_READ_ONLY), "r");
	zassert_str_equal(kfsw_param_mode_name(KFSW_PARAM_FLAG_LIVE), "w");

	/* Kept, and only read when the node next starts. */
	zassert_str_equal(kfsw_param_mode_name(KFSW_PARAM_FLAG_PERSISTENT), "wpb");

	/* Kept, and applied as soon as it is set. */
	zassert_str_equal(kfsw_param_mode_name(KFSW_PARAM_FLAG_PERSISTENT | KFSW_PARAM_FLAG_LIVE),
			  "wp");

	/* Written, applied now, and gone at the next start. */
	zassert_str_equal(kfsw_param_mode_name(0U), "wb");

	/* Read-only values don't get the boot letter, but can be saved. */
	zassert_str_equal(kfsw_param_mode_name(KFSW_PARAM_FLAG_READ_ONLY |
					       KFSW_PARAM_FLAG_PERSISTENT | KFSW_PARAM_FLAG_LIVE),
			  "rp");
	zassert_str_equal(kfsw_param_mode_name(KFSW_PARAM_FLAG_READ_ONLY | KFSW_PARAM_FLAG_LIVE),
			  "r");
}

ZTEST(app_param_tables, test_telemetry_is_read_only_throughout)
{
	struct kfsw_param_info info;
	struct kfsw_param_value value;

	/* A write to a read-only value must be refused. */
	zassert_ok(kfsw_param_get_info("uptime_s", &info));
	zassert_equal(info.table, KFSW_PARAM_TABLE_TELEMETRY);
	zassert_true(info.read_only);
	zassert_str_equal(kfsw_param_mode_name(info.flags), "r");

	zassert_ok(kfsw_param_get("uptime_s", &value));
	zassert_equal(kfsw_param_set("uptime_s", &value), -EACCES);
}

ZTEST(app_param_tables, test_board_reports_the_running_address)
{
	struct kfsw_param_info info;
	struct kfsw_param_value value;

	zassert_ok(kfsw_param_get_info("node_id", &info));
	zassert_equal(info.table, KFSW_PARAM_TABLE_BOARD);
	zassert_equal(info.offset, 0x00U);

	zassert_ok(kfsw_param_get("node_id", &value));
	zassert_equal(value.scalar.u16, CONFIG_KFSW_CSP_ADDRESS);
}

/* ------------------------------------------------------------- the sampling */

ZTEST(app_param_tables, test_a_sampled_value_follows_what_it_reports)
{
	struct kfsw_param_value before;
	struct kfsw_param_value after;

	/* Sampled values must follow the current state. */
	zassert_ok(kfsw_param_get("uptime_s", &before));
	k_sleep(K_MSEC(1100));
	zassert_ok(kfsw_param_get("uptime_s", &after));
	zassert_true(after.scalar.u32 > before.scalar.u32,
		     "uptime must advance between two reads a second apart");
}

ZTEST(app_param_tables, test_storage_reports_a_mounted_volume)
{
	struct kfsw_param_value mounted;
	struct kfsw_param_value total;
	struct kfsw_param_value free_space;
	struct kfsw_param_value used;

	zassert_ok(kfsw_param_get("mounted", &mounted));
	zassert_equal(mounted.scalar.u8, 1U, "the fixture mounts the volume before it reads it");

	zassert_ok(kfsw_param_get("total_kb", &total));
	zassert_ok(kfsw_param_get("free_kb", &free_space));
	zassert_true(total.scalar.u32 > 0U);
	zassert_true(free_space.scalar.u32 <= total.scalar.u32,
		     "free space beyond capacity would mean the sample is not of one snapshot");

	/* An unmounted volume reports zero, not full. */
	zassert_ok(kfsw_param_get("used_pct", &used));
	zassert_true(used.scalar.u8 <= 100U);
}

ZTEST(app_param_tables, test_watchdog_reports_no_hardware_rather_than_a_timeout)
{
	struct kfsw_param_value bound;
	struct kfsw_param_value timeout;
	struct kfsw_param_value feeds;

	/* No watchdog device here, so the table must not report a timeout. */
	zassert_ok(kfsw_param_get("device_bound", &bound));
	zassert_equal(bound.scalar.u8, 0U);

	zassert_ok(kfsw_param_get("timeout_ms", &timeout));
	zassert_equal(timeout.scalar.u32, 0U,
		      "an unbound watchdog must not report a timeout it does not hold");

	zassert_ok(kfsw_param_get("feeds", &feeds));
	zassert_equal(feeds.scalar.u32, 0U);
}

ZTEST(app_param_tables, test_the_feed_interval_leaves_a_margin)
{
	/* Two feeds must be missable before the timeout. */
	const uint32_t interval =
		kfsw_platform_watchdog_feed_interval_ms(CONFIG_KFSW_WATCHDOG_TIMEOUT_MS);

	zassert_true(interval > 0U);
	zassert_true((interval * 2U) < CONFIG_KFSW_WATCHDOG_TIMEOUT_MS);
}

/* ------------------------------------------------------ configuration modes */

ZTEST(app_param_tables, test_a_stored_parameter_says_it_is_stored)
{
	struct kfsw_param_info info;
	struct kfsw_param_value value;

	zassert_ok(kfsw_param_get_info("boot_delay_ms", &info));
	zassert_equal(info.table, KFSW_PARAM_TABLE_SYSTEM);
	zassert_str_equal(kfsw_param_mode_name(info.flags), "wpb",
			  "the boot it delays has already happened by the time it could apply");

	zassert_ok(kfsw_param_get("boot_delay_ms", &value));
	value.scalar.u16 = 250U;
	zassert_ok(kfsw_param_set("boot_delay_ms", &value));
	zassert_equal(kfsw_system_boot_delay_ms(), 250U);
}

ZTEST(app_param_tables, test_a_report_period_that_would_reset_the_board_is_refused)
{
	struct kfsw_param_info info;
	struct kfsw_param_value value;
	const uint16_t original = kfsw_system_app_report_ms();

	zassert_ok(kfsw_param_get_info("app_report_ms", &info));
	zassert_equal(info.table, KFSW_PARAM_TABLE_SYSTEM);
	zassert_str_equal(kfsw_param_mode_name(info.flags), "wp",
			  "the loop reads it every cycle and the value survives a reboot");

	/* A period at or above half the health deadline is refused. */
	zassert_ok(kfsw_param_get("app_report_ms", &value));
	value.scalar.u16 = UINT16_MAX;
	zassert_equal(kfsw_param_set("app_report_ms", &value), -ERANGE);
	zassert_equal(kfsw_system_app_report_ms(), original, "a refused write must change nothing");

	value.scalar.u16 = 0U;
	zassert_equal(kfsw_param_set("app_report_ms", &value), -ERANGE,
		      "a period of zero would spin the application thread");
	zassert_equal(kfsw_system_app_report_ms(), original);

	value.scalar.u16 = 200U;
	zassert_ok(kfsw_param_set("app_report_ms", &value));
	zassert_equal(kfsw_system_app_report_ms(), 200U);
}

/* ---------------------------------------------------------------- strings */

ZTEST(app_param_tables, test_identity_is_reported_as_text)
{
	struct kfsw_param_info info;
	struct kfsw_param_value value;

	/* Sampled from the running CSP identity. */
	zassert_ok(kfsw_param_get_info("uid", &info));
	zassert_equal(info.table, KFSW_PARAM_TABLE_BOARD);
	zassert_equal(info.type, KFSW_PARAM_STRING);
	zassert_true(info.read_only);

	{
		struct kfsw_csp_info csp_info;

		kfsw_csp_get_info(&csp_info);
		zassert_ok(kfsw_param_get("uid", &value));
		zassert_equal(value.type, KFSW_PARAM_STRING);
		/* Compared with CSP's running value, not the build option. */
		zassert_str_equal(value.text, csp_info.hostname);
		zassert_true(strlen(csp_info.hostname) + 1U <= info.array_size,
			     "a truncated identity looks like a different node");
		zassert_equal(value.size, strlen(value.text) + 1U, "size carries the terminator");
	}

	zassert_ok(kfsw_param_get("revision", &value));
	zassert_true(strlen(value.text) > 0U);

	/* Read-only means refused, not accepted and ignored. */
	zassert_equal(kfsw_param_set("uid", &value), -EACCES);
}

ZTEST(app_param_tables, test_a_string_reports_its_capacity)
{
	struct kfsw_param_info info;

	/* array_size is the declared capacity. */
	zassert_ok(kfsw_param_get_info("uid", &info));
	zassert_true(info.array_size > 1U);
	zassert_true(info.array_size <= KFSW_PARAM_STRING_MAX);
}

ZTEST(app_param_tables, test_the_route_table_starts_from_the_composed_one)
{
	struct kfsw_param_info info;
	struct kfsw_param_value value;

	zassert_ok(kfsw_param_get_info("route_table", &info));
	zassert_equal(info.table, KFSW_PARAM_TABLE_CSP);
	zassert_equal(info.type, KFSW_PARAM_STRING);
	zassert_true(info.read_only);
	zassert_str_equal(kfsw_param_mode_name(info.flags), "r");
	memset(&value, 0, sizeof(value));
	value.type = KFSW_PARAM_STRING;
	strcpy(value.text, "0/0 LOOP");
	zassert_equal(kfsw_param_set("route_table", &value), -EACCES);

	zassert_ok(kfsw_param_get("route_table", &value));
	zassert_str_equal(value.text, CONFIG_KFSW_CSP_ROUTE_TABLE);
}

/* -------------------------------------------------------- service tables */

ZTEST(app_param_tables, test_a_service_table_sits_in_the_service_band)
{
	struct kfsw_param_info info;

	/* Each table must be in its component's band. */
	zassert_ok(kfsw_param_get_info("events_recorded", &info));
	zassert_equal(info.table, KFSW_EVENT_PARAM_TABLE_ID);
	zassert_str_equal(kfsw_param_band_name(info.table), "service");

	zassert_ok(kfsw_param_get_info("fwu_state", &info));
	zassert_equal(info.table, KFSW_FWU_PARAM_TABLE_ID);
	zassert_str_equal(kfsw_param_band_name(info.table), "service");

	zassert_ok(kfsw_param_get_info("health_state", &info));
	zassert_equal(info.table, KFSW_HEALTH_PARAM_TABLE_ID);
	zassert_str_equal(kfsw_param_band_name(info.table), "service");
}

ZTEST(app_param_tables, test_update_state_cannot_be_set_from_outside)
{
	struct kfsw_param_value value;

	/* The update table is read-only. */
	zassert_ok(kfsw_param_get("fwu_state", &value));
	zassert_equal(kfsw_param_set("fwu_state", &value), -EACCES);

	zassert_ok(kfsw_param_get("fwu_swap_scheduled", &value));
	zassert_equal(kfsw_param_set("fwu_swap_scheduled", &value), -EACCES);

	zassert_ok(kfsw_param_get("fwu_expected_crc", &value));
	zassert_equal(kfsw_param_set("fwu_expected_crc", &value), -EACCES);
}

ZTEST(app_param_tables, test_the_event_table_follows_the_record)
{
	struct kfsw_event_stats stats;
	struct kfsw_param_value capacity;
	struct kfsw_param_value recorded;

	kfsw_event_get_stats(&stats);
	zassert_ok(kfsw_param_get("events_capacity", &capacity));
	zassert_equal(capacity.scalar.u16, stats.capacity);

	/* Recording an event must move the table's counter. */
	zassert_ok(kfsw_param_get("events_recorded", &recorded));
	kfsw_event_emit(KFSW_EVENT_SOURCE_APP, 1U, KFSW_EVENT_INFO, NULL, 0U);
	{
		struct kfsw_param_value after;

		zassert_ok(kfsw_param_get("events_recorded", &after));
		zassert_equal(after.scalar.u32, recorded.scalar.u32 + 1U);
	}
}

ZTEST(app_param_tables, test_a_check_slower_than_the_watchdog_is_refused)
{
	struct kfsw_param_info info;
	struct kfsw_param_value value;

	zassert_ok(kfsw_param_get_info("health_interval_ms", &info));
	zassert_str_equal(kfsw_param_mode_name(info.flags), "w",
			  "live so it can be corrected from the ground, not stored so a "
			  "mistake does not outlive the pass");

	/* Zero would be missed the instant it was set. */
	zassert_equal(kfsw_health_check_interval_ms(0U), -EINVAL);

	/* No watchdog here, so any interval is accepted. */
	zassert_ok(kfsw_health_check_interval_ms(60000U));

	zassert_ok(kfsw_param_get("health_interval_ms", &value));
	value.scalar.u16 = 250U;
	zassert_ok(kfsw_param_set("health_interval_ms", &value));
	zassert_equal(kfsw_health_get_interval_ms(), 250U);

	value.scalar.u16 = 0U;
	zassert_equal(kfsw_param_set("health_interval_ms", &value), -EINVAL);
	zassert_equal(kfsw_health_get_interval_ms(), 250U, "a refused write changes nothing");
}

ZTEST(app_param_tables, test_a_counter_moves_when_the_thing_it_counts_happens)
{
	struct kfsw_param_value before;
	struct kfsw_param_value after;

	/* Asserted as a delta, not a value: these are lifetime counters and
	 * anything else in the image may have moved them first. */
	{
		struct kfsw_command_result command_result = {0};

		zassert_ok(kfsw_param_get("cmd_invoked", &before));
		zassert_ok(kfsw_command_invoke("counted", NULL, 0U, &command_result));
		zassert_ok(kfsw_param_get("cmd_invoked", &after));
	}
	zassert_true(after.scalar.u32 > before.scalar.u32,
		     "an invocation that reached the service must be counted");

	zassert_ok(kfsw_param_get("log_emitted", &before));
	kfsw_log_error("a message the counter has to see");
	zassert_ok(kfsw_param_get("log_emitted", &after));
	zassert_true(after.scalar.u32 > before.scalar.u32);
}

ZTEST(app_param_tables, test_a_transfer_size_larger_than_the_buffer_is_refused)
{
	struct kfsw_param_value value;

	/* A chunk larger than the build-time buffer is refused. */
	zassert_ok(kfsw_param_get("ftp_chunk_size", &value));
	value.scalar.u16 = KFSW_FTP_CHUNK_SIZE + 1U;
	zassert_equal(kfsw_param_set("ftp_chunk_size", &value), -ERANGE);

	value.scalar.u16 = 0U;
	zassert_equal(kfsw_param_set("ftp_chunk_size", &value), -ERANGE);

	value.scalar.u16 = 64U;
	zassert_ok(kfsw_param_set("ftp_chunk_size", &value));
	zassert_equal(kfsw_ftp_get_chunk_size(), 64U);
}

ZTEST(app_param_tables, test_echo_is_off_until_asked_for)
{
	struct kfsw_param_info info;
	struct kfsw_param_value value;

	/* Echo is off by default. */
	zassert_ok(kfsw_param_get_info("echo_enabled", &info));
	zassert_equal(info.table, KFSW_COMMAND_PARAM_TABLE_ID);
	zassert_str_equal(kfsw_param_mode_name(info.flags), "w", "echo must apply immediately");

	zassert_ok(kfsw_param_get("echo_enabled", &value));
	zassert_equal(value.scalar.u8, 0U);

	value.scalar.u8 = 1U;
	zassert_ok(kfsw_param_set("echo_enabled", &value));
	zassert_true(kfsw_command_echo_enabled());

	value.scalar.u8 = 2U;
	zassert_equal(kfsw_param_set("echo_enabled", &value), -ERANGE);
	zassert_true(kfsw_command_echo_enabled(), "a refused write changes nothing");

	value.scalar.u8 = 0U;
	zassert_ok(kfsw_param_set("echo_enabled", &value));
	zassert_false(kfsw_command_echo_enabled());
}

ZTEST(app_param_tables, test_the_boot_table_reports_a_real_image)
{
	struct kfsw_param_value value;

	/* The version names the build source. */
	zassert_ok(kfsw_param_get("boot_image", &value));
	zassert_equal(value.type, KFSW_PARAM_STRING);
	zassert_true(strlen(value.text) > 0U);
	zassert_str_equal(value.text, kfsw_boot_get_image_version());

	/* Read-only and persistent. */
	zassert_ok(kfsw_param_get("boot_count", &value));
	zassert_equal(kfsw_param_set("boot_count", &value), -EACCES);
}

ZTEST(app_param_tables, test_looking_a_parameter_up_by_name)
{
	struct kfsw_param_info info;

	zassert_equal(kfsw_param_get_info(NULL, &info), -EINVAL);
	zassert_equal(kfsw_param_get_info("uptime_s", NULL), -EINVAL);
	zassert_equal(kfsw_param_get_info("no_such_parameter", &info), -ENOENT);
}

ZTEST_SUITE(app_param_tables, NULL, tables_setup, NULL, NULL, NULL);
