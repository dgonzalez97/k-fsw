#include <errno.h>
#include <stdint.h>

#include <zephyr/sys/util.h>
#include <zephyr/ztest.h>

#include <kfsw/services/parameter.h>

static float float_value;
static double double_value;

static const struct kfsw_param_definition float_definitions[] = {
	{
		.offset = 0x00U,
		.type = KFSW_PARAM_FLOAT,
		.name = "a_float",
		.value = &float_value,
	},
	{
		.offset = 0x08U,
		.type = KFSW_PARAM_DOUBLE,
		.name = "a_double",
		.value = &double_value,
	},
};

static const struct kfsw_param_definition_set float_set = {
	.table = KFSW_PARAM_TABLE_CORE_FIRST,
	.name = "floats",
	.definitions = float_definitions,
	.count = ARRAY_SIZE(float_definitions),
};

ZTEST(services_param_float, test_the_option_decides_whether_a_float_registers)
{
	const struct kfsw_param_definition_set *const sets[] = {&float_set};
	struct kfsw_param_value value = {0};

#if CONFIG_KFSW_PARAM_FLOAT
	zassert_ok(kfsw_param_init(sets, ARRAY_SIZE(sets)));
	value.type = KFSW_PARAM_FLOAT;
	value.size = sizeof(float);
	value.scalar.f32 = 1.5f;
	zassert_ok(kfsw_param_set("a_float", &value));
	zassert_ok(kfsw_param_get("a_float", &value));
	zassert_equal(value.scalar.f32, 1.5f);
	zassert_equal(float_value, 1.5f);
#else
	/* Refused rather than registered and then printed as something else. */
	zassert_equal(kfsw_param_init(sets, ARRAY_SIZE(sets)), -ENOTSUP);
	zassert_false(kfsw_param_is_initialized());
	zassert_equal(kfsw_param_get("a_float", &value), -EACCES);
#endif
}

ZTEST_SUITE(services_param_float, NULL, NULL, NULL, NULL, NULL);
