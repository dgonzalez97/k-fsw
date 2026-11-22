#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include <kfsw/platform/lastwords.h>
#include <kfsw/services/event.h>
#include <kfsw/services/health.h>

static uint64_t now_ms = UINT64_C(0x1ffffffff);
static uint8_t handle;
static uint32_t feeds;
static bool replace_on_fault;
K_SEM_DEFINE(replace_component, 0, 1);
K_SEM_DEFINE(replaced, 0, 1);

uint64_t __wrap_kfsw_time_monotonic_ms(void)
{
	return now_ms;
}

int __wrap_kfsw_platform_watchdog_release(void)
{
	return 0;
}

int __wrap_kfsw_platform_watchdog_feed(void)
{
	feeds++;
	return 0;
}

void __wrap_kfsw_log_write(uint8_t module, uint8_t severity, const char *format, ...)
{
	ARG_UNUSED(module);
	ARG_UNUSED(format);
	if (severity == 3U && replace_on_fault) {
		replace_on_fault = false;
		k_sem_give(&replace_component);
		zassert_ok(k_sem_take(&replaced, K_SECONDS(1)));
	}
}

static void reporter(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);
	zassert_ok(k_sem_take(&replace_component, K_SECONDS(1)));
	now_ms = UINT64_C(0x300000063);
	zassert_ok(kfsw_health_report(handle));
	zassert_ok(kfsw_health_unregister(handle));
	zassert_ok(kfsw_health_register("replacement", 1000U, &handle));
	k_sem_give(&replaced);
}

K_THREAD_STACK_DEFINE(reporter_stack, 1024);
static struct k_thread reporter_thread;

ZTEST(services_health_fault, test_fault_details_survive_a_concurrent_report_and_reuse)
{
	struct kfsw_lastwords note;
	struct kfsw_event_record event;
	struct kfsw_health_status status;

	zassert_ok(kfsw_health_register("original", 10U, &handle));
	zassert_ok(kfsw_health_start());
	zassert_ok(kfsw_health_evaluate());
	zassert_equal(feeds, 1U);
	zassert_false(kfsw_lastwords_take(&note));

	k_thread_create(&reporter_thread, reporter_stack, K_THREAD_STACK_SIZEOF(reporter_stack),
			reporter, NULL, NULL, NULL, 0, 0, K_NO_WAIT);
	now_ms = UINT64_C(0x200000031);
	replace_on_fault = true;
	zassert_equal(kfsw_health_evaluate(), -ETIMEDOUT);
	zassert_ok(k_thread_join(&reporter_thread, K_SECONDS(1)));
	zassert_equal(feeds, 1U);
	zassert_true(kfsw_lastwords_take(&note));
	zassert_equal(note.reason, KFSW_LASTWORDS_STARVED);
	zassert_equal(note.detail, 50U);
	zassert_equal(note.uptime_ms, 49U);
	zassert_ok(kfsw_event_get(0U, &event));
	zassert_equal(event.id, KFSW_EVENT_HEALTH_FAULT);
	zassert_equal(event.payload_size, strlen("original"));
	zassert_mem_equal(event.payload, "original", event.payload_size);

	zassert_ok(kfsw_health_evaluate());
	zassert_equal(feeds, 2U);
	zassert_ok(kfsw_health_get_status(&status));
	zassert_equal(status.state, KFSW_HEALTH_OK);
}

ZTEST_SUITE(services_health_fault, NULL, NULL, NULL, NULL, NULL);
