#include <errno.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/ztest.h>
#include <zephyr/sys/util.h>

#include <kfsw/services/log.h>
#include <kfsw/services/parameter.h>
#include <kfsw/testing/parameter_definitions.h>

struct table_summary {
	size_t count;
	bool saw_read_only;
	bool saw_u8;
	bool saw_u32;
	bool saw_signed;
	bool saw_float;
};

static bool summarize_parameter(const struct kfsw_param_info *info, void *context)
{
	struct table_summary *summary = context;

	zassert_not_null(info);
	zassert_not_null(info->name);
	summary->count++;
	summary->saw_read_only |= info->read_only;
	summary->saw_u8 |= info->type == KFSW_PARAM_U8;
	summary->saw_u32 |= info->type == KFSW_PARAM_U32;
	summary->saw_signed |= info->type == KFSW_PARAM_I32;
	summary->saw_float |= info->type == KFSW_PARAM_FLOAT;
	return true;
}

ZTEST(services_param, test_kfsw_parameter_lifecycle)
{
	static uint8_t duplicate_value;
	static const struct kfsw_param_definition duplicate_definitions[] = {
		{
			.id = 1U,
			.type = KFSW_PARAM_U8,
			.name = "duplicate_id",
			.value = &duplicate_value,
		},
	};
	static const struct kfsw_param_definition_set duplicate_set = {
		.definitions = duplicate_definitions,
		.count = ARRAY_SIZE(duplicate_definitions),
	};
	struct kfsw_param_value value = {0};
	struct table_summary table = {0};
	const struct kfsw_param_definition_set *const parameter_sets[] = {
		&kfsw_log_param_definitions,
		&kfsw_test_param_definitions,
	};
	const struct kfsw_param_definition_set *const invalid_sets[] = {
		&kfsw_log_param_definitions,
		&duplicate_set,
	};

	zassert_false(kfsw_param_is_initialized());
	zassert_equal(kfsw_param_get("test_u32", &value), -EACCES);
	zassert_equal(kfsw_param_set("test_u32", &value), -EACCES);
	zassert_equal(kfsw_param_visit(summarize_parameter, &table), -EACCES);
	zassert_equal(kfsw_param_init(NULL, 0U), -EINVAL);
	zassert_equal(kfsw_param_init(invalid_sets, ARRAY_SIZE(invalid_sets)), -EEXIST);
	zassert_false(kfsw_param_is_initialized());
#if CONFIG_KFSW_PARAM_CSP
	zassert_equal(kfsw_param_server_start(), -EACCES);
	zassert_equal(kfsw_param_remote_get(2U, "test_u32", &value), -EACCES);
#endif

	zassert_ok(kfsw_param_init(parameter_sets, ARRAY_SIZE(parameter_sets)));
	zassert_ok(kfsw_param_init(parameter_sets, ARRAY_SIZE(parameter_sets)));
	zassert_true(kfsw_param_is_initialized());
	zassert_ok(kfsw_param_visit(summarize_parameter, &table));
	zassert_equal(table.count, 5U);
	zassert_true(table.saw_read_only);
	zassert_true(table.saw_u8);
	zassert_true(table.saw_u32);
	zassert_true(table.saw_signed);
	zassert_true(table.saw_float);

	zassert_ok(kfsw_param_get("test_u32", &value));
	zassert_equal(value.type, KFSW_PARAM_U32);
	zassert_equal(value.size, sizeof(uint32_t));
	zassert_equal(value.scalar.u32, 42U);

	value.scalar.u32 = 99U;
	zassert_ok(kfsw_param_set("test_u32", &value));
	zassert_ok(kfsw_param_get("test_u32", &value));
	zassert_equal(value.scalar.u32, 99U);

	zassert_equal(kfsw_param_get("node_id", &value), -ENOENT);
	zassert_ok(kfsw_param_get("test_read_only", &value));
	zassert_equal(value.type, KFSW_PARAM_U8);
	zassert_equal(kfsw_param_set("test_read_only", &value), -EACCES);

	zassert_ok(kfsw_param_get("test_u32", &value));
	value.type = KFSW_PARAM_I32;
	zassert_equal(kfsw_param_set("test_u32", &value), -EMSGSIZE);
	value.type = KFSW_PARAM_U32;
	value.size = sizeof(uint16_t);
	zassert_equal(kfsw_param_set("test_u32", &value), -EMSGSIZE);
	zassert_equal(kfsw_param_get("missing", &value), -ENOENT);
	zassert_equal(kfsw_param_set("missing", &value), -ENOENT);
	zassert_equal(kfsw_param_get(NULL, &value), -EINVAL);
	zassert_equal(kfsw_param_set("test_u32", NULL), -EINVAL);
	zassert_ok(kfsw_param_get("log_level", &value));
	value.scalar.u8 = 5U;
	zassert_equal(kfsw_param_set("log_level", &value), -ERANGE);
	value.scalar.u8 = 3U;
	zassert_ok(kfsw_param_set("log_level", &value));
	zassert_equal(kfsw_log_get_level(), 3U);

#if CONFIG_KFSW_PARAM_CSP
	zassert_equal(kfsw_param_server_start(), -EACCES);
#endif
}

ZTEST_SUITE(services_param, NULL, NULL, NULL, NULL, NULL);
