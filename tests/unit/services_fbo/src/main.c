#include <errno.h>
#include <string.h>
#include <zephyr/fs/fs.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/ztest.h>
#include <kfsw/platform/storage.h>
#include <kfsw/services/command.h>
#include <kfsw/services/fbo.h>

#define PROCEDURE "/kfsw/ftp/procedures/test"
static atomic_t commands;
static int fail_after = -1;
static bool hide_size;
static K_SEM_DEFINE(entered, 0, 1);
static K_SEM_DEFINE(release_handler, 0, 1);

ssize_t __real_fs_read(struct fs_file_t *file, void *data, size_t size);
int __real_fs_stat(const char *path, struct fs_dirent *entry);

ssize_t __wrap_fs_read(struct fs_file_t *file, void *data, size_t size)
{
	if (fail_after == 0) {
		return -EIO;
	}
	if (fail_after > 0) {
		fail_after--;
	}
	return __real_fs_read(file, data, size);
}

int __wrap_fs_stat(const char *path, struct fs_dirent *entry)
{
	int result = __real_fs_stat(path, entry);

	if (hide_size && (result == 0) && (strcmp(path, PROCEDURE) == 0)) {
		entry->size = 0;
	}
	return result;
}

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

static int gate(const struct kfsw_command_arg *args, size_t count,
		const struct kfsw_command_source *source, struct kfsw_command_result *result)
{
	ARG_UNUSED(args);
	ARG_UNUSED(count);
	ARG_UNUSED(source);
	k_sem_give(&entered);
	if (k_sem_take(&release_handler, K_SECONDS(2)) != 0) {
		return -ETIMEDOUT;
	}
	result->status = KFSW_COMMAND_OK;
	return 0;
}

static void write_procedure(const char *text, size_t size)
{
	struct fs_file_t file;

	fs_file_t_init(&file);
	zassert_ok(fs_open(&file, PROCEDURE, FS_O_CREATE | FS_O_WRITE | FS_O_TRUNC));
	zassert_equal(fs_write(&file, text, size), size);
	zassert_ok(fs_close(&file));
}

static int completed(void)
{
	struct kfsw_fbo_status status;
	int64_t deadline = k_uptime_get() + 1000;

	do {
		zassert_ok(kfsw_fbo_get_status(&status));
		if (!status.running) {
			return status.last_result;
		}
		k_sleep(K_MSEC(1));
	} while (k_uptime_get() < deadline);
	zassert_unreachable("Procedure did not stop within one second");
	return -ETIMEDOUT;
}

static void run_text(const char *text, int expected)
{
	write_procedure(text, strlen(text));
	zassert_ok(kfsw_fbo_run("test"));
	zassert_equal(completed(), expected);
}

static void *setup(void)
{
	static const struct kfsw_command_definition definitions[] = {
		{.id = 1, .name = "count", .handler = handler},
		{.id = 2, .name = "gate", .handler = gate},
	};
	static const struct kfsw_command_definition_set set = {
		.commands = definitions,
		.count = ARRAY_SIZE(definitions),
	};
	const struct kfsw_command_definition_set *sets[] = {&set};
	const struct flash_area *area;

	zassert_ok(
		flash_area_open(DT_FIXED_PARTITION_ID(DT_CHOSEN(kfsw_storage_partition)), &area));
	zassert_ok(flash_area_flatten(area, 0, area->fa_size));
	flash_area_close(area);
	zassert_ok(kfsw_storage_init());
	zassert_ok(kfsw_storage_mount());
	zassert_ok(fs_mkdir("/kfsw/ftp"));
	zassert_ok(fs_mkdir("/kfsw/ftp/procedures"));
	zassert_ok(kfsw_command_init(sets, ARRAY_SIZE(sets)));
	zassert_ok(kfsw_fbo_init());
	return NULL;
}

static void before(void *fixture)
{
	ARG_UNUSED(fixture);
	atomic_set(&commands, 0);
	fail_after = -1;
	hide_size = false;
	k_sem_reset(&entered);
	k_sem_reset(&release_handler);
}

ZTEST(services_fbo, test_idle_stop_does_not_cancel_next_run)
{
	zassert_ok(kfsw_fbo_stop());
	run_text("# comment\n\ncount\ncount", 0);
	zassert_equal(atomic_get(&commands), 2);
}

ZTEST(services_fbo, test_stop_wakes_long_wait_even_when_errors_continue)
{
	const char text[] = "on-error continue\ngate\nwait 600\ncount\n";
	int64_t start;

	write_procedure(text, sizeof(text) - 1U);
	zassert_ok(kfsw_fbo_run("test"));
	zassert_ok(k_sem_take(&entered, K_SECONDS(1)));
	zassert_equal(kfsw_fbo_run("test"), -EBUSY);
	k_sem_give(&release_handler);
	k_sleep(K_MSEC(20));
	start = k_uptime_get();
	zassert_ok(kfsw_fbo_stop());
	zassert_equal(completed(), -ECANCELED);
	zassert_true(k_uptime_get() - start < 100);
	zassert_equal(atomic_get(&commands), 0);
	run_text("count\n", 0);
	zassert_equal(atomic_get(&commands), 1);
}

ZTEST(services_fbo, test_read_failure_does_not_execute_partial_line)
{
	fail_after = 5;
	run_text("count\n", -EIO);
	zassert_equal(atomic_get(&commands), 0);
}

ZTEST(services_fbo, test_guard_ranges_and_unsigned_sign)
{
	const char *invalid[] = {"if-event 65536 1 skip\ncount\n", "if-event 1 65536 skip\ncount\n",
				 "if-event -1 1 skip\ncount\n", "wait -0\ncount\n",
				 "wait 4294967296\ncount\n"};

	for (size_t i = 0; i < ARRAY_SIZE(invalid); i++) {
		run_text(invalid[i], -EINVAL);
	}
	zassert_equal(atomic_get(&commands), 0);
}

ZTEST(services_fbo, test_byte_limit_counts_comments_and_growth)
{
	char text[CONFIG_KFSW_FBO_BYTES_MAX + 2];

	memset(text, '\n', sizeof(text));
	write_procedure(text, sizeof(text));
	zassert_equal(kfsw_fbo_run("test"), -EFBIG);
	hide_size = true;
	zassert_ok(kfsw_fbo_run("test"));
	zassert_equal(completed(), -EFBIG);
	zassert_equal(atomic_get(&commands), 0);
}

ZTEST(services_fbo, test_continue_preserves_first_failure)
{
	run_text("on-error continue\nmissing\ncount\n", -ENOENT);
	zassert_equal(atomic_get(&commands), 1);
}

ZTEST(services_fbo, test_line_limit_is_reported)
{
	run_text("count\ncount\ncount\ncount\ncount\ncount\ncount\ncount\ncount\n", -E2BIG);
	zassert_equal(atomic_get(&commands), CONFIG_KFSW_FBO_LINES_MAX);
}

ZTEST_SUITE(services_fbo, NULL, setup, before, NULL, NULL);
