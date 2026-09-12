#include <errno.h>
#include <string.h>
#include <zephyr/ztest.h>
#include <csp/csp.h>
#include <csp/csp_buffer.h>
#include <kfsw/comms/uart_codec.h>
#include "uart_internal.h"

void kfsw_uart_codec_receive(csp_packet_t *packet, csp_iface_t *iface, void *task_woken);
static K_SEM_DEFINE(entered, 0, 1);
static K_SEM_DEFINE(release, 0, 1);
static atomic_t block_next;
static bool refuse_tx;
static unsigned int encoded, sent;

static int encode(csp_packet_t *packet)
{
	ARG_UNUSED(packet);
	encoded++;
	return refuse_tx ? -EACCES : 0;
}

static int decode(csp_packet_t *packet)
{
	ARG_UNUSED(packet);
	zassert_false(k_is_in_isr());
	if (atomic_cas(&block_next, 1, 0)) {
		k_sem_give(&entered);
		zassert_ok(k_sem_take(&release, K_SECONDS(2)));
	}
	return 1;
}

static int raw_tx(csp_iface_t *iface, uint16_t via, csp_packet_t *packet, int from_me)
{
	ARG_UNUSED(iface);
	ARG_UNUSED(via);
	ARG_UNUSED(from_me);
	sent++;
	csp_buffer_free(packet);
	return CSP_ERR_NONE;
}

ZTEST(uart_codec, test_binding_queue_and_packet_ownership)
{
	static const struct kfsw_uart_codec codec = {.encode = encode, .decode = decode};
	static csp_iface_t iface = {.name = "RADIO", .addr = 7, .nexthop = raw_tx};
	static csp_iface_t other = {.name = "OTHER", .addr = 7, .nexthop = raw_tx};
	zassert_equal(kfsw_uart_codec_register(NULL, &codec), -EINVAL);
	zassert_equal(kfsw_uart_codec_register("", &codec), -EINVAL);
	zassert_ok(kfsw_uart_codec_register("RADIO", &codec));
	zassert_equal(kfsw_uart_codec_register("RADIO", &codec), -EALREADY);
	zassert_equal(kfsw_uart_codec_check(), -ENODEV);
	zassert_ok(kfsw_uart_codec_attach(&other));
	zassert_equal(kfsw_uart_codec_check(), -ENODEV);
	csp_init();
	zassert_ok(kfsw_uart_codec_attach(&iface));
	zassert_ok(kfsw_uart_codec_check());
	zassert_equal(kfsw_uart_codec_attach(&iface), -EALREADY);
	int free_before = csp_buffer_remaining();
	atomic_set(&block_next, 1);
	kfsw_uart_codec_receive(csp_buffer_get(8), &iface, NULL);
	zassert_ok(k_sem_take(&entered, K_SECONDS(1)));
	for (size_t i = 0; i < CONFIG_KFSW_CSP_UART_CODEC_QUEUE_DEPTH + 3; i++) {
		kfsw_uart_codec_receive(csp_buffer_get(8), &iface, NULL);
	}
	zassert_equal(iface.drop, 3);
	zassert_equal(csp_buffer_remaining(),
		      free_before - 1 - CONFIG_KFSW_CSP_UART_CODEC_QUEUE_DEPTH);
	k_sem_give(&release);
	for (int i = 0; i < 50 && csp_buffer_remaining() != free_before; i++) {
		k_sleep(K_MSEC(1));
	}
	zassert_equal(csp_buffer_remaining(), free_before);
	csp_packet_t *packet = csp_buffer_get(8);
	refuse_tx = true;
	zassert_equal(iface.nexthop(&iface, 0, packet, 1), CSP_ERR_TX);
	zassert_equal(csp_buffer_remaining(), free_before - 1);
	csp_buffer_free(packet);
	refuse_tx = false;
	zassert_ok(iface.nexthop(&iface, 0, csp_buffer_get(8), 1));
	zassert_equal(encoded, 2);
	zassert_equal(sent, 1);
	uint8_t control[] = {1, 2};
	zassert_ok(kfsw_uart_codec_control(8, control, sizeof(control)));
	zassert_equal(encoded, 2);
	zassert_equal(sent, 2);
	zassert_equal(csp_buffer_remaining(), free_before);
	zassert_equal(kfsw_uart_codec_control(8, control, CSP_BUFFER_SIZE), -EINVAL);
}

ZTEST_SUITE(uart_codec, NULL, NULL, NULL, NULL, NULL);
