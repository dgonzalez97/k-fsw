#include <errno.h>

#include <zephyr/ztest.h>

#include <csp/csp.h>
#include <csp/csp_iflist.h>
#include <csp/drivers/usart.h>

#include <kfsw/comms/csp.h>
#include <kfsw/comms/uart.h>

static unsigned int open_calls;
static unsigned int attach_calls;
static unsigned int check_calls;
static bool inject_failure = true;

int __real_csp_usart_open(const csp_usart_conf_t *conf, csp_usart_callback_t callback,
			  void *user_data, csp_usart_fd_t *fd);

int __wrap_csp_usart_open(const csp_usart_conf_t *conf, csp_usart_callback_t callback,
			  void *user_data, csp_usart_fd_t *fd)
{
	open_calls++;
	if (inject_failure && ((UART_OPEN_FAILURE == 1 && open_calls == 1U) ||
			       (UART_OPEN_FAILURE == 2 && open_calls == 2U))) {
		return CSP_ERR_DRIVER;
	}
	return __real_csp_usart_open(conf, callback, user_data, fd);
}

#if CONFIG_KFSW_CSP_UART_CODEC
int __wrap_kfsw_uart_codec_attach(csp_iface_t *iface)
{
	zassert_not_null(iface);
	attach_calls++;
	return inject_failure && UART_OPEN_FAILURE == 3 && attach_calls == 2U ? -EIO : 0;
}

int __wrap_kfsw_uart_codec_check(void)
{
	check_calls++;
	return inject_failure && UART_OPEN_FAILURE == 4 ? -ENODEV : 0;
}
#endif

static bool check_context(const struct kfsw_uart_info *info, void *context)
{
	unsigned int *count = context;

	if (info->interface_started) {
		zassert_not_null(csp_iflist_get_by_name(info->interface_name));
		(*count)++;
	}
	return true;
}

ZTEST(comms_uart_validation, test_open_result_is_stable)
{
	int expected = CSP_ERR_NONE;
	unsigned int expected_contexts = UART_OPEN_FAILURE == 1 ? 1U : 2U;
	unsigned int contexts = 0U;

	if (UART_OPEN_FAILURE == 1 || UART_OPEN_FAILURE == 2) {
		expected = CSP_ERR_DRIVER;
	} else if (UART_OPEN_FAILURE == 3) {
		expected = -EIO;
	} else if (UART_OPEN_FAILURE == 4) {
		expected = -ENODEV;
	}
	zassert_equal(kfsw_csp_init(), expected);
	zassert_equal(open_calls, expected_contexts);
	csp_iface_t *first = csp_iflist_get_by_name("KISS_1");
	csp_iface_t *second = csp_iflist_get_by_name("KISS_2");
	unsigned int initial_attach_calls = attach_calls;
	unsigned int initial_check_calls = check_calls;

	zassert_not_null(first);
	zassert_equal(second != NULL, expected_contexts == 2U);
	kfsw_uart_visit(check_context, &contexts);
	zassert_equal(contexts, expected_contexts);

	inject_failure = false;
	for (unsigned int retry = 0U; retry < 2U; retry++) {
		zassert_equal(kfsw_csp_init(), expected);
		zassert_equal(open_calls, expected_contexts);
		zassert_equal(attach_calls, initial_attach_calls);
		zassert_equal(check_calls, initial_check_calls);
		zassert_equal_ptr(csp_iflist_get_by_name("KISS_1"), first);
		zassert_equal_ptr(csp_iflist_get_by_name("KISS_2"), second);
	}
}

ZTEST_SUITE(comms_uart_validation, NULL, NULL, NULL, NULL, NULL);
