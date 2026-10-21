#include <errno.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>
#include <kfsw/platform/semihost.h>
#include <kfsw/services/log.h>
#include <kfsw/services/log_history.h>

static bool real_context;
static int context_result;
static long write_result;
static unsigned int writes;
static char captured[KFSW_SEMIHOST_MAX_WRITE + 1U];
static bool reenter;

int __real_kfsw_semihost_context(void);
long __real_kfsw_semihost_target_write(const char *text, size_t size);

int __wrap_kfsw_semihost_context(void)
{
	return real_context ? __real_kfsw_semihost_context() : context_result;
}

long __wrap_kfsw_semihost_target_write(const char *text, size_t size)
{
	struct kfsw_log_history_window window;
	unsigned int key = arch_irq_lock();

	arch_irq_unlock(key);
	zassert_false(k_is_in_isr());
	zassert_true(arch_irq_unlocked(key));
	zassert_ok(kfsw_log_history_window(1U, &window));
	zassert_true(size <= KFSW_SEMIHOST_MAX_WRITE);
	memcpy(captured, text, size);
	captured[size] = '\0';
	writes++;
	if (reenter) {
		zassert_equal(kfsw_semihost_write("nested", 6U), -EBUSY);
	}
	return write_result;
}

static void before(void *fixture)
{
	ARG_UNUSED(fixture);
	real_context = false;
	context_result = 0;
	write_result = 0L;
	writes = 0U;
	reenter = false;
	captured[0] = '\0';
	zassert_ok(kfsw_log_set_level(0U));
}

ZTEST(log_semihost, test_sink_rejects_invalid_and_unsupported_calls)
{
	zassert_equal(kfsw_semihost_write(NULL, 1U), -EINVAL);
	zassert_equal(kfsw_semihost_write("", 0U), -EINVAL);
	zassert_equal(kfsw_semihost_write("x", KFSW_SEMIHOST_MAX_WRITE + 1U), -EINVAL);
	real_context = true;
	zassert_equal(kfsw_semihost_write("x", 1U), -ENOTSUP);
	zassert_equal(__real_kfsw_semihost_target_write("x", 1U), -ENOTSUP);
	zassert_equal(writes, 0U);
}

ZTEST(log_semihost, test_sink_skips_detached_and_unsafe_contexts)
{
	const int errors[] = {-ENODEV, -EWOULDBLOCK, -ENOTSUP};

	for (size_t i = 0U; i < ARRAY_SIZE(errors); i++) {
		context_result = errors[i];
		zassert_equal(kfsw_semihost_write("x", 1U), errors[i]);
	}
	zassert_equal(writes, 0U);
}

ZTEST(log_semihost, test_sink_write_results_and_reentry)
{
	const long results[] = {0L, -1L, 1L, 3L, 99L};

	reenter = true;
	for (size_t i = 0U; i < ARRAY_SIZE(results); i++) {
		write_result = results[i];
		zassert_equal(kfsw_semihost_write("abc", 3U), results[i] == 0L ? 0 : -EIO);
		zassert_equal(writes, i + 1U);
		zassert_str_equal(captured, "abc");
	}
	write_result = 0L;
	zassert_ok(kfsw_semihost_write("next", 4U));
}

ZTEST(log_semihost, test_sink_maximum_write)
{
	char text[KFSW_SEMIHOST_MAX_WRITE];

	memset(text, 'x', sizeof(text));
	zassert_ok(kfsw_semihost_write(text, sizeof(text)));
	zassert_equal(writes, 1U);
	zassert_equal(strlen(captured), sizeof(text));
	zassert_mem_equal(captured, text, sizeof(text));
}

ZTEST(log_semihost, test_rendered_text_level_and_marker_boundaries)
{
	kfsw_log_warning("value %u %s\nnext\r", 42U, "RAM");
#if CONFIG_KFSW_LOG_COLOR
	zassert_str_equal(captured, "\033[33m[WARNING] value 42 RAM next \033[0m\n");
#else
	zassert_str_equal(captured, "[WARNING] value 42 RAM next \n");
#endif
	zassert_equal(writes, 1U);
	zassert_ok(kfsw_log_set_level(4U));
	kfsw_log_warning("filtered");
	zassert_equal(writes, 1U);
	kfsw_log_marker("@MARKER value=%u\nsecond", 7U);
	zassert_str_equal(captured, "@MARKER value=7\nsecond\n");
	zassert_equal(writes, 2U);
}

ZTEST(log_semihost, test_full_local_text_survives_history_fallback)
{
	char message[240];
	char expected[240];
	struct kfsw_log_history_window window;
	struct kfsw_log_record record;

	memset(message, 'x', sizeof(message) - 1U);
	message[sizeof(message) - 1U] = '\0';
	kfsw_log_error("%s", message);
#if CONFIG_KFSW_LOG_COLOR
	strcpy(expected, "\033[31m[ERROR] ");
#else
	strcpy(expected, "[ERROR] ");
#endif
	strncat(expected, message, 191U);
#if CONFIG_KFSW_LOG_COLOR
	strcat(expected, "\033[0m");
#endif
	strcat(expected, "\n");
	zassert_str_equal(captured, expected);
	zassert_ok(kfsw_log_history_window(1U, &window));
	zassert_ok(kfsw_log_history_get(window.first, &record));
	zassert_true(record.truncated);
	zassert_true(strlen(record.text) < 191U);
}

ZTEST(log_semihost, test_sink_failure_leaves_history_available)
{
	struct kfsw_log_history_window window;
	struct kfsw_log_record record;

	write_result = -1L;
	kfsw_log_error("host write failed");
	zassert_equal(writes, 1U);
	zassert_ok(kfsw_log_history_window(1U, &window));
	zassert_ok(kfsw_log_history_get(window.first, &record));
	zassert_str_equal(record.text, "host write failed");
}

ZTEST_SUITE(log_semihost, NULL, NULL, before, NULL, NULL);
