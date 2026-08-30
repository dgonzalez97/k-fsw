#include <stdint.h>

#include <zephyr/sys/util.h>

#include <kfsw/services/parameter.h>
#include <kfsw/testing/parameter_definitions.h>

static uint32_t test_u32 = 42U;
static int32_t test_i32 = -7;
static float test_float = 1.5F;
static uint8_t test_read_only = 9U;

static const struct kfsw_param_definition test_param_definitions[] = {
	{
		.id = 2U,
		.type = KFSW_PARAM_U32,
		.flags = KFSW_PARAM_FLAG_DEBUG | KFSW_PARAM_FLAG_PERSISTENT,
		.name = "test_u32",
		.description = "Writable unsigned integration value",
		.value = &test_u32,
		.default_value = {.u32 = 42U},
	},
	{
		.id = 3U,
		.type = KFSW_PARAM_I32,
		.flags = KFSW_PARAM_FLAG_DEBUG | KFSW_PARAM_FLAG_PERSISTENT,
		.name = "test_i32",
		.description = "Writable signed integration value",
		.value = &test_i32,
		.default_value = {.i32 = -7},
	},
	{
		.id = 4U,
		.type = KFSW_PARAM_FLOAT,
		.flags = KFSW_PARAM_FLAG_DEBUG | KFSW_PARAM_FLAG_PERSISTENT,
		.name = "test_float",
		.description = "Writable floating-point integration value",
		.value = &test_float,
		.default_value = {.f32 = 1.5F},
	},
	{
		.id = 5U,
		.type = KFSW_PARAM_U8,
		.flags = KFSW_PARAM_FLAG_READ_ONLY | KFSW_PARAM_FLAG_DEBUG,
		.name = "test_read_only",
		.description = "Read-only parameter fixture",
		.value = &test_read_only,
		.default_value = {.u8 = 9U},
	},
};

const struct kfsw_param_definition_set kfsw_test_param_definitions = {
	.definitions = test_param_definitions,
	.count = ARRAY_SIZE(test_param_definitions),
};
