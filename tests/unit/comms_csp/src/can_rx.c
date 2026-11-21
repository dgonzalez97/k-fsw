#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/ztest.h>

#include <csp/csp.h>
#include <csp/csp_buffer.h>
#include <csp/csp_id.h>
#include <csp/interfaces/csp_if_can.h>

/* Private to libcsp; the test needs it to drain the reserve that csp_buffer_get() keeps back. */
csp_packet_t *csp_buffer_get_always(void);

#define LOCAL_ADDRESS 7U
#define REMOTE_ADDRESS 3U
#define FORWARDED_ADDRESS 9U
#define BEGIN_DATA_BYTES 8U
#define POOL_SIZE CONFIG_CSP_BUFFER_COUNT

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
static struct begin_frame make_begin(uint16_t dst, uint8_t counter)
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
			   ((uint32_t)REMOTE_ADDRESS << CFP2_SENDER_OFFSET) |
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

	if (can_data.pbufs != NULL) {
		csp_buffer_free(can_data.pbufs);
		can_data.pbufs = NULL;
	}
	release(held_count);
	zassert_equal(csp_can_remove_interface(&can_iface), CSP_ERR_NONE);
	zassert_equal(csp_buffer_remaining(), POOL_SIZE);
}

static void check_exhausted_begin(uint16_t dst, bool isr)
{
	struct begin_frame frame = make_begin(dst, 1);

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
	struct begin_frame first = make_begin(LOCAL_ADDRESS, 1);
	struct begin_frame second = make_begin(LOCAL_ADDRESS, 2);
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

ZTEST_SUITE(comms_csp_can_rx, NULL, suite_setup, before, after, NULL);
