#include <errno.h>
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/ztest.h>
#include <kfsw/modules/temperature_sensor_example.h>

static K_SEM_DEFINE(fetch_entered, 0, 1);
static K_SEM_DEFINE(fetch_release, 0, 1);
static K_SEM_DEFINE(system_ran, 0, 1);
static uint64_t now_ms = 100;
static int fetch_result;
static bool stack_found;

uint64_t __wrap_kfsw_time_monotonic_ms(void)
{
	return now_ms;
}

static int fetch(const struct device *device, enum sensor_channel channel)
{
	ARG_UNUSED(device);
	zassert_equal(channel, SENSOR_CHAN_DIE_TEMP);
	k_sem_give(&fetch_entered);
	k_sem_take(&fetch_release, K_FOREVER);
	return fetch_result;
}

static int channel_get(const struct device *device, enum sensor_channel channel,
		       struct sensor_value *value)
{
	ARG_UNUSED(device);
	zassert_equal(channel, SENSOR_CHAN_DIE_TEMP);
	value->val1 = 25;
	value->val2 = 500000;
	return 0;
}

static DEVICE_API(sensor, test_api) = {.sample_fetch = fetch, .channel_get = channel_get};
DEVICE_DT_DEFINE(DT_NODELABEL(test_temperature), NULL, NULL, NULL, NULL, POST_KERNEL,
		 CONFIG_KERNEL_INIT_PRIORITY_DEVICE, &test_api);

static void system_handler(struct k_work *work)
{
	ARG_UNUSED(work);
	k_sem_give(&system_ran);
}
static K_WORK_DEFINE(system_work, system_handler);

static void check_system_queue(void)
{
	zassert_true(k_work_submit(&system_work) >= 0);
	zassert_ok(k_sem_take(&system_ran, K_MSEC(100)));
}

static void inspect_stack(const struct k_thread *thread, void *data)
{
	ARG_UNUSED(data);
	const char *name = k_thread_name_get((k_tid_t)thread);
	size_t unused;

	if ((name == NULL) || strcmp(name, "kfsw_temp")) {
		return;
	}
	zassert_ok(k_thread_stack_space_get(thread, &unused));
	printk("sensor stack: %zu bytes unused\n", unused);
	zassert_true(unused >= 256);
	stack_found = true;
}

ZTEST(modules_sensor_queue, test_blocked_sensor_leaves_system_work_and_cache_reads_available)
{
	struct kfsw_temp_example_reading reading;

	zassert_ok(kfsw_temp_example_init());
	zassert_ok(k_sem_take(&fetch_entered, K_SECONDS(1)));
	zassert_ok(kfsw_temp_example_get(&reading));
	zassert_false(reading.valid);
	check_system_queue();

	k_sem_give(&fetch_release);
	zassert_ok(k_sem_take(&fetch_entered, K_SECONDS(1)));
	zassert_ok(kfsw_temp_example_get(&reading));
	zassert_true(reading.valid);
	zassert_equal(reading.milli_c, 25500);
	now_ms += CONFIG_KFSW_TEMP_EXAMPLE_MAX_AGE_MS;
	zassert_ok(kfsw_temp_example_get(&reading));
	zassert_true(reading.valid);
	now_ms++;
	zassert_ok(kfsw_temp_example_get(&reading));
	zassert_false(reading.valid);
	check_system_queue();
	k_thread_foreach(inspect_stack, NULL);
	zassert_true(stack_found);

	fetch_result = -EIO;
	k_sem_give(&fetch_release);
	zassert_ok(k_sem_take(&fetch_entered, K_SECONDS(1)));
	zassert_ok(kfsw_temp_example_get(&reading));
	zassert_false(reading.valid);
	zassert_equal(reading.failures, 1);

	fetch_result = 0;
	k_sem_give(&fetch_release);
	zassert_ok(k_sem_take(&fetch_entered, K_SECONDS(1)));
	zassert_ok(kfsw_temp_example_get(&reading));
	zassert_true(reading.valid);
	zassert_equal(reading.samples, 2);
}

ZTEST_SUITE(modules_sensor_queue, NULL, NULL, NULL, NULL, NULL);
