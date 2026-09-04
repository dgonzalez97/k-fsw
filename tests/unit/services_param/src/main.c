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
	/* Same table and offset as the log service's own parameter: the wire
	 * identifier is what a remote list is keyed by, so two components
	 * claiming one must be refused rather than silently shadowing.
	 */
	static const struct kfsw_param_definition duplicate_definitions[] = {
		{
			.offset = 0x00U,
			.type = KFSW_PARAM_U8,
			.name = "duplicate_offset",
			.value = &duplicate_value,
		},
	};
	static const struct kfsw_param_definition_set duplicate_set = {
		.table = KFSW_LOG_PARAM_TABLE_ID,
		.name = KFSW_LOG_PARAM_TABLE_NAME,
		.definitions = duplicate_definitions,
		.count = ARRAY_SIZE(duplicate_definitions),
	};
	struct kfsw_param_value value = {0};
	struct kfsw_param_info info = {0};
	struct table_summary table = {0};
	const struct kfsw_param_definition_set *const parameter_sets[] = {
		&kfsw_log_param_definitions,
		&kfsw_test_param_definitions,
	};
	const struct kfsw_param_definition_set *const invalid_sets[] = {
		&kfsw_log_param_definitions,
		&duplicate_set,
	};

	static uint8_t spare_value;
	static uint8_t second_spare_value;
	static const struct kfsw_param_definition one_definition[] = {
		{
			.offset = 0x00U,
			.type = KFSW_PARAM_U8,
			.name = "only",
			.value = &spare_value,
		},
	};
	/* Thirty-three characters, one past the limit. */
	static const struct kfsw_param_definition long_name_definition[] = {
		{
			.offset = 0x00U,
			.type = KFSW_PARAM_U8,
			.name = "a_name_of_exactly_thirty_three_ch",
			.value = &spare_value,
		},
	};
	/* Different names, one address. */
	static const struct kfsw_param_definition colliding_definitions[] = {
		{
			.offset = 0x04U,
			.type = KFSW_PARAM_U8,
			.name = "first",
			.value = &spare_value,
		},
		{
			.offset = 0x04U,
			.type = KFSW_PARAM_U8,
			.name = "second",
			.value = &second_spare_value,
		},
	};
	static const struct kfsw_param_definition_set reserved_table = {
		.table = KFSW_PARAM_TABLE_INVALID,
		.name = "reserved",
		.definitions = one_definition,
		.count = ARRAY_SIZE(one_definition),
	};
	static const struct kfsw_param_definition_set unallocated_table = {
		.table = KFSW_PARAM_TABLE_MODULE_LAST + 1U,
		.name = "unallocated",
		.definitions = one_definition,
		.count = ARRAY_SIZE(one_definition),
	};
	static const struct kfsw_param_definition_set unnamed_table = {
		.table = KFSW_PARAM_TABLE_CORE_FIRST,
		.name = NULL,
		.definitions = one_definition,
		.count = ARRAY_SIZE(one_definition),
	};
	static const struct kfsw_param_definition_set long_name_table = {
		.table = KFSW_PARAM_TABLE_CORE_FIRST,
		.name = "long",
		.definitions = long_name_definition,
		.count = ARRAY_SIZE(long_name_definition),
	};
	static const struct kfsw_param_definition_set colliding_table = {
		.table = KFSW_PARAM_TABLE_CORE_FIRST,
		.name = "collide",
		.definitions = colliding_definitions,
		.count = ARRAY_SIZE(colliding_definitions),
	};
	const struct kfsw_param_definition_set *const reserved_table_sets[] = {&reserved_table};
	const struct kfsw_param_definition_set *const unallocated_table_sets[] = {
		&unallocated_table,
	};
	const struct kfsw_param_definition_set *const unnamed_table_sets[] = {&unnamed_table};
	const struct kfsw_param_definition_set *const long_name_sets[] = {&long_name_table};
	const struct kfsw_param_definition_set *const colliding_sets[] = {&colliding_table};

	zassert_false(kfsw_param_is_initialized());
	zassert_equal(kfsw_param_get("test_u32", &value), -EACCES);
	zassert_equal(kfsw_param_set("test_u32", &value), -EACCES);
	zassert_equal(kfsw_param_visit(summarize_parameter, &table), -EACCES);
	zassert_equal(kfsw_param_init(NULL, 0U), -EINVAL);
	zassert_equal(kfsw_param_init(invalid_sets, ARRAY_SIZE(invalid_sets)), -EEXIST);
	zassert_false(kfsw_param_is_initialized());

	/* Registration has to refuse what could not be addressed afterwards.
	 * These all belong here rather than in cases of their own because the
	 * service initializes once: after the successful call below, every
	 * later attempt is a no-op and would prove nothing.
	 */
	zassert_equal(kfsw_param_init(reserved_table_sets, 1U), -EINVAL,
		      "table zero is reserved so an uninitialised field cannot address a table");
	zassert_equal(kfsw_param_init(unallocated_table_sets, 1U), -EINVAL,
		      "a table outside every band has no owner");
	zassert_equal(kfsw_param_init(unnamed_table_sets, 1U), -EINVAL);
	zassert_equal(kfsw_param_init(long_name_sets, 1U), -ENAMETOOLONG,
		      "a truncated name would be printed but could not be typed");
	zassert_equal(kfsw_param_init(colliding_sets, 1U), -EEXIST,
		      "two parameters at one address would shadow each other on the link");
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

	/* One offset in two tables is the point of the scheme: each table starts
	 * its own address space at zero, and the wire identifier keeps them
	 * apart. */
	zassert_equal(kfsw_param_table_count(), 2U);
	zassert_ok(kfsw_param_get_info("log_level", &info));
	zassert_equal(info.table, KFSW_LOG_PARAM_TABLE_ID);
	zassert_equal(info.offset, 0x00U);
	zassert_equal(info.id, (uint16_t)(KFSW_LOG_PARAM_TABLE_ID << 8));
	zassert_str_equal(info.table_name, KFSW_LOG_PARAM_TABLE_NAME);
	zassert_str_equal(kfsw_param_band_name(info.table), "service");

	zassert_ok(kfsw_param_get_info("test_u32", &info));
	zassert_equal(info.offset, 0x08U);
	zassert_str_equal(kfsw_param_band_name(info.table), "core");

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
