#include <string.h>

#include <zephyr/devicetree.h>
#include <zephyr/fs/fs.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/crc.h>
#include <zephyr/sys/util.h>
#include <zephyr/ztest.h>

#include <kfsw/platform/storage.h>
#include <kfsw/services/hk.h>
#include <kfsw/services/parameter.h>

#define TEST_TABLE 30U
#define REPORTS_PATH KFSW_STORAGE_MOUNT_POINT "/hk/reports.dat"
#define STORAGE_PARTITION_NODE DT_CHOSEN(kfsw_storage_partition)
#define STORAGE_PARTITION_ID DT_FIXED_PARTITION_ID(STORAGE_PARTITION_NODE)

/*
 * What survives a reset, and what must not.
 *
 * A definition is the one piece of housekeeping state an operator builds by
 * hand over a link, so losing it to a reset costs a pass. That makes the file
 * worth keeping — and makes every way of reading it back wrong worth refusing
 * loudly, because a definition decoded from a damaged file does not fail: it
 * collects the wrong parameters and reports them as if they were the right
 * ones. Ground would have no way to tell.
 *
 * Save and load are internal because nothing outside the service should choose
 * when they happen. Declaring them here is the exception a test earns.
 */
int kfsw_hk_persist_save(void);
int kfsw_hk_persist_load(void);

static uint16_t counter_u16 = 0x1234U;
static uint32_t counter_u32 = 0xAABBCCDDU;

static const struct kfsw_param_definition test_definitions[] = {
	{
		.offset = 0x00,
		.type = KFSW_PARAM_U16,
		.flags = KFSW_PARAM_FLAG_READ_ONLY,
		.name = "persist_test_u16",
		.value = &counter_u16,
	},
	{
		.offset = 0x02,
		.type = KFSW_PARAM_U32,
		.flags = KFSW_PARAM_FLAG_READ_ONLY,
		.name = "persist_test_u32",
		.value = &counter_u32,
	},
};

static const struct kfsw_param_definition_set test_set = {
	.table = TEST_TABLE,
	.name = "persisttest",
	.definitions = test_definitions,
	.count = ARRAY_SIZE(test_definitions),
};

/* The partition holds whatever the last run left in it, and a first mount of
 * dirty media is not the case under test here.
 */
static void erase_storage_partition(void)
{
	const struct flash_area *area;
	int result;

	zassert_ok(flash_area_open(STORAGE_PARTITION_ID, &area),
		   "the storage partition would not open");
	result = flash_area_flatten(area, 0, area->fa_size);
	flash_area_close(area);
	zassert_ok(result, "the storage partition would not erase");
}

static void *persist_setup(void)
{
	static const struct kfsw_param_definition_set *const sets[] = {&test_set};

	erase_storage_partition();
	zassert_ok(kfsw_storage_init(), "storage did not start");
	zassert_ok(kfsw_storage_mount(), "storage did not mount");
	zassert_ok(kfsw_param_init(sets, ARRAY_SIZE(sets)), "the parameter table did not start");
	zassert_ok(kfsw_hk_init(), "housekeeping did not start");
	return NULL;
}

static void persist_before(void *fixture)
{
	ARG_UNUSED(fixture);
	(void)kfsw_hk_save();
	for (uint8_t report = 0U; report < CONFIG_KFSW_HK_REPORTS; report++) {
		(void)kfsw_hk_set_beacon(report, 1U, 0U);
		(void)kfsw_hk_clear_store(report);
		(void)kfsw_hk_clear(report);
	}
	(void)fs_unlink(REPORTS_PATH);
}

/* Defines report 0 over both test parameters. */
static void define_report(uint8_t report)
{
	const struct kfsw_hk_entry entries[] = {
		{.node = KFSW_HK_NODE_LOCAL, .param_id = KFSW_PARAM_ID(TEST_TABLE, 0x00)},
		{.node = KFSW_HK_NODE_LOCAL, .param_id = KFSW_PARAM_ID(TEST_TABLE, 0x02)},
	};

	zassert_ok(kfsw_hk_define(report, entries, ARRAY_SIZE(entries)), "the report was refused");
}

static void forget_everything(void)
{
	for (uint8_t report = 0U; report < CONFIG_KFSW_HK_REPORTS; report++) {
		(void)kfsw_hk_clear(report);
	}
}

static size_t read_file(uint8_t *buffer, size_t size);
static void write_file(const uint8_t *buffer, size_t size);

/*
 * What a reset actually looks like from the service's side.
 *
 * Defining a report writes the file, and so does clearing one — the snapshot
 * tracks the live set rather than waiting to be asked. So a test cannot make
 * the service forget by clearing, because that persists the forgetting too.
 * It has to put the bytes back afterwards, which is what a node that lost
 * power mid-pass would come back to.
 */
static void restart_with(const uint8_t *saved, size_t size)
{
	forget_everything();
	write_file(saved, size);
}

/* Reads the saved file into a buffer, and says how long it was. */
static size_t read_file(uint8_t *buffer, size_t size)
{
	struct fs_file_t file;
	ssize_t read;

	fs_file_t_init(&file);
	zassert_ok(fs_open(&file, REPORTS_PATH, FS_O_READ), "the saved file would not open");
	read = fs_read(&file, buffer, size);
	(void)fs_close(&file);
	zassert_true(read > 0, "the saved file was empty");
	return (size_t)read;
}

static void write_file(const uint8_t *buffer, size_t size)
{
	struct fs_file_t file;

	fs_file_t_init(&file);
	zassert_ok(fs_open(&file, REPORTS_PATH, FS_O_CREATE | FS_O_WRITE | FS_O_TRUNC),
		   "the file would not open for writing");
	zassert_equal(fs_write(&file, buffer, size), (ssize_t)size, "the file was not written");
	zassert_ok(fs_close(&file), "the file would not close");
}

ZTEST_SUITE(kfsw_hk_persistence, NULL, persist_setup, persist_before, NULL, NULL);

/* The whole point: a report built over a link comes back after a reset. */
ZTEST(kfsw_hk_persistence, test_a_definition_comes_back)
{
	struct kfsw_hk_entry read_back[CONFIG_KFSW_HK_ENTRIES];
	size_t count = ARRAY_SIZE(read_back);

	uint8_t saved[256];
	size_t size;

	define_report(0U);
	zassert_ok(kfsw_hk_persist_save(), "the definitions were not saved");
	size = read_file(saved, sizeof(saved));

	restart_with(saved, size);
	count = ARRAY_SIZE(read_back);
	zassert_not_equal(kfsw_hk_get_definition(0U, read_back, &count), 0,
			  "the report was still defined before the reload");

	zassert_ok(kfsw_hk_persist_load(), "the definitions were not loaded");

	count = ARRAY_SIZE(read_back);
	zassert_ok(kfsw_hk_get_definition(0U, read_back, &count), "the report did not come back");
	zassert_equal(count, 2U, "the report came back a different shape");
	zassert_equal(read_back[0].param_id, KFSW_PARAM_ID(TEST_TABLE, 0x00),
		      "the first value changed");
	zassert_equal(read_back[1].param_id, KFSW_PARAM_ID(TEST_TABLE, 0x02),
		      "the second value changed");
}

/* A definition without its period would come back and never collect. */
ZTEST(kfsw_hk_persistence, test_the_collection_period_comes_back)
{
	uint32_t period = 0U;

	uint8_t saved[256];
	size_t size;

	define_report(0U);
	zassert_ok(kfsw_hk_set_period(0U, 60000U), "the period was refused");
	zassert_ok(kfsw_hk_persist_save(), "the definitions were not saved");
	size = read_file(saved, sizeof(saved));

	restart_with(saved, size);
	zassert_ok(kfsw_hk_persist_load(), "the definitions were not loaded");

	zassert_ok(kfsw_hk_get_period(0U, &period), "the period could not be read");
	zassert_equal(period, 60000U, "the report came back on a different schedule");
}

ZTEST(kfsw_hk_persistence, test_several_reports_come_back)
{
	struct kfsw_hk_entry read_back[CONFIG_KFSW_HK_ENTRIES];
	size_t count;

	uint8_t saved[256];
	size_t size;

	define_report(0U);
	define_report(1U);
	zassert_ok(kfsw_hk_persist_save(), "the definitions were not saved");
	size = read_file(saved, sizeof(saved));

	restart_with(saved, size);
	zassert_ok(kfsw_hk_persist_load(), "the definitions were not loaded");

	for (uint8_t report = 0U; report < 2U; report++) {
		count = ARRAY_SIZE(read_back);
		zassert_ok(kfsw_hk_get_definition(report, read_back, &count),
			   "a report did not come back");
		zassert_equal(count, 2U, "a report came back a different shape");
	}
}

/* Nothing saved is not an error worth resetting over, but it must not be read
 * as an empty set of definitions either.
 */
ZTEST(kfsw_hk_persistence, test_no_file_is_not_a_definition)
{
	zassert_not_equal(kfsw_hk_persist_load(), 0, "a missing file loaded successfully");
}

/*
 * The three ways a file can be wrong, each refused rather than decoded.
 *
 * This is the part that matters: every one of these, accepted, produces a
 * report that looks defined and collects something other than what it names.
 */
ZTEST(kfsw_hk_persistence, test_a_file_from_another_format_is_refused)
{
	uint8_t blob[256];
	size_t size;

	define_report(0U);
	zassert_ok(kfsw_hk_persist_save(), "the definitions were not saved");
	size = read_file(blob, sizeof(blob));

	blob[0] = 'X';
	restart_with(blob, size);

	zassert_equal(kfsw_hk_persist_load(), -EBADMSG, "a foreign file was accepted");
}

ZTEST(kfsw_hk_persistence, test_a_newer_version_is_refused_rather_than_guessed_at)
{
	uint8_t blob[256];
	size_t size;

	define_report(0U);
	zassert_ok(kfsw_hk_persist_save(), "the definitions were not saved");
	size = read_file(blob, sizeof(blob));

	blob[4] = 99U;
	restart_with(blob, size);

	zassert_equal(kfsw_hk_persist_load(), -EPROTONOSUPPORT,
		      "a layout this reader does not know was decoded anyway");
}

ZTEST(kfsw_hk_persistence, test_a_damaged_file_is_refused)
{
	uint8_t blob[256];
	size_t size;

	define_report(0U);
	zassert_ok(kfsw_hk_persist_save(), "the definitions were not saved");
	size = read_file(blob, sizeof(blob));

	/* One flipped bit in the entries, past the header the CRC covers. */
	blob[size - 1U] ^= 0xFFU;
	restart_with(blob, size);

	zassert_equal(kfsw_hk_persist_load(), -EBADMSG,
		      "a file that failed its own checksum was decoded");
}

ZTEST(kfsw_hk_persistence, test_a_truncated_file_is_refused)
{
	uint8_t blob[256];
	size_t size;

	define_report(0U);
	zassert_ok(kfsw_hk_persist_save(), "the definitions were not saved");
	size = read_file(blob, sizeof(blob));
	zassert_true(size > 8U, "the saved file was implausibly short");

	restart_with(blob, size / 2U);

	zassert_not_equal(kfsw_hk_persist_load(), 0, "half a file was accepted");
}

/*
 * A definition is re-validated on the way in, not trusted. A parameter named
 * by a definition written before a firmware update may not exist any more, and
 * a report that cannot be collected must not come back looking defined.
 */
ZTEST(kfsw_hk_persistence, test_a_report_naming_what_is_gone_does_not_come_back)
{
	struct kfsw_hk_entry read_back[CONFIG_KFSW_HK_ENTRIES];
	size_t count = ARRAY_SIZE(read_back);
	uint8_t blob[256];
	size_t size;
	uint32_t crc_offset = 8U;
	uint32_t crc;

	define_report(0U);
	zassert_ok(kfsw_hk_persist_save(), "the definitions were not saved");
	size = read_file(blob, sizeof(blob));

	/* Point the first entry at an offset this build does not define, then
	 * re-checksum so the file is honestly formed and only its content is
	 * stale — which is exactly what a firmware update leaves behind.
	 *
	 * 12 bytes of file header, then 16 naming the report, its period and
	 * the policy around it, then entries of node and identifier: the first
	 * identifier is at 30.
	 */
	sys_put_be16(KFSW_PARAM_ID(TEST_TABLE, 0x7F), &blob[30]);
	sys_put_be32(0U, &blob[crc_offset]);
	crc = crc32_ieee(blob, size);
	sys_put_be32(crc, &blob[crc_offset]);
	restart_with(blob, size);

	(void)kfsw_hk_persist_load();

	zassert_not_equal(kfsw_hk_get_definition(0U, read_back, &count), 0,
			  "a report naming a parameter that no longer exists came back defined");
}

/*
 * Version 2 of the snapshot, and why it exists.
 *
 * A definition that comes back without the policy built around it is the half
 * that does not help: the node collects again, but its store is off and it
 * says nothing on its own, which is exactly the state an operator cannot fix
 * if the reset happened between passes.
 */
ZTEST(kfsw_hk_persistence, test_a_store_interval_comes_back)
{
	uint8_t saved[256];
	size_t size;
	uint32_t interval = 0U;

	define_report(0U);
	zassert_ok(kfsw_hk_set_period(0U, 2000U), "the period was refused");
	zassert_ok(kfsw_hk_set_store(0U, CONFIG_KFSW_HK_STORE_FLOOR_MS), "the store was refused");
	zassert_ok(kfsw_hk_persist_save(), "the definitions were not saved");
	size = read_file(saved, sizeof(saved));

	restart_with(saved, size);
	zassert_ok(kfsw_hk_persist_load(), "the definitions were not loaded");

	zassert_ok(kfsw_hk_get_store(0U, &interval), "the store could not be read");
	zassert_equal(interval, CONFIG_KFSW_HK_STORE_FLOOR_MS,
		      "a report came back collecting with its store off");
}

ZTEST(kfsw_hk_persistence, test_a_beacon_comes_back)
{
	uint8_t saved[256];
	size_t size;
	uint16_t node = 0U;
	uint32_t interval = 0U;

	define_report(0U);
	zassert_ok(kfsw_hk_set_period(0U, 2000U), "the period was refused");
	zassert_ok(kfsw_hk_set_beacon(0U, 16U, CONFIG_KFSW_HK_BEACON_FLOOR_MS),
		   "the beacon was refused");
	zassert_ok(kfsw_hk_persist_save(), "the definitions were not saved");
	size = read_file(saved, sizeof(saved));

	restart_with(saved, size);
	zassert_ok(kfsw_hk_persist_load(), "the definitions were not loaded");

	zassert_ok(kfsw_hk_get_beacon(0U, &node, &interval), "the beacon could not be read");
	zassert_equal(interval, CONFIG_KFSW_HK_BEACON_FLOOR_MS,
		      "a node came back quiet after a reset it did not choose");
	zassert_equal(node, 16U, "the beacon came back pointed at a different node");
}

/* A report that was not beaconing must not start on its own. */
ZTEST(kfsw_hk_persistence, test_a_report_that_was_silent_stays_silent)
{
	uint8_t saved[256];
	size_t size;
	uint16_t node = 0U;
	uint32_t interval = 1U;

	define_report(0U);
	zassert_ok(kfsw_hk_persist_save(), "the definitions were not saved");
	size = read_file(saved, sizeof(saved));

	restart_with(saved, size);
	zassert_ok(kfsw_hk_persist_load(), "the definitions were not loaded");

	zassert_ok(kfsw_hk_get_beacon(0U, &node, &interval), "the beacon could not be read");
	zassert_equal(interval, 0U, "a node that was never told to beacon started on its own");
}

/*
 * A board updated in place has a version 1 file on it. Refusing that would
 * cost the reports it holds for no reason: the fields version 2 added are
 * simply absent, which is the same as not configured.
 */
ZTEST(kfsw_hk_persistence, test_a_version_one_file_is_still_read)
{
	struct kfsw_hk_entry read_back[CONFIG_KFSW_HK_ENTRIES];
	size_t count = ARRAY_SIZE(read_back);
	uint8_t blob[256];
	uint8_t v1[256];
	size_t size;
	size_t v1_size;
	uint32_t period;
	uint32_t crc;

	define_report(0U);
	zassert_ok(kfsw_hk_set_period(0U, 4000U), "the period was refused");
	zassert_ok(kfsw_hk_persist_save(), "the definitions were not saved");
	size = read_file(blob, sizeof(blob));

	/* Rewrite it as version 1: the same header and entries, without the ten
	 * bytes of policy version 2 added between them.
	 */
	memcpy(v1, blob, 12U);
	v1[4] = 1U;
	memcpy(&v1[12], &blob[12], 6U);
	memcpy(&v1[18], &blob[28], size - 28U);
	v1_size = 18U + (size - 28U);
	sys_put_be32(0U, &v1[8]);
	crc = crc32_ieee(v1, v1_size);
	sys_put_be32(crc, &v1[8]);

	restart_with(v1, v1_size);
	zassert_ok(kfsw_hk_persist_load(), "a version 1 file was refused");

	zassert_ok(kfsw_hk_get_definition(0U, read_back, &count),
		   "the report in a version 1 file was lost");
	zassert_equal(count, 2U, "the report came back a different shape");
	zassert_ok(kfsw_hk_get_period(0U, &period), "the period could not be read");
	zassert_equal(period, 4000U, "the period in a version 1 file was lost");
}

/* A version this reader does not know is still refused rather than guessed. */
ZTEST(kfsw_hk_persistence, test_a_version_three_file_is_refused)
{
	uint8_t blob[256];
	size_t size;

	define_report(0U);
	zassert_ok(kfsw_hk_persist_save(), "the definitions were not saved");
	size = read_file(blob, sizeof(blob));

	blob[4] = 3U;
	restart_with(blob, size);

	zassert_equal(kfsw_hk_persist_load(), -EPROTONOSUPPORT,
		      "a layout from the future was decoded anyway");
}

static int write_error;
static int sync_error;
static int rename_error;
static atomic_t block_writes;
static K_SEM_DEFINE(write_entered, 0, 2);
static K_SEM_DEFINE(write_release, 0, 2);

ssize_t __real_fs_write(struct fs_file_t *file, const void *data, size_t size);
int __real_fs_sync(struct fs_file_t *file);
int __real_fs_rename(const char *from, const char *to);

ssize_t __wrap_fs_write(struct fs_file_t *file, const void *data, size_t size)
{
	if (atomic_get(&block_writes) > 0) {
		atomic_dec(&block_writes);
		k_sem_give(&write_entered);
		zassert_ok(k_sem_take(&write_release, K_SECONDS(3)));
	}
	return write_error != 0 ? write_error : __real_fs_write(file, data, size);
}

int __wrap_fs_sync(struct fs_file_t *file)
{
	return sync_error != 0 ? sync_error : __real_fs_sync(file);
}

int __wrap_fs_rename(const char *from, const char *to)
{
	return rename_error != 0 ? rename_error : __real_fs_rename(from, to);
}

ZTEST(kfsw_hk_persistence, test_save_errors_leave_active_settings_dirty_and_retryable)
{
	struct kfsw_hk_stats stats;
	uint32_t period;
	int *faults[] = {&write_error, &sync_error, &rename_error};
	int errors[] = {-ENOSPC, -EIO, -EACCES};

	define_report(0);
	for (size_t i = 0; i < ARRAY_SIZE(faults); i++) {
		*faults[i] = errors[i];
		zassert_equal(kfsw_hk_set_period(0, 5000 + i), KFSW_HK_APPLIED_UNSAVED);
		zassert_ok(kfsw_hk_get_period(0, &period));
		zassert_equal(period, 5000 + i);
		kfsw_hk_get_stats(&stats);
		zassert_true(stats.settings_dirty);
		zassert_equal(stats.last_save_error, errors[i]);
		*faults[i] = 0;
		zassert_ok(kfsw_hk_save());
		kfsw_hk_get_stats(&stats);
		zassert_false(stats.settings_dirty);
		zassert_equal(stats.last_save_error, 0);
	}
}

static K_THREAD_STACK_DEFINE(save_stack_a, 3072);
static K_THREAD_STACK_DEFINE(save_stack_b, 3072);
static struct k_thread save_thread_a, save_thread_b;

static void set_period_thread(void *period, void *unused_b, void *unused_c)
{
	ARG_UNUSED(unused_b);
	ARG_UNUSED(unused_c);
	zassert_ok(kfsw_hk_set_period(0, (uint32_t)(uintptr_t)period));
}

ZTEST(kfsw_hk_persistence, test_older_save_does_not_clear_newer_dirty_settings)
{
	struct kfsw_hk_stats stats;
	uint32_t period = 0;

	define_report(0);
	atomic_set(&block_writes, 2);
	k_thread_create(&save_thread_a, save_stack_a, K_THREAD_STACK_SIZEOF(save_stack_a),
			set_period_thread, (void *)5000, NULL, NULL, 5, 0, K_NO_WAIT);
	zassert_ok(k_sem_take(&write_entered, K_SECONDS(1)));
	k_thread_create(&save_thread_b, save_stack_b, K_THREAD_STACK_SIZEOF(save_stack_b),
			set_period_thread, (void *)6000, NULL, NULL, 5, 0, K_NO_WAIT);
	for (unsigned int i = 0; i < 100; i++) {
		zassert_ok(kfsw_hk_get_period(0, &period));
		if (period == 6000) {
			break;
		}
		k_sleep(K_MSEC(1));
	}
	zassert_equal(period, 6000);
	k_sem_give(&write_release);
	zassert_ok(k_thread_join(&save_thread_a, K_SECONDS(1)));
	zassert_ok(k_sem_take(&write_entered, K_SECONDS(1)));
	kfsw_hk_get_stats(&stats);
	zassert_true(stats.settings_dirty);
	k_sem_give(&write_release);
	zassert_ok(k_thread_join(&save_thread_b, K_SECONDS(1)));
	kfsw_hk_get_stats(&stats);
	zassert_false(stats.settings_dirty);
}

ZTEST(kfsw_hk_persistence, test_invalid_late_report_applies_nothing_and_preserves_file)
{
	uint8_t saved[1024], after[1024];
	struct kfsw_hk_entry entries[CONFIG_KFSW_HK_ENTRIES];
	struct kfsw_hk_stats stats;
	size_t count = ARRAY_SIZE(entries);

	define_report(0);
	define_report(1);
	size_t size = read_file(saved, sizeof(saved));

	/* The second report reuses the first ID; keep the CRC valid. */
	saved[12 + 16 + 2 * 4] = 0;
	sys_put_be32(0, &saved[8]);
	sys_put_be32(crc32_ieee(saved, size), &saved[8]);
	restart_with(saved, size);
	zassert_equal(kfsw_hk_persist_load(), -EBADMSG);
	zassert_equal(kfsw_hk_get_definition(0, entries, &count), -ENOENT);
	zassert_equal(kfsw_hk_clear(0), KFSW_HK_APPLIED_UNSAVED);
	zassert_equal(read_file(after, sizeof(after)), size);
	zassert_mem_equal(after, saved, size);
	kfsw_hk_get_stats(&stats);
	zassert_equal(stats.last_load_error, -EBADMSG);
	zassert_true(stats.settings_dirty);
	zassert_ok(kfsw_hk_save());
}

static atomic_t block_reads;
static K_SEM_DEFINE(read_entered, 0, 1);
static K_SEM_DEFINE(read_release, 0, 1);
ssize_t __real_fs_read(struct fs_file_t *file, void *data, size_t size);

ssize_t __wrap_fs_read(struct fs_file_t *file, void *data, size_t size)
{
	if (atomic_cas(&block_reads, 1, 0)) {
		k_sem_give(&read_entered);
		zassert_ok(k_sem_take(&read_release, K_SECONDS(2)));
	}
	return __real_fs_read(file, data, size);
}

static void restore_thread(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);
	zassert_ok(kfsw_hk_persist_load());
}

ZTEST(kfsw_hk_persistence, test_requests_are_gated_while_restoring)
{
	define_report(0);
	atomic_set(&block_reads, 1);
	k_thread_create(&save_thread_a, save_stack_a, K_THREAD_STACK_SIZEOF(save_stack_a),
			restore_thread, NULL, NULL, NULL, 5, 0, K_NO_WAIT);
	zassert_ok(k_sem_take(&read_entered, K_SECONDS(1)));
	zassert_false(kfsw_hk_is_ready());
	zassert_equal(kfsw_hk_set_period(0, 5000), -EACCES);
	zassert_equal(kfsw_hk_collect(0), -EACCES);
	zassert_equal(kfsw_hk_start(), -EACCES);
	k_sem_give(&read_release);
	zassert_ok(k_thread_join(&save_thread_a, K_SECONDS(1)));
	zassert_true(kfsw_hk_is_ready());
}

ZTEST(kfsw_hk_persistence, test_restore_preserves_samples_and_continues_sequence)
{
	struct fs_file_t file;
	uint8_t saved[512], after[512];
	struct kfsw_hk_sample sample;
	ssize_t size;

	define_report(0);
	zassert_ok(kfsw_hk_set_period(0, CONFIG_KFSW_HK_STORE_FLOOR_MS));
	zassert_ok(kfsw_hk_set_store(0, CONFIG_KFSW_HK_STORE_FLOOR_MS));
	zassert_ok(kfsw_hk_collect(0));
	zassert_ok(kfsw_hk_collect(0));
	fs_file_t_init(&file);
	zassert_ok(fs_open(&file, KFSW_STORAGE_MOUNT_POINT "/hk/report0.bin", FS_O_READ));
	size = fs_read(&file, saved, sizeof(saved));
	zassert_true(size > 12);
	zassert_ok(fs_close(&file));
	zassert_ok(kfsw_hk_persist_load());
	zassert_ok(kfsw_hk_collect(0));
	zassert_ok(kfsw_hk_get(0, 0, &sample));
	zassert_equal(sample.sequence, 2);
	fs_file_t_init(&file);
	zassert_ok(fs_open(&file, KFSW_STORAGE_MOUNT_POINT "/hk/report0.bin", FS_O_READ));
	zassert_equal(fs_read(&file, after, size), size);
	zassert_mem_equal(after, saved, size);
	zassert_ok(fs_close(&file));
}
