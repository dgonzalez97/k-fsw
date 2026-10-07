#include <errno.h>
#include <endian.h>
#include <string.h>
#include <zephyr/ztest.h>
#include <csp/csp.h>
#include <csp/csp_cmp.h>
#include <kfsw/comms/csp.h>

static bool mock;
static bool timeout;
static unsigned int calls;
static struct csp_cmp_if_stats_msg reply;

int __real_csp_transaction_w_opts(uint8_t priority, uint16_t node, uint8_t port,
				  uint32_t timeout_ms, const void *out, int outlen, void *in,
				  int inlen, uint32_t opts);

int __wrap_csp_transaction_w_opts(uint8_t priority, uint16_t node, uint8_t port,
				  uint32_t timeout_ms, const void *out, int outlen, void *in,
				  int inlen, uint32_t opts)
{
	if (!mock) {
		return __real_csp_transaction_w_opts(priority, node, port, timeout_ms, out, outlen,
						     in, inlen, opts);
	}
	const struct csp_cmp_if_stats_msg *request = out;
	calls++;
	zassert_equal(node, 2U);
	zassert_equal(port, CSP_CMP);
	zassert_equal(opts, CSP_O_CRC32);
	zassert_equal(outlen, sizeof(reply));
	zassert_equal(inlen, sizeof(reply));
	zassert_equal(request->type, CSP_CMP_REQUEST);
	zassert_equal(request->code, CSP_CMP_IF_STATS);
	zassert_mem_equal(request->interface, "KISS\0", 5U);
	if (timeout) {
		return 0;
	}
	memcpy(in, &reply, sizeof(reply));
	return sizeof(reply);
}

static void *setup(void)
{
	zassert_ok(kfsw_csp_init());
	zassert_ok(kfsw_csp_start());
	return NULL;
}

static void before(void *fixture)
{
	ARG_UNUSED(fixture);
	mock = true;
	timeout = false;
	calls = 0U;
	reply = (struct csp_cmp_if_stats_msg){
		.type = CSP_CMP_REPLY,
		.code = CSP_CMP_IF_STATS,
		.interface = "KISS",
		.tx = htobe32(0x12345678U),
		.rx = htobe32(UINT32_MAX),
		.tx_error = htobe32(3U),
		.rx_error = htobe32(4U),
		.drop = htobe32(5U),
		.autherr = htobe32(6U),
		.frame = htobe32(7U),
		.txbytes = htobe32(800U),
		.rxbytes = htobe32(900U),
		.irq = htobe32(10U),
	};
}

ZTEST(comms_ifstats, test_decodes_all_counters)
{
	struct kfsw_csp_interface_stats stats = {.name = "KISS"};
	zassert_ok(kfsw_csp_interface_stats_read(2U, stats.name, 50U, &stats));
	zassert_equal(calls, 1U);
	zassert_equal(strcmp(stats.name, "KISS"), 0);
	zassert_equal(stats.tx_packets, 0x12345678U);
	zassert_equal(stats.rx_packets, UINT32_MAX);
	zassert_equal(stats.tx_errors, 3U);
	zassert_equal(stats.rx_errors, 4U);
	zassert_equal(stats.dropped_packets, 5U);
	zassert_equal(stats.auth_errors, 6U);
	zassert_equal(stats.frame_errors, 7U);
	zassert_equal(stats.tx_bytes, 800U);
	zassert_equal(stats.rx_bytes, 900U);
	zassert_equal(stats.interrupts, 10U);
}

ZTEST(comms_ifstats, test_rejects_invalid_arguments_without_sending)
{
	struct kfsw_csp_interface_stats stats;
	zassert_equal(kfsw_csp_interface_stats_read(2, NULL, 50, &stats), -EINVAL);
	zassert_equal(kfsw_csp_interface_stats_read(2, "", 50, &stats), -EINVAL);
	zassert_equal(kfsw_csp_interface_stats_read(2, "abcdefghijk", 50, &stats), -EINVAL);
	zassert_equal(
		kfsw_csp_interface_stats_read(KFSW_CSP_BROADCAST_ADDRESS + 1U, "KISS", 50, &stats),
		-EINVAL);
	zassert_equal(kfsw_csp_interface_stats_read(2, "KISS", 0, &stats), -EINVAL);
	zassert_equal(kfsw_csp_interface_stats_read(2, "KISS", 50, NULL), -EINVAL);
	zassert_equal(calls, 0U);
}

static void unchanged(int expected)
{
	struct kfsw_csp_interface_stats stats;
	struct kfsw_csp_interface_stats original;
	memset(&stats, 0xa5, sizeof(stats));
	original = stats;
	zassert_equal(kfsw_csp_interface_stats_read(2, "KISS", 50, &stats), expected);
	zassert_mem_equal(&stats, &original, sizeof(stats));
}

ZTEST(comms_ifstats, test_failure_preserves_output)
{
	timeout = true;
	unchanged(-ETIMEDOUT);
	timeout = false;
	reply.type = CSP_CMP_REQUEST;
	unchanged(-EBADMSG);
	reply.type = CSP_CMP_REPLY;
	reply.code = CSP_CMP_IDENT;
	unchanged(-EBADMSG);
	reply.code = CSP_CMP_IF_STATS;
	memset(reply.interface, 'x', sizeof(reply.interface));
	unchanged(-EBADMSG);
}

ZTEST(comms_ifstats, test_real_loopback_and_unknown_interface)
{
	struct kfsw_csp_interface_stats stats;
	struct kfsw_csp_info before_info, after_info;
	mock = false;
	kfsw_csp_get_info(&before_info);
	zassert_ok(kfsw_csp_interface_stats_read(7, "LOOP", 500, &stats));
	zassert_equal(strcmp(stats.name, "LOOP"), 0);
	zassert_true(stats.rx_packets > 0);
	zassert_equal(kfsw_csp_interface_stats_read(7, "missing", 50, &stats), -ETIMEDOUT);
	kfsw_csp_get_info(&after_info);
	zassert_equal(before_info.free_buffers, after_info.free_buffers);
}

ZTEST_SUITE(comms_ifstats, NULL, setup, before, NULL, NULL);
