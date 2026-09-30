#include <errno.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/fff.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include <csp/csp.h>
#include <csp/csp_id.h>
#include <csp/csp_iflist.h>
#include <csp/csp_interface.h>

#include <kfsw/comms/csp.h>

DEFINE_FFF_GLOBALS;

/* The driver bounds both the wait for a mailbox and the wait for the frame to
 * be sent, so a frame that never completes costs two of those waits. Anything
 * near that is the fix working; a call that does not return at all is the
 * defect, and shows up as the suite timing out.
 */
#define TX_BOUND_MS 600

/* Somewhere the CAN interface is the only way out. */
#define REMOTE_NODE 33U

static csp_iface_t *can_interface(void)
{
	csp_iface_t *iface = csp_iflist_get_by_name(CONFIG_KFSW_CSP_CAN_INTERFACE_NAME);

	zassert_not_null(iface, "the CAN interface was not added");
	zassert_not_null(iface->nexthop);
	return iface;
}

ZTEST(comms_can_tx, test_a_frame_that_is_never_acknowledged_does_not_hold_the_sender)
{
	csp_iface_t *iface = can_interface();
	csp_packet_t *packet;
	int64_t started_ms;
	int64_t elapsed_ms;
	int result;

	packet = csp_buffer_get(0);
	zassert_not_null(packet, "no CSP buffer to send");
	packet->id.dst = REMOTE_NODE;
	packet->id.src = CONFIG_KFSW_CSP_ADDRESS;
	packet->id.dport = CSP_PING;
	packet->id.sport = 40U;
	packet->id.pri = CSP_PRIO_NORM;
	packet->id.flags = 0U;
	packet->length = 8U;
	(void)memset(packet->data, 0xA5, packet->length);
	csp_id_prepend(packet);

	started_ms = k_uptime_get();
	result = iface->nexthop(iface, REMOTE_NODE, packet, 1);
	elapsed_ms = k_uptime_delta(&started_ms);

	zassert_not_equal(result, CSP_ERR_NONE,
			  "a frame the controller never sent was reported as sent");
	zassert_true(elapsed_ms < TX_BOUND_MS,
		     "the send held its thread for %lld ms; a controller that cannot "
		     "transmit must not block the caller",
		     elapsed_ms);

	/* And the interface says so, rather than the failure only being in a log. */
	zassert_true(iface->tx_error > 0U, "the dropped frame was not counted");
}

static void *can_tx_setup(void)
{
	zassert_ok(kfsw_csp_init(), "the CSP stack with a CAN interface did not come up");
	return NULL;
}

ZTEST_SUITE(comms_can_tx, NULL, can_tx_setup, NULL, NULL, NULL);
