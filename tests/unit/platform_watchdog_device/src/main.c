#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/irq_offload.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/ztest.h>

#include <kfsw/platform/watchdog.h>

K_SEM_DEFINE(feed_seen, 0, 1);
static atomic_t feeds;
static atomic_t wake_on_feed;
static int isr_result;
static int work_result;
K_SEM_DEFINE(work_done, 0, 1);

static uint8_t setup_options[2];
static unsigned int setups;
static bool feed_fails;

/* The first call refuses the debugger-pause option, so the fallback is taken. */
static int fake_setup(const struct device *device, uint8_t options)
{
	ARG_UNUSED(device);
	if (setups < ARRAY_SIZE(setup_options)) {
		setup_options[setups] = options;
	}
	setups++;
	return (setups == 1U) ? -ENOTSUP : 0;
}

static int fake_install(const struct device *device, const struct wdt_timeout_cfg *timeout)
{
	ARG_UNUSED(device);
	zassert_equal(timeout->window.max, 300U);
	return 0;
}

static int fake_feed(const struct device *device, int channel)
{
	ARG_UNUSED(device);
	ARG_UNUSED(channel);
	if (feed_fails) {
		return -EIO;
	}
	atomic_inc(&feeds);
	if (atomic_get(&wake_on_feed)) {
		k_sem_give(&feed_seen);
	}
	return 0;
}

static DEVICE_API(wdt, test_api) = {
	.setup = fake_setup,
	.install_timeout = fake_install,
	.feed = fake_feed,
};

DEVICE_DT_DEFINE(DT_NODELABEL(test_watchdog), NULL, NULL, NULL, NULL, PRE_KERNEL_1,
		 CONFIG_KERNEL_INIT_PRIORITY_DEVICE, &test_api);

static void release_in_isr(const void *argument)
{
	ARG_UNUSED(argument);
	isr_result = kfsw_platform_watchdog_release();
}

static void release_in_work(struct k_work *work)
{
	ARG_UNUSED(work);
	work_result = kfsw_platform_watchdog_release();
	k_sem_give(&work_done);
}

K_WORK_DEFINE(release_work, release_in_work);

/* One test: the module state is a single global and never walks back. */
ZTEST(platform_watchdog_device, test_handover_drains_the_preempted_keepalive)
{
	struct kfsw_platform_watchdog_info info;
	atomic_val_t feeds_after;
	uint32_t counted;

	zassert_equal(kfsw_platform_watchdog_configured_timeout_ms(), 300U);

	/* Nothing works before the device is claimed. */
	zassert_equal(kfsw_platform_watchdog_start(), -EINVAL);
	zassert_equal(kfsw_platform_watchdog_feed(), -EINVAL);
	zassert_equal(kfsw_platform_watchdog_release(), -EINVAL);
	zassert_equal(kfsw_platform_watchdog_stop_feeding(), -EINVAL);
	zassert_ok(kfsw_platform_watchdog_get_info(&info));
	zassert_equal(info.state, KFSW_PLATFORM_WATCHDOG_UNCONFIGURED);
	zassert_equal(info.feeds, 0U);
	zassert_equal(info.since_feed_ms, 0U);
	zassert_false(info.device_bound);

	zassert_ok(kfsw_platform_watchdog_init());
	zassert_equal(kfsw_platform_watchdog_init(), -EALREADY);
	zassert_ok(kfsw_platform_watchdog_start());
	zassert_equal(setups, 2U);
	zassert_equal(setup_options[0], WDT_OPT_PAUSE_HALTED_BY_DBG);
	zassert_equal(setup_options[1], 0U);

	irq_offload(release_in_isr, NULL);
	zassert_equal(isr_result, -EWOULDBLOCK);
	zassert_true(k_work_submit(&release_work) >= 0);
	zassert_ok(k_sem_take(&work_done, K_SECONDS(1)));
	zassert_equal(work_result, -EWOULDBLOCK);
	zassert_ok(kfsw_platform_watchdog_get_info(&info));
	zassert_true(info.keepalive_owned);

	/* The workqueue wakes this higher-priority thread from the driver's
	 * feed call. Release therefore races the handler's reschedule.
	 */
	atomic_set(&wake_on_feed, 1);
	zassert_ok(k_sem_take(&feed_seen, K_SECONDS(1)));
	zassert_ok(kfsw_platform_watchdog_release());
	zassert_ok(kfsw_platform_watchdog_get_info(&info));
	zassert_false(info.keepalive_owned);
	feeds_after = atomic_get(&feeds);
	k_sleep(K_MSEC(350));
	zassert_equal(atomic_get(&feeds), feeds_after);
	zassert_ok(kfsw_platform_watchdog_feed());
	zassert_equal(atomic_get(&feeds), feeds_after + 1);

	/* A driver refusal reaches the caller and does not count as a feed. */
	zassert_ok(kfsw_platform_watchdog_get_info(&info));
	counted = info.feeds;
	feed_fails = true;
	zassert_equal(kfsw_platform_watchdog_feed(), -EIO);
	feed_fails = false;
	zassert_ok(kfsw_platform_watchdog_get_info(&info));
	zassert_equal(info.feeds, counted);

	zassert_ok(kfsw_platform_watchdog_stop_feeding());
	zassert_equal(kfsw_platform_watchdog_feed(), -EPERM);
	/* Starved is terminal: the hardware is counting down to the reset. */
	zassert_equal(kfsw_platform_watchdog_stop_feeding(), -EINVAL);
	zassert_equal(kfsw_platform_watchdog_release(), -EINVAL);
	zassert_equal(kfsw_platform_watchdog_start(), -EINVAL);
	zassert_ok(kfsw_platform_watchdog_get_info(&info));
	zassert_equal(info.state, KFSW_PLATFORM_WATCHDOG_STARVED);
}

ZTEST_SUITE(platform_watchdog_device, NULL, NULL, NULL, NULL, NULL);
