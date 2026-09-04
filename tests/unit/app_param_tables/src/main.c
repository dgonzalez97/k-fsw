#include <errno.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#include <zephyr/ztest.h>

#include <kfsw/comms/csp.h>
#include <kfsw/platform/storage.h>
#include <kfsw/platform/watchdog.h>
#include <kfsw/services/parameter.h>

#include "parameters/tables.h"

/* What is tested here is the scheme, not the numbers. A value read from a
 * table is only as good as the layer underneath it, and asserting a particular
 * free-space figure would test LittleFS rather than this code. What must hold
 * is that every core table registers, that its identifier falls in the band
 * its owner is allocated, that offsets and names cannot collide, that a
 * read-only parameter refuses a write, and that a sampled value follows the
 * state it reports rather than a copy taken once at start-up.
 */

#define CORE_TABLE_COUNT 6U

struct table_seen {
	uint8_t ids[KFSW_PARAM_TABLE_MODULE_LAST + 1U];
	size_t count;
	bool every_band_correct;
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
	&kfsw_board_param_definitions,     &kfsw_system_param_definitions,
	&kfsw_telemetry_param_definitions, &kfsw_csp_param_definitions,
	&kfsw_storage_param_definitions,   &kfsw_watchdog_param_definitions,
};

static void *tables_setup(void)
{
	/* Storage is mounted first so the two tables that report it have
	 * something real to read; the parameter service does not depend on it,
	 * but a filesystem that never mounted would make those rows
	 * indistinguishable from ones that are simply broken.
	 */
	(void)kfsw_storage_init();
	(void)kfsw_storage_mount();

	/* The watchdog table reports what the platform installed, so the
	 * platform has to have installed it. Reading the table before
	 * initialization reports zeros, which is honest but says nothing about
	 * whether the table follows the hardware.
	 */
	(void)kfsw_platform_watchdog_init();

	/* Same reasoning for identity: the board table samples the running CSP
	 * configuration rather than the build options, so CSP has to be running
	 * for those rows to say anything. Before initialization they report
	 * empty, which is honest and worth nothing as a test.
	 */
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
	if (strcmp(kfsw_param_band_name(info->id), "core") != 0) {
		seen->every_band_correct = false;
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
		.every_band_correct = true,
		.ascending = true,
	};

	zassert_equal(kfsw_param_table_count(), CORE_TABLE_COUNT);
	zassert_ok(kfsw_param_visit_tables(record_table, &seen));
	zassert_equal(seen.count, CORE_TABLE_COUNT);

	zassert_equal(seen.ids[0], KFSW_PARAM_TABLE_BOARD);
	zassert_equal(seen.ids[1], KFSW_PARAM_TABLE_SYSTEM);
	zassert_equal(seen.ids[2], KFSW_PARAM_TABLE_TELEMETRY);
	zassert_equal(seen.ids[3], KFSW_PARAM_TABLE_CSP);
	zassert_equal(seen.ids[4], KFSW_PARAM_TABLE_STORAGE);
	zassert_equal(seen.ids[5], KFSW_PARAM_TABLE_WATCHDOG);

	zassert_true(seen.ascending, "a listing must read in one direction");
	zassert_true(seen.every_band_correct,
		     "a core table outside the core band would collide with a service or module");
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

ZTEST(app_param_tables, test_mode_follows_the_flags)
{
	zassert_str_equal(kfsw_param_mode_name(KFSW_PARAM_FLAG_READ_ONLY), "r");
	zassert_str_equal(kfsw_param_mode_name(KFSW_PARAM_FLAG_LIVE), "w");
	zassert_str_equal(kfsw_param_mode_name(KFSW_PARAM_FLAG_PERSISTENT), "b");
	zassert_str_equal(kfsw_param_mode_name(KFSW_PARAM_FLAG_PERSISTENT | KFSW_PARAM_FLAG_LIVE),
			  "wb");

	/* Read-only wins over everything else: a parameter nobody can write
	 * has no write behaviour to describe. */
	zassert_str_equal(kfsw_param_mode_name(KFSW_PARAM_FLAG_READ_ONLY |
					       KFSW_PARAM_FLAG_PERSISTENT | KFSW_PARAM_FLAG_LIVE),
			  "r");
}

ZTEST(app_param_tables, test_telemetry_is_read_only_throughout)
{
	struct kfsw_param_info info;
	struct kfsw_param_value value;

	/* A write here must be refused rather than accepted and ignored: an
	 * operator who believes a housekeeping value took a new setting has
	 * been told something false. */
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

	/* The point of sampling on read is that the answer is current when it
	 * was asked for. A value refreshed on a timer, or copied once at
	 * start-up, would pass every other check in this file and still report
	 * a stale uptime to the ground.
	 */
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

	/* Never reported as a full filesystem while it is simply unmounted:
	 * that would trigger the wrong response on the ground. */
	zassert_ok(kfsw_param_get("used_pct", &used));
	zassert_true(used.scalar.u8 <= 100U);
}

ZTEST(app_param_tables, test_watchdog_reports_no_hardware_rather_than_a_timeout)
{
	struct kfsw_param_value bound;
	struct kfsw_param_value timeout;
	struct kfsw_param_value feeds;

	/* This target has no watchdog device, and the fixture has already tried
	 * to bind one. The table must say so instead of reporting the
	 * configured timeout: a timeout that was never installed in hardware
	 * looks, from the ground, exactly like one that was, and it is the
	 * difference between a board that will reset itself and one that will
	 * hang forever.
	 */
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
	/* Pure arithmetic, available on every target including this one, so the
	 * margin can be asserted where the hardware cannot be: two feeds must
	 * be missable before the timeout expires.
	 */
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
	zassert_str_equal(kfsw_param_mode_name(info.flags), "b",
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
	zassert_str_equal(kfsw_param_mode_name(info.flags), "wb",
			  "the loop reads it every cycle and the value survives a reboot");

	/* A period at or beyond half the health deadline leaves no room for an
	 * ordinary scheduling delay, so one late cycle would look like a
	 * stopped thread and reset a board that is working. This is the one
	 * value in the core tables that can do that by being set to a number
	 * that looks perfectly reasonable, so it is refused rather than
	 * accepted.
	 */
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

	/* Sampled from the running CSP identity rather than kept as a second
	 * copy of the build options: two sources for one fact eventually
	 * disagree, and the one an operator can reach would be the wrong one.
	 */
	zassert_ok(kfsw_param_get_info("uid", &info));
	zassert_equal(info.table, KFSW_PARAM_TABLE_BOARD);
	zassert_equal(info.type, KFSW_PARAM_STRING);
	zassert_true(info.read_only);

	{
		struct kfsw_csp_info csp_info;

		kfsw_csp_get_info(&csp_info);
		zassert_ok(kfsw_param_get("uid", &value));
		zassert_equal(value.type, KFSW_PARAM_STRING);
		/* Compared against what CSP actually reports, not against the
		 * build option: the point of sampling is that the two can
		 * differ, and the table has to follow the running one. */
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

	/* array_size carries the declared capacity so every layer -- the
	 * listing, the snapshot, the wire -- sees one number for how much
	 * storage the owner set aside.
	 */
	zassert_ok(kfsw_param_get_info("uid", &info));
	zassert_true(info.array_size > 1U);
	zassert_true(info.array_size <= KFSW_PARAM_STRING_MAX);
}

ZTEST(app_param_tables, test_the_route_table_starts_from_the_composed_one)
{
	struct kfsw_param_info info;
	struct kfsw_param_value value;

	/* Writable and live, but deliberately not persistent: a route table is
	 * the one setting that can put a node out of reach, so a wrong one must
	 * not survive a reboot. The compiled table comes back and the mistake
	 * costs a pass rather than the node.
	 */
	zassert_ok(kfsw_param_get_info("route_table", &info));
	zassert_equal(info.table, KFSW_PARAM_TABLE_CSP);
	zassert_equal(info.type, KFSW_PARAM_STRING);
	zassert_false(info.read_only);
	zassert_str_equal(kfsw_param_mode_name(info.flags), "w",
			  "live so it can be fixed from the ground, not stored so a "
			  "mistake does not outlive the pass");

	zassert_ok(kfsw_param_get("route_table", &value));
	zassert_str_equal(value.text, CONFIG_KFSW_CSP_ROUTE_TABLE);
}

ZTEST(app_param_tables, test_looking_a_parameter_up_by_name)
{
	struct kfsw_param_info info;

	zassert_equal(kfsw_param_get_info(NULL, &info), -EINVAL);
	zassert_equal(kfsw_param_get_info("uptime_s", NULL), -EINVAL);
	zassert_equal(kfsw_param_get_info("no_such_parameter", &info), -ENOENT);
}

ZTEST_SUITE(app_param_tables, NULL, tables_setup, NULL, NULL, NULL);
