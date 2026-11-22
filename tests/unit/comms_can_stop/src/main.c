#include <errno.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/drivers/can/can_fake.h>
#include <zephyr/fff.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include <csp/csp.h>
#include <csp/csp_iflist.h>
#include <csp/drivers/can_zephyr.h>

#include <kfsw/comms/can.h>

DEFINE_FFF_GLOBALS;

static const struct device *const devices[] = {
	DEVICE_DT_GET(DT_NODELABEL(fake_can_a)),
	DEVICE_DT_GET(DT_NODELABEL(fake_can_b)),
	DEVICE_DT_GET(DT_NODELABEL(fake_can_c)),
};

/* Wrap kernel boundaries, not the CAN lifetime code under test. Refuse an
 * attempted live-stack reuse before it can corrupt the test process.
 */
static struct {
	struct k_thread *thread;
	k_thread_stack_t *stack;
} live[CONFIG_CSP_CAN_RX_THREAD_NUM];
static bool track_creation;
static bool fail_join;
static struct k_thread *held_thread;
static struct k_msgq *last_queue;
static void *last_context;
static unsigned int context_frees;
static unsigned int stack_reuses;
static can_tx_callback_t pending_callback;
static void *pending_user_data;

extern k_tid_t __real_z_impl_k_thread_create(struct k_thread *thread, k_thread_stack_t *stack,
					     size_t size, k_thread_entry_t entry, void *p1,
					     void *p2, void *p3, int priority, uint32_t options,
					     k_timeout_t delay);
extern int __real_z_impl_k_thread_join(struct k_thread *thread, k_timeout_t timeout);
extern void __real_k_free(void *memory);

k_tid_t __wrap_z_impl_k_thread_create(struct k_thread *thread, k_thread_stack_t *stack, size_t size,
				      k_thread_entry_t entry, void *p1, void *p2, void *p3,
				      int priority, uint32_t options, k_timeout_t delay)
{
	if (track_creation) {
		for (size_t index = 0U; index < ARRAY_SIZE(live); index++) {
			if ((live[index].thread != NULL) && (live[index].stack == stack)) {
				stack_reuses++;
				return NULL;
			}
		}
		for (size_t index = 0U; index < ARRAY_SIZE(live); index++) {
			if (live[index].thread == NULL) {
				live[index].thread = thread;
				live[index].stack = stack;
				break;
			}
		}
		last_queue = p1;
		last_context = ((csp_iface_t *)p2)->driver_data;
		context_frees = 0U;
	}
	return __real_z_impl_k_thread_create(thread, stack, size, entry, p1, p2, p3, priority,
					     options, delay);
}

int __wrap_z_impl_k_thread_join(struct k_thread *thread, k_timeout_t timeout)
{
	int result;

	if (fail_join) {
		fail_join = false;
		held_thread = thread;
		k_thread_suspend(thread);
		return -EAGAIN;
	}
	result = __real_z_impl_k_thread_join(thread, timeout);
	if (result == 0) {
		for (size_t index = 0U; index < ARRAY_SIZE(live); index++) {
			if (live[index].thread == thread) {
				live[index].thread = NULL;
			}
		}
	}
	return result;
}

void __wrap_k_free(void *memory)
{
	if (memory == last_context) {
		context_frees++;
	}
	__real_k_free(memory);
}

static int open_interface(size_t device_index, const char *name, csp_iface_t **iface)
{
	int result;

	track_creation = true;
	result = csp_can_open_and_add_interface(devices[device_index], name, 7U, 500000U, 0U, 0U,
						iface);
	track_creation = false;
	return result;
}

ZTEST(comms_can_stop, test_successful_stop_releases_context_and_slot)
{
	csp_iface_t *iface = NULL;

	zassert_ok(open_interface(0U, "stop-a", &iface));
	zassert_ok(csp_can_stop(iface));
	zassert_is_null(csp_iflist_get_by_name("stop-a"));
	zassert_equal(context_frees, 1U);
	zassert_equal(fake_can_remove_rx_filter_fake.call_count, 1U);
	zassert_ok(open_interface(0U, "stop-a", &iface));
	zassert_ok(csp_can_stop(iface));
	zassert_equal(context_frees, 1U);
}

ZTEST(comms_can_stop, test_controller_stop_error_keeps_owned_resources)
{
	csp_iface_t *iface = NULL;

	zassert_ok(open_interface(0U, "stop-a", &iface));
	fake_can_stop_fake.return_val = -EIO;
	zassert_equal(csp_can_stop(iface), -EIO);
	zassert_equal_ptr(csp_iflist_get_by_name("stop-a"), iface);
	zassert_equal(context_frees, 0U);
	zassert_equal(fake_can_remove_rx_filter_fake.call_count, 0U);
	fake_can_stop_fake.return_val = 0;
	zassert_ok(csp_can_stop(iface));
	zassert_equal(context_frees, 1U);
}

ZTEST(comms_can_stop, test_failed_join_retains_queue_context_and_stop_request)
{
	csp_iface_t *first = NULL;
	csp_iface_t *second = NULL;
	csp_iface_t *third = NULL;
	struct k_msgq *queue;
	struct can_frame frame = {0};

	zassert_ok(open_interface(0U, "stop-a", &first));
	queue = last_queue;
	zassert_ok(open_interface(1U, "stop-b", &second));
	last_context = first->driver_data;
	context_frees = 0U;
	fail_join = true;
	zassert_equal(csp_can_stop(first), -EAGAIN);
	zassert_equal_ptr(csp_iflist_get_by_name("stop-a"), first);
	zassert_equal(context_frees, 0U);
	zassert_equal(fake_can_remove_rx_filter_fake.call_count, 0U);
	zassert_ok(k_msgq_put(queue, &frame, K_NO_WAIT));
	zassert_equal(k_msgq_num_used_get(queue), 1U);
	zassert_not_equal(open_interface(2U, "stop-c", &third), 0);
	zassert_is_null(third);
	zassert_equal(stack_reuses, 0U);

	k_thread_resume(held_thread);
	held_thread = NULL;
	fake_can_stop_fake.return_val = -EALREADY;
	zassert_ok(csp_can_stop(first));
	zassert_equal(context_frees, 1U);
	fake_can_stop_fake.return_val = 0;
	zassert_ok(csp_can_stop(second));
}

ZTEST(comms_can_stop, test_out_of_order_stop_does_not_reuse_a_live_stack)
{
	csp_iface_t *first = NULL;
	csp_iface_t *second = NULL;
	csp_iface_t *third = NULL;

	zassert_ok(open_interface(0U, "stop-a", &first));
	zassert_ok(open_interface(1U, "stop-b", &second));
	zassert_ok(csp_can_stop(first));
	zassert_ok(open_interface(2U, "stop-c", &third));
	zassert_equal(stack_reuses, 0U);
	zassert_ok(csp_can_stop(second));
	zassert_ok(csp_can_stop(third));
}

static int remember_tx(const struct device *device, const struct can_frame *frame,
		       k_timeout_t timeout, can_tx_callback_t callback, void *user_data)
{
	ARG_UNUSED(device);
	ARG_UNUSED(frame);
	ARG_UNUSED(timeout);
	pending_callback = callback;
	pending_user_data = user_data;
	return 0;
}

ZTEST(comms_can_stop, test_pending_tx_callback_keeps_context_until_completion)
{
	csp_iface_t *iface = NULL;
	csp_can_interface_data_t *data;
	const uint8_t payload = 0U;

	zassert_ok(open_interface(0U, "stop-a", &iface));
	data = iface->interface_data;
	fake_can_send_fake.custom_fake = remember_tx;
	zassert_equal(data->tx_func(iface->driver_data, 1U, &payload, 1U, NULL), CSP_ERR_TIMEDOUT);
	zassert_not_null(pending_callback);
	zassert_equal(csp_can_stop(iface), CSP_ERR_TIMEDOUT);
	zassert_equal(context_frees, 0U);
	zassert_equal_ptr(csp_iflist_get_by_name("stop-a"), iface);
	zassert_equal(fake_can_remove_rx_filter_fake.call_count, 0U);
	pending_callback(devices[0], -ENETDOWN, pending_user_data);
	fake_can_stop_fake.return_val = -EALREADY;
	zassert_ok(csp_can_stop(iface));
	zassert_equal(context_frees, 1U);
}

ZTEST(comms_can_stop, test_start_failure_and_failed_join_do_not_free_live_rx)
{
	csp_iface_t *iface;
	csp_iface_t *second = NULL;
	csp_iface_t *third = NULL;
	struct kfsw_can_info info;

	fake_can_start_fake.return_val = -EIO;
	fail_join = true;
	track_creation = true;
	zassert_equal(kfsw_can_open(), -EIO);
	track_creation = false;
	kfsw_can_get_info(&info);
	zassert_false(info.ready);
	iface = csp_iflist_get_by_name(CONFIG_KFSW_CSP_CAN_INTERFACE_NAME);
	zassert_not_null(iface);
	zassert_equal(context_frees, 0U);
	zassert_equal(fake_can_remove_rx_filter_fake.call_count, 0U);
	fake_can_start_fake.return_val = 0;
	zassert_ok(open_interface(1U, "stop-b", &second));
	zassert_not_equal(open_interface(2U, "stop-c", &third), 0);
	zassert_is_null(third);
	zassert_equal(stack_reuses, 0U);
	last_context = iface->driver_data;
	context_frees = 0U;
	k_thread_resume(held_thread);
	held_thread = NULL;
	fake_can_stop_fake.return_val = -EALREADY;
	zassert_ok(csp_can_stop(iface));
	zassert_equal(context_frees, 1U);
	fake_can_stop_fake.return_val = 0;
	zassert_ok(csp_can_stop(second));
}

ZTEST(comms_can_stop, test_invalid_inputs_do_not_touch_controller)
{
	csp_iface_t invalid = {0};
	struct kfsw_can_info before;
	struct kfsw_can_info after;

	zassert_equal(csp_can_stop(NULL), CSP_ERR_INVAL);
	zassert_equal(csp_can_stop(&invalid), CSP_ERR_INVAL);
	kfsw_can_get_info(&before);
	zassert_equal(kfsw_can_set_bitrate(123U), -EINVAL);
	kfsw_can_get_info(&after);
	zassert_equal(after.bitrate, before.bitrate);
	zassert_equal(fake_can_stop_fake.call_count, 0U);
}

static void *can_stop_setup(void)
{
	csp_conf.version = 2U;
	csp_init();
	return NULL;
}

ZTEST_SUITE(comms_can_stop, NULL, can_stop_setup, NULL, NULL, NULL);
