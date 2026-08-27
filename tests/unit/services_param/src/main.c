#include <errno.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/ztest.h>

#include <param/param.h>
#include <param/param_list.h>
#include <param/param_queue.h>

#include <kfsw/services/parameter.h>

#define TEST_PROTOCOL_VERSION 2

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

ZTEST(services_param, test_kfsw_parameter_lifecycle_and_wire_queue)
{
	struct kfsw_param_value value = {0};
	struct table_summary table = {0};
	const param_t *wire_param;
	param_queue_t encode_queue;
	param_queue_t decode_queue;
	uint8_t wire_buffer[32];
	uint32_t serialized_value = 0x12345678U;

	zassert_false(kfsw_param_is_initialized());
	zassert_equal(kfsw_param_get("test_u32", &value), -EACCES);
	zassert_equal(kfsw_param_set("test_u32", &value), -EACCES);
	zassert_equal(kfsw_param_visit(summarize_parameter, &table), -EACCES);
	zassert_equal(kfsw_param_server_start(), -EACCES);
	zassert_equal(kfsw_param_remote_get(2U, "test_u32", &value), -EACCES);

	zassert_ok(kfsw_param_init());
	zassert_ok(kfsw_param_init());
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

	zassert_ok(kfsw_param_get("node_id", &value));
	zassert_equal(value.type, KFSW_PARAM_U16);
	zassert_equal(kfsw_param_set("node_id", &value), -EACCES);

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

	wire_param = param_list_find_name(0, "test_u32");
	zassert_not_null(wire_param);
	param_queue_init(&encode_queue, wire_buffer, sizeof(wire_buffer), 0, PARAM_QUEUE_TYPE_SET,
			 TEST_PROTOCOL_VERSION);
	zassert_ok(param_queue_add(&encode_queue, wire_param, -1, &serialized_value));
	zassert_true(encode_queue.used > 0U);
	param_queue_init(&decode_queue, wire_buffer, sizeof(wire_buffer), encode_queue.used,
			 PARAM_QUEUE_TYPE_SET, TEST_PROTOCOL_VERSION);
	zassert_ok(param_queue_apply(&decode_queue, 0, 0));
	zassert_ok(kfsw_param_get("test_u32", &value));
	zassert_equal(value.scalar.u32, serialized_value);

	zassert_equal(kfsw_param_server_start(), -EACCES);
}

ZTEST_SUITE(services_param, NULL, NULL, NULL, NULL, NULL);
