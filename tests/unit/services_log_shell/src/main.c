#include <errno.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/shell/shell_dummy.h>
#include <zephyr/ztest.h>
#include <kfsw/services/log.h>
#include <kfsw/services/parameter.h>

static void *setup(void)
{
	const struct kfsw_param_definition_set *const sets[] = {&kfsw_log_param_definitions};
	const struct shell *sh = shell_backend_dummy_get_ptr();

	zassert_ok(kfsw_param_init(sets, ARRAY_SIZE(sets)));
	for (unsigned int i = 0U; i < 100U && !shell_ready(sh); i++) {
		k_sleep(K_MSEC(1));
	}
	zassert_true(shell_ready(sh));
	return NULL;
}

ZTEST(log_shell, test_mirroring_policy)
{
	const struct shell *sh = shell_backend_dummy_get_ptr();
	struct kfsw_param_value value;
	size_t size;
	const char *output;

	shell_backend_dummy_clear_output(sh);
	kfsw_log_warning("csp ping: node 2 did not answer");
	output = shell_backend_dummy_get_output(sh, &size);
#if CONFIG_KFSW_LOG_SHELL
	zassert_not_null(strstr(output, "[WARNING] csp ping: node 2 did not answer"));
	zassert_ok(kfsw_param_get("log_shell", &value));
	zassert_equal(value.scalar.u8, 1U);
	value.scalar.u8 = 2U;
	zassert_equal(kfsw_param_set("log_shell", &value), -ERANGE);
	value.scalar.u8 = 0U;
	zassert_ok(kfsw_param_set("log_shell", &value));
	shell_backend_dummy_clear_output(sh);
	kfsw_log_warning("mirroring disabled");
	output = shell_backend_dummy_get_output(sh, &size);
	zassert_is_null(strstr(output, "mirroring disabled"));
	value.scalar.u8 = 1U;
	zassert_ok(kfsw_param_set("log_shell", &value));
	zassert_ok(kfsw_log_set_level(3U));
	shell_backend_dummy_clear_output(sh);
	kfsw_log_warning("filtered warning");
	kfsw_log_error("visible error");
	kfsw_log_marker("@MIRROR marker");
	output = shell_backend_dummy_get_output(sh, &size);
	zassert_is_null(strstr(output, "filtered warning"));
	zassert_not_null(strstr(output, "[ERROR] visible error"));
	zassert_not_null(strstr(output, "@MIRROR marker"));
#else
	zassert_is_null(strstr(output, "csp ping: node 2 did not answer"));
	zassert_equal(kfsw_param_get("log_shell", &value), -ENOENT);
#endif
}

ZTEST_SUITE(log_shell, NULL, setup, NULL, NULL, NULL);
