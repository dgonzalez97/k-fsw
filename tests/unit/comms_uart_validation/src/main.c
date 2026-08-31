#include <zephyr/ztest.h>

#include <csp/csp.h>

#include <kfsw/comms/csp.h>

ZTEST(comms_uart_validation, test_duplicate_interface_name_is_rejected)
{
	zassert_equal(kfsw_csp_init(), CSP_ERR_INVAL);
}

ZTEST_SUITE(comms_uart_validation, NULL, NULL, NULL, NULL, NULL);
