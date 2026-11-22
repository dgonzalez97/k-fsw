#include <errno.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/drivers/can/can_fake.h>
#include <zephyr/fff.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include <csp/csp.h>
#include <csp/csp_id.h>
#include <csp/csp_iflist.h>
#include <csp/csp_interface.h>
#include <csp/drivers/can_zephyr.h>

#include <kfsw/comms/can.h>
#include <kfsw/comms/csp.h>
#include <kfsw/services/parameter.h>

#include "parameters/tables.h"

DEFINE_FFF_GLOBALS;

#define TX_BOUND_MS 600
#define COMPLETION_DELAY_MS 10
#define REMOTE_NODE 33U

static const struct device *const second_device = DEVICE_DT_GET(DT_NODELABEL(fake_can_b));
struct completion {
	can_tx_callback_t callback;
	void *user_data;
	const struct device *device;
	int status;
};

static struct completion pending_completion;
static struct completion late_completion;
static int next_status;
static bool auto_complete;
static bool fail_join;
static struct k_thread *held_thread;
static void *tracked_context;
static unsigned int context_frees;
static bool retain_context;
static int sender_result;
static int64_t sender_elapsed_ms;

K_SEM_DEFINE(frame_queued, 0, 1);
K_THREAD_STACK_DEFINE(sender_stack, 2048);
static struct k_thread sender_thread;

extern int __real_z_impl_k_thread_join(struct k_thread *thread, k_timeout_t timeout);
extern void __real_k_free(void *memory);

int __wrap_z_impl_k_thread_join(struct k_thread *thread, k_timeout_t timeout)
{
	if (fail_join) {
		fail_join = false;
		held_thread = thread;
		k_thread_suspend(thread);
		return -EAGAIN;
	}
	return __real_z_impl_k_thread_join(thread, timeout);
}

void __wrap_k_free(void *memory)
{
	if (memory == tracked_context) {
		context_frees++;
		/* Keep a failing live-stop regression from freeing the sender's context. */
		if (retain_context) {
			return;
		}
	}
	__real_k_free(memory);
}

static void complete_frame(struct completion *frame)
{
	can_tx_callback_t callback = frame->callback;

	frame->callback = NULL;
	if (callback != NULL) {
		callback(frame->device, frame->status, frame->user_data);
	}
}

static void completion_expired(struct k_timer *timer)
{
	ARG_UNUSED(timer);
	complete_frame(&pending_completion);
}

static void late_completion_expired(struct k_timer *timer)
{
	ARG_UNUSED(timer);
	complete_frame(&late_completion);
}

K_TIMER_DEFINE(completion_timer, completion_expired, NULL);
K_TIMER_DEFINE(late_timer, late_completion_expired, NULL);

static int queue_frame(const struct device *device, const struct can_frame *frame,
		       k_timeout_t timeout, can_tx_callback_t callback, void *user_data)
{
	ARG_UNUSED(frame);
	ARG_UNUSED(timeout);
	pending_completion.device = device;
	pending_completion.callback = callback;
	pending_completion.user_data = user_data;
	pending_completion.status = next_status;
	k_sem_give(&frame_queued);
	if (auto_complete) {
		k_timer_start(&completion_timer, K_MSEC(2 * COMPLETION_DELAY_MS), K_NO_WAIT);
	}
	return 0;
}

static csp_iface_t *can_interface(void)
{
	csp_iface_t *iface = csp_iflist_get_by_name(CONFIG_KFSW_CSP_CAN_INTERFACE_NAME);

	zassert_not_null(iface);
	zassert_not_null(iface->nexthop);
	return iface;
}

static int send_packet(csp_iface_t *iface, int64_t *elapsed_ms)
{
	csp_packet_t *packet = csp_buffer_get(0);
	int64_t started_ms;
	int result;

	zassert_not_null(packet);
	packet->id.dst = REMOTE_NODE;
	packet->id.src = CONFIG_KFSW_CSP_ADDRESS;
	packet->id.dport = CSP_PING;
	packet->id.sport = 40U;
	packet->id.pri = CSP_PRIO_NORM;
	packet->id.flags = 0U;
	packet->length = 1U;
	packet->data[0] = 0xA5;
	csp_id_prepend(packet);

	started_ms = k_uptime_get();
	result = iface->nexthop(iface, REMOTE_NODE, packet, 1);
	*elapsed_ms = k_uptime_delta(&started_ms);
	/* libcsp only consumes a CAN packet on success. */
	if (result != CSP_ERR_NONE) {
		csp_buffer_free(packet);
	}
	return result;
}

ZTEST(comms_can_tx, test_a_frame_that_is_never_acknowledged_does_not_hold_the_sender)
{
	csp_iface_t *iface = can_interface();
	unsigned int errors = iface->tx_error;
	int64_t elapsed_ms;

	zassert_equal(send_packet(iface, &elapsed_ms), CSP_ERR_DRIVER);
	zassert_true(elapsed_ms < TX_BOUND_MS, "send took %lld ms", elapsed_ms);
	zassert_equal(iface->tx_error, errors + 1U);
}

ZTEST(comms_can_tx, test_callback_error_is_a_bounded_counted_failure)
{
	csp_iface_t *iface = can_interface();
	unsigned int errors = iface->tx_error;
	int64_t elapsed_ms;

	auto_complete = true;
	next_status = -EIO;
	zassert_equal(send_packet(iface, &elapsed_ms), CSP_ERR_DRIVER);
	zassert_equal(fake_can_send_fake.call_count, 1U);
	zassert_is_null(pending_completion.callback);
	zassert_true(elapsed_ms < TX_BOUND_MS, "send took %lld ms", elapsed_ms);
	zassert_equal(iface->tx_error, errors + 1U);
}

ZTEST(comms_can_tx, test_late_success_does_not_complete_the_next_frame)
{
	csp_iface_t *iface = can_interface();
	unsigned int errors = iface->tx_error;
	int64_t elapsed_ms;

	zassert_equal(send_packet(iface, &elapsed_ms), CSP_ERR_DRIVER);
	zassert_true(elapsed_ms < TX_BOUND_MS);
	zassert_not_null(pending_completion.callback);
	late_completion = pending_completion;
	pending_completion.callback = NULL;
	k_timer_start(&late_timer, K_MSEC(COMPLETION_DELAY_MS), K_NO_WAIT);
	/* The next frame gets its own failing completion, after the late success. */
	auto_complete = true;
	next_status = -EIO;
	zassert_equal(send_packet(iface, &elapsed_ms), CSP_ERR_DRIVER);
	zassert_equal(fake_can_send_fake.call_count, 2U);
	zassert_is_null(late_completion.callback);
	zassert_is_null(pending_completion.callback);
	zassert_true(elapsed_ms < TX_BOUND_MS);
	zassert_equal(iface->tx_error, errors + 2U);
}

ZTEST(comms_can_tx, test_missing_completion_prevents_another_frame_being_queued)
{
	csp_iface_t *iface = can_interface();
	csp_can_interface_data_t *data = iface->interface_data;
	const uint8_t payload = 0U;
	unsigned int errors = iface->tx_error;
	int64_t elapsed_ms;
	int64_t started_ms;

	zassert_equal(send_packet(iface, &elapsed_ms), CSP_ERR_DRIVER);
	started_ms = k_uptime_get();
	zassert_equal(data->tx_func(iface->driver_data, 1U, &payload, 1U, NULL), CSP_ERR_TIMEDOUT);
	zassert_true(k_uptime_delta(&started_ms) < TX_BOUND_MS);
	zassert_equal(send_packet(iface, &elapsed_ms), CSP_ERR_DRIVER);
	zassert_true(elapsed_ms < TX_BOUND_MS);
	zassert_equal(fake_can_send_fake.call_count, 1U);
	zassert_equal(iface->tx_error, errors + 2U);
}

static void send_in_thread(void *iface, void *unused2, void *unused3)
{
	ARG_UNUSED(unused2);
	ARG_UNUSED(unused3);
	sender_result = send_packet(iface, &sender_elapsed_ms);
}

ZTEST(comms_can_tx, test_live_bitrate_change_keeps_the_inflight_sender_owned)
{
	csp_iface_t *iface = can_interface();
	struct kfsw_can_info before;
	struct kfsw_can_info after;
	unsigned int errors = iface->tx_error;
	int change_result;

	zassert_ok(kfsw_csp_start());
	kfsw_can_get_info(&before);
	tracked_context = iface->driver_data;
	retain_context = true;
	k_thread_create(&sender_thread, sender_stack, K_THREAD_STACK_SIZEOF(sender_stack),
			send_in_thread, iface, NULL, NULL, K_PRIO_PREEMPT(1), 0, K_NO_WAIT);
	zassert_ok(k_sem_take(&frame_queued, K_MSEC(TX_BOUND_MS)));
	change_result = kfsw_can_set_bitrate(KFSW_CAN_BITRATE_250K);
	pending_completion.status = -EIO;
	complete_frame(&pending_completion);
	zassert_ok(k_thread_join(&sender_thread, K_MSEC(TX_BOUND_MS)));
	kfsw_can_get_info(&after);

	zassert_equal(change_result, -EBUSY);
	zassert_equal(fake_can_stop_fake.call_count, 0U);
	zassert_equal(fake_can_set_timing_fake.call_count, 0U);
	zassert_equal(context_frees, 0U);
	zassert_equal_ptr(can_interface(), iface);
	zassert_equal(after.bitrate, before.bitrate);
	zassert_true(after.ready);
	zassert_equal(sender_result, CSP_ERR_DRIVER);
	zassert_true(sender_elapsed_ms < TX_BOUND_MS);
	zassert_equal(iface->tx_error, errors + 1U);
}

ZTEST(comms_can_tx, test_parameter_write_is_visibly_refused_while_routing)
{
	struct kfsw_param_value before;
	struct kfsw_param_value after;
	struct kfsw_param_value requested;

	zassert_ok(kfsw_csp_start());
	zassert_ok(kfsw_param_get("can_speed", &before));
	requested = before;
	requested.scalar.u32 = KFSW_CAN_BITRATE_250K;
	zassert_equal(kfsw_param_set("can_speed", &requested), -EBUSY);
	zassert_ok(kfsw_param_get("can_speed", &after));
	zassert_equal(after.scalar.u32, before.scalar.u32);
	zassert_equal(fake_can_stop_fake.call_count, 0U);
	requested.scalar.u32 = 123U;
	zassert_equal(kfsw_param_set("can_speed", &requested), -EINVAL);
}

ZTEST(comms_can_tx, test_failed_join_keeps_a_sendable_context_until_retry)
{
	csp_iface_t *iface = NULL;
	unsigned int errors;
	int64_t elapsed_ms;

	zassert_ok(csp_can_open_and_add_interface(second_device, "tx-stop", 7U, 500000U, 0U, 0U,
						  &iface));
	tracked_context = iface->driver_data;
	fail_join = true;
	zassert_equal(csp_can_stop(iface), -EAGAIN);
	zassert_equal_ptr(csp_iflist_get_by_name("tx-stop"), iface);
	zassert_equal(context_frees, 0U);
	zassert_equal(fake_can_remove_rx_filter_fake.call_count, 0U);
	errors = iface->tx_error;
	fake_can_send_fake.custom_fake = NULL;
	fake_can_send_fake.return_val = -ENETDOWN;
	zassert_equal(send_packet(iface, &elapsed_ms), CSP_ERR_DRIVER);
	zassert_true(elapsed_ms < TX_BOUND_MS);
	zassert_equal(iface->tx_error, errors + 1U);
	zassert_equal(fake_can_send_fake.call_count, 1U);
	zassert_equal_ptr(fake_can_send_fake.arg0_val, second_device);
	zassert_equal(context_frees, 0U);

	k_thread_resume(held_thread);
	held_thread = NULL;
	fake_can_stop_fake.return_val = -EALREADY;
	zassert_ok(csp_can_stop(iface));
	zassert_is_null(csp_iflist_get_by_name("tx-stop"));
	zassert_equal(context_frees, 1U);
	zassert_equal(fake_can_remove_rx_filter_fake.call_count, 1U);
}

ZTEST(comms_can_tx, test_invalid_bitrate_leaves_the_controller_unchanged)
{
	struct kfsw_can_info before;
	struct kfsw_can_info after;

	kfsw_can_get_info(&before);
	zassert_equal(kfsw_can_set_bitrate(123U), -EINVAL);
	zassert_ok(kfsw_can_set_bitrate(before.bitrate));
	kfsw_can_get_info(&after);
	zassert_equal(after.bitrate, before.bitrate);
	zassert_equal(fake_can_stop_fake.call_count, 0U);
}

static void can_tx_before(void *fixture)
{
	ARG_UNUSED(fixture);
	auto_complete = false;
	next_status = 0;
	tracked_context = NULL;
	context_frees = 0U;
	retain_context = false;
	k_sem_reset(&frame_queued);
	fake_can_send_fake.custom_fake = queue_frame;
}

static void can_tx_after(void *fixture)
{
	int64_t elapsed_ms;

	ARG_UNUSED(fixture);
	k_timer_stop(&completion_timer);
	k_timer_stop(&late_timer);
	late_completion.status = 0;
	complete_frame(&late_completion);
	pending_completion.status = 0;
	complete_frame(&pending_completion);
	/* Consume any timed-out frame's completion before the next test. */
	auto_complete = true;
	next_status = 0;
	fake_can_send_fake.custom_fake = queue_frame;
	zassert_ok(send_packet(can_interface(), &elapsed_ms));
}

static void *can_tx_setup(void)
{
	const struct kfsw_param_definition_set *const sets[] = {&kfsw_board_param_definitions};
	struct kfsw_can_info info;

	zassert_ok(kfsw_can_set_bitrate(KFSW_CAN_BITRATE_250K));
	kfsw_can_get_info(&info);
	zassert_false(info.ready);
	zassert_equal(info.bitrate, KFSW_CAN_BITRATE_250K);
	zassert_ok(kfsw_csp_init());
	zassert_ok(kfsw_can_set_bitrate(KFSW_CAN_BITRATE_500K));
	zassert_equal(fake_can_stop_fake.call_count, 1U);
	kfsw_can_get_info(&info);
	zassert_true(info.ready);
	zassert_equal(info.bitrate, KFSW_CAN_BITRATE_500K);
	zassert_ok(kfsw_param_init(sets, ARRAY_SIZE(sets)));
	return NULL;
}

ZTEST_SUITE(comms_can_tx, NULL, can_tx_setup, can_tx_before, can_tx_after, NULL);
