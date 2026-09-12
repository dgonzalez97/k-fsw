#include <errno.h>
#include <zephyr/ztest.h>
#include <csp/csp.h>
#include <kfsw/comms/csp.h>
#include <kfsw/services/fwu_lite.h>

ZTEST(fwu_lite_start, test_occupied_port_fails_synchronously_and_retry_works)
{
	static csp_socket_t occupied;

	zassert_equal(kfsw_fwu_lite_server_start(), -EACCES);
	zassert_ok(kfsw_csp_init());
	zassert_ok(kfsw_csp_start());
	zassert_ok(csp_listen(&occupied, 1));
	zassert_ok(csp_bind(&occupied, CONFIG_KFSW_FWU_LITE_CSP_PORT));
	zassert_equal(kfsw_fwu_lite_server_start(), -EADDRINUSE);
	zassert_ok(csp_socket_close(&occupied));
	zassert_ok(kfsw_fwu_lite_server_start());
	zassert_equal(kfsw_fwu_lite_server_start(), -EALREADY);
}

ZTEST_SUITE(fwu_lite_start, NULL, NULL, NULL, NULL, NULL);
