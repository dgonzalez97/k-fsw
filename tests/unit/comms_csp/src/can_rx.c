#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/ztest.h>

#include <csp/csp.h>
#include <csp/csp_buffer.h>
#include <csp/csp_debug.h>
#include <csp/csp_id.h>
#include <csp/interfaces/csp_if_can.h>

#include <kfsw/comms/csp.h>

/* Private to libcsp; the test needs it to drain the reserve that csp_buffer_get() keeps back. */
csp_packet_t *csp_buffer_get_always(void);

#define LOCAL_ADDRESS 7U
#define REMOTE_ADDRESS 3U
#define FORWARDED_ADDRESS 9U
#define BEGIN_DATA_BYTES 8U
#define POOL_SIZE CONFIG_CSP_BUFFER_COUNT
#define PARTIAL_LIMIT CONFIG_CSP_CAN_PBUF_MAX
#define FLOOD_KEYS 256U
#define REFRESH_INTERVAL_MS 600U
#define EXPIRY_WAIT_MS 1100U

BUILD_ASSERT(PARTIAL_LIMIT < FLOOD_KEYS);

static csp_can_interface_data_t can_data;
static csp_iface_t can_iface;
static csp_packet_t *held[POOL_SIZE];
static size_t held_count;

struct begin_frame {
	uint32_t id;
	uint8_t data[8];
	uint8_t dlc;
};

static int tx_stub(void *driver_data, uint32_t id, const uint8_t *data, uint8_t dlc,
		   const csp_packet_t *packet)
{
	ARG_UNUSED(driver_data);
	ARG_UNUSED(id);
	ARG_UNUSED(data);
	ARG_UNUSED(dlc);
	ARG_UNUSED(packet);
	return CSP_ERR_NONE;
}

/* First frame of a two-frame packet, so a successful receive leaves a pbuf in the list. */
static struct begin_frame make_begin(uint16_t dst, uint8_t sender, uint8_t counter)
{
	static csp_packet_t scratch;
	struct begin_frame frame = {0};

	memset(&scratch, 0, sizeof(scratch));
	scratch.id.pri = 2;
	scratch.id.src = REMOTE_ADDRESS;
	scratch.id.dst = dst;
	scratch.id.dport = 10;
	scratch.id.sport = 11;
	scratch.length = BEGIN_DATA_BYTES;
	csp_id_prepend(&scratch);

	if (csp_conf.version == 1) {
		uint16_t length = sys_cpu_to_be16(BEGIN_DATA_BYTES);

		frame.id = CFP_MAKE_SRC(REMOTE_ADDRESS) | CFP_MAKE_DST(dst) | CFP_MAKE_TYPE(0) |
			   CFP_MAKE_REMAIN(1) | CFP_MAKE_ID(counter);
		memcpy(frame.data, scratch.frame_begin, 4);
		memcpy(&frame.data[4], &length, sizeof(length));
		frame.dlc = 6;
	} else {
		frame.id = ((uint32_t)scratch.id.pri << CFP2_PRIO_OFFSET) |
			   ((uint32_t)dst << CFP2_DST_OFFSET) |
			   ((uint32_t)sender << CFP2_SENDER_OFFSET) |
			   ((uint32_t)(counter & CFP2_SC_MASK) << CFP2_SC_OFFSET) |
			   (CFP2_BEGIN_MASK << CFP2_BEGIN_OFFSET);
		memcpy(frame.data, &scratch.frame_begin[2], 4);
		frame.dlc = 4;
	}

	return frame;
}

static int receive(const struct begin_frame *frame, bool isr)
{
	int woken = 0;

	return csp_can_rx(&can_iface, frame->id, frame->data, frame->dlc, 0U, isr ? &woken : NULL);
}

static void drain_pool(void)
{
	/* Only called while buffers remain, so the panicking allocator cannot fire here. */
	while (csp_buffer_remaining() > 0) {
		zassert_true(held_count < POOL_SIZE);
		held[held_count++] = csp_buffer_get_always();
	}
	zassert_equal(csp_buffer_remaining(), 0);
}

static void release(size_t count)
{
	while (count-- > 0U && held_count > 0U) {
		csp_buffer_free(held[--held_count]);
	}
}

static void *suite_setup(void)
{
	csp_conf.version = CONFIG_KFSW_CSP_VERSION;
	csp_init();
	return NULL;
}

static void before(void *fixture)
{
	ARG_UNUSED(fixture);

	csp_buffer_init();
	held_count = 0U;
	memset(&can_data, 0, sizeof(can_data));
	memset(&can_iface, 0, sizeof(can_iface));
	can_data.tx_func = tx_stub;
	can_iface.name = "TCAN";
	can_iface.addr = LOCAL_ADDRESS;
	can_iface.interface_data = &can_data;
	zassert_equal(csp_can_add_interface(&can_iface), CSP_ERR_NONE);
}

static void after(void *fixture)
{
	ARG_UNUSED(fixture);

	while (can_data.pbufs != NULL) {
		csp_packet_t *packet = can_data.pbufs;

		can_data.pbufs = packet->next;
		csp_buffer_free(packet);
	}
	release(held_count);
	zassert_equal(csp_can_remove_interface(&can_iface), CSP_ERR_NONE);
	zassert_equal(csp_buffer_remaining(), POOL_SIZE);
	kfsw_csp_clear_counters();
}

static void check_exhausted_begin(uint16_t dst, bool isr)
{
	struct begin_frame frame = make_begin(dst, REMOTE_ADDRESS, 1);

	drain_pool();

	for (unsigned int attempt = 1U; attempt <= 3U; attempt++) {
		zassert_equal(receive(&frame, isr), CSP_ERR_NOBUFS);
		zassert_equal(can_iface.drop, attempt);
		zassert_is_null(can_data.pbufs);
		zassert_equal(csp_buffer_remaining(), 0);
	}
	zassert_equal(can_iface.frame, 0U);
	zassert_equal(can_iface.rx_error, 0U);

	/* Capacity returns: the same frame is now accepted and parked in the pbuf list. */
	release(3U);
	zassert_equal(receive(&frame, isr), CSP_ERR_NONE);
	zassert_not_null(can_data.pbufs);
	zassert_is_null(can_data.pbufs->next);
	zassert_equal(can_iface.drop, 3U);
}

ZTEST(comms_csp_can_rx, test_addressed_to_us_task)
{
	check_exhausted_begin(LOCAL_ADDRESS, false);
}

ZTEST(comms_csp_can_rx, test_addressed_to_us_isr)
{
	check_exhausted_begin(LOCAL_ADDRESS, true);
}

ZTEST(comms_csp_can_rx, test_forwarded_task)
{
	check_exhausted_begin(FORWARDED_ADDRESS, false);
}

ZTEST(comms_csp_can_rx, test_forwarded_isr)
{
	check_exhausted_begin(FORWARDED_ADDRESS, true);
}

ZTEST(comms_csp_can_rx, test_drop_keeps_existing_reassembly)
{
	struct begin_frame first = make_begin(LOCAL_ADDRESS, REMOTE_ADDRESS, 1);
	struct begin_frame second = make_begin(LOCAL_ADDRESS, REMOTE_ADDRESS, 2);
	csp_packet_t *pending;

	zassert_equal(receive(&first, false), CSP_ERR_NONE);
	pending = can_data.pbufs;
	zassert_not_null(pending);

	drain_pool();
	zassert_equal(receive(&second, false), CSP_ERR_NOBUFS);
	zassert_equal(can_iface.drop, 1U);
	zassert_equal(can_data.pbufs, pending);
	zassert_is_null(pending->next);
}

static struct begin_frame flood_begin(unsigned int key)
{
	if (csp_conf.version == 1) {
		return make_begin(LOCAL_ADDRESS, REMOTE_ADDRESS, (uint8_t)key);
	}

	return make_begin(LOCAL_ADDRESS, (uint8_t)(key / (CFP2_SC_MASK + 1U)),
			  (uint8_t)(key & CFP2_SC_MASK));
}

static void check_partial_capacity(size_t expected)
{
	size_t count = 0U;
	csp_packet_t *probe;

	for (csp_packet_t *packet = can_data.pbufs; packet != NULL; packet = packet->next) {
		zassert_true(++count <= PARTIAL_LIMIT);
	}
	zassert_equal(count, expected);
	zassert_equal(csp_buffer_remaining(), POOL_SIZE - expected);

	/* The local liveness ping needs this ordinary allocation, beyond the reserve. */
	probe = csp_buffer_get(0);
	zassert_not_null(probe);
	csp_buffer_free(probe);
	zassert_equal(csp_buffer_remaining(), POOL_SIZE - expected);
}

static void flood(bool isr, unsigned int round)
{
	struct kfsw_csp_counters counters;

	for (unsigned int key = 0U; key < FLOOD_KEYS; key++) {
		struct begin_frame frame = flood_begin(key);
		int expected = key < PARTIAL_LIMIT ? CSP_ERR_NONE : CSP_ERR_NOBUFS;

		zassert_equal(receive(&frame, isr), expected);
		check_partial_capacity(round == 1U ? MIN(key + 1U, PARTIAL_LIMIT) : PARTIAL_LIMIT);
	}

	zassert_equal(can_iface.drop, round * (FLOOD_KEYS - PARTIAL_LIMIT));
	zassert_equal(can_iface.frame, 0U);
	zassert_equal(can_iface.rx_error, 0U);
	kfsw_csp_get_counters(&counters);
	zassert_equal(counters.last_can_error, CSP_DBG_CAN_ERR_RX_OUT);
}

static void check_refresh(bool isr)
{
	flood(isr, 1U);
	csp_packet_t *pending = can_data.pbufs;

	for (unsigned int round = 2U; round <= 3U; round++) {
		k_sleep(K_MSEC(REFRESH_INTERVAL_MS));
		flood(isr, round);
		zassert_equal(can_data.pbufs, pending);
	}
}

static void check_expiry(bool isr)
{
	struct kfsw_csp_counters counters;
	struct begin_frame frame = flood_begin(FLOOD_KEYS - 1U);

	flood(isr, 1U);
	k_sleep(K_MSEC(EXPIRY_WAIT_MS));
	zassert_equal(receive(&frame, isr), CSP_ERR_NONE);
	check_partial_capacity(1U);
	zassert_equal(can_iface.drop, FLOOD_KEYS - PARTIAL_LIMIT);
	zassert_equal(can_iface.frame, 0U);
	zassert_equal(can_iface.rx_error, 0U);
	kfsw_csp_get_counters(&counters);
	zassert_equal(counters.last_can_error, CSP_DBG_CAN_ERR_INCOMPLETE);
}

ZTEST(comms_csp_can_rx, test_partial_flood_task)
{
	flood(false, 1U);
}

ZTEST(comms_csp_can_rx, test_partial_flood_isr)
{
	flood(true, 1U);
}

ZTEST(comms_csp_can_rx, test_partial_refresh_task)
{
	check_refresh(false);
}

ZTEST(comms_csp_can_rx, test_partial_refresh_isr)
{
	check_refresh(true);
}

ZTEST(comms_csp_can_rx, test_partial_expiry_task)
{
	check_expiry(false);
}

ZTEST(comms_csp_can_rx, test_partial_expiry_isr)
{
	check_expiry(true);
}

ZTEST_SUITE(comms_csp_can_rx, NULL, suite_setup, before, after, NULL);
