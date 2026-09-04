#include <errno.h>
#include <inttypes.h>
#include <string.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

#include <zephyr/shell/shell.h>
#include <zephyr/shell/shell_string_conv.h>
#include <zephyr/sys/util.h>

#include <kfsw/services/parameter.h>

/* Column widths for the listing. The name column is the widest a name may be,
 * so no value ever pushes the columns out of line; registration refuses a
 * longer name rather than truncating one here.
 */
/* Wide enough for the longest table name in any composition. */
#define KFSW_PARAM_TABLE_COLUMN 10
#define KFSW_PARAM_NAME_COLUMN ((int)KFSW_PARAM_NAME_MAX)
#define KFSW_PARAM_TYPE_COLUMN 6
#define KFSW_PARAM_MODE_COLUMN 4
/* Wide enough for a quoted string parameter at full capacity. */
#define KFSW_PARAM_VALUE_TEXT_SIZE (KFSW_PARAM_STRING_MAX + 3)

struct param_list_context {
	const struct shell *shell;
	/* The header is printed on the first row rather than before the walk,
	 * so a listing that turns out to be empty prints nothing at all
	 * instead of column titles over nothing.
	 */
	bool header_printed;
	/* False for a remote listing. Values are looked up by name, and a name
	 * can exist on both nodes, so reading one during a remote listing would
	 * print this node's value in the other node's table.
	 */
	bool local;
};

static void format_param_value(char *text, size_t size, const struct kfsw_param_value *value);

static void print_list_header(const struct shell *sh)
{
	shell_print(sh, "%-*s  %-4s  %-*s  %-*s  %-*s  %s", KFSW_PARAM_TABLE_COLUMN, "table",
		    "addr", KFSW_PARAM_NAME_COLUMN, "name", KFSW_PARAM_TYPE_COLUMN, "type",
		    KFSW_PARAM_MODE_COLUMN, "mode", "value");
	shell_print(sh, "%.*s  %.*s  %.*s  %.*s  %.*s  %s", KFSW_PARAM_TABLE_COLUMN,
		    "--------------------------------", 4, "--------------------------------",
		    KFSW_PARAM_NAME_COLUMN, "--------------------------------",
		    KFSW_PARAM_TYPE_COLUMN, "--------------------------------",
		    KFSW_PARAM_MODE_COLUMN, "--------------------------------", "-----");
}

static bool print_param_info(const struct kfsw_param_info *info, void *context)
{
	struct param_list_context *list_context = context;
	struct kfsw_param_value value;
	char value_text[KFSW_PARAM_VALUE_TEXT_SIZE];
	char table_text[KFSW_PARAM_TABLE_COLUMN + 1];

	if (!list_context->header_printed) {
		print_list_header(list_context->shell);
		list_context->header_printed = true;
	}

	/* A remote listing carries the table in the identifier but not its
	 * name, so the name is printed where it is known and the number where
	 * it is not.
	 */
	if (info->table_name != NULL) {
		(void)snprintf(table_text, sizeof(table_text), "%s", info->table_name);
	} else {
		(void)snprintf(table_text, sizeof(table_text), "%" PRIu8, info->table);
	}

	if (list_context->local && (kfsw_param_get(info->name, &value) == 0)) {
		format_param_value(value_text, sizeof(value_text), &value);
	} else {
		(void)snprintf(value_text, sizeof(value_text), "-");
	}

	shell_print(list_context->shell, "%-*s  0x%02" PRIx8 "  %-*s  %-*s  %-*s  %s",
		    KFSW_PARAM_TABLE_COLUMN, table_text, info->offset, KFSW_PARAM_NAME_COLUMN,
		    info->name, KFSW_PARAM_TYPE_COLUMN, kfsw_param_type_name(info->type),
		    KFSW_PARAM_MODE_COLUMN, kfsw_param_mode_name(info->flags), value_text);
	return true;
}

static bool print_table_info(const struct kfsw_param_table_info *info, void *context)
{
	const struct param_list_context *list_context = context;

	shell_print(list_context->shell, "%3" PRIu8 "  %-7s  %-*s  %6" PRIu16, info->id,
		    kfsw_param_band_name(info->id), KFSW_PARAM_TABLE_COLUMN, info->name,
		    info->count);
	return true;
}

#if CONFIG_KFSW_PARAM_CSP
static int parse_param_node(const struct shell *sh, const char *text, uint16_t *node)
{
	unsigned long parsed;
	int parse_error = 0;

	parsed = shell_strtoul(text, 10, &parse_error);
	if ((parse_error != 0) || (parsed == 0U) || (parsed > 16383U)) {
		shell_error(sh, "CSP node must be in range 1..16383");
		return -EINVAL;
	}

	*node = (uint16_t)parsed;
	return 0;
}
#endif

static int print_param_error(const struct shell *sh, const char *operation, const char *name,
			     int result)
{
	if (result == -ENOENT) {
		shell_error(sh, "%s: parameter '%s' not found", operation, name);
	} else if (result == -EACCES) {
		shell_error(sh, "%s: parameter '%s' is read-only or service is not ready",
			    operation, name);
	} else if (result == -EMSGSIZE) {
		shell_error(sh, "%s: value type or size does not match '%s'", operation, name);
	} else {
		shell_error(sh, "%s: parameter '%s' failed (%d)", operation, name, result);
	}
	return result;
}

static void format_param_value(char *text, size_t size, const struct kfsw_param_value *value)
{
	switch (value->type) {
	case KFSW_PARAM_U8:
		(void)snprintf(text, size, "%" PRIu8, value->scalar.u8);
		break;
	case KFSW_PARAM_U16:
		(void)snprintf(text, size, "%" PRIu16, value->scalar.u16);
		break;
	case KFSW_PARAM_U32:
		(void)snprintf(text, size, "%" PRIu32, value->scalar.u32);
		break;
	case KFSW_PARAM_U64:
		(void)snprintf(text, size, "%" PRIu64, value->scalar.u64);
		break;
	case KFSW_PARAM_I8:
		(void)snprintf(text, size, "%" PRId8, value->scalar.i8);
		break;
	case KFSW_PARAM_I16:
		(void)snprintf(text, size, "%" PRId16, value->scalar.i16);
		break;
	case KFSW_PARAM_I32:
		(void)snprintf(text, size, "%" PRId32, value->scalar.i32);
		break;
	case KFSW_PARAM_I64:
		(void)snprintf(text, size, "%" PRId64, value->scalar.i64);
		break;
	case KFSW_PARAM_X8:
		(void)snprintf(text, size, "0x%02" PRIx8, value->scalar.u8);
		break;
	case KFSW_PARAM_X16:
		(void)snprintf(text, size, "0x%04" PRIx16, value->scalar.u16);
		break;
	case KFSW_PARAM_X32:
		(void)snprintf(text, size, "0x%08" PRIx32, value->scalar.u32);
		break;
	case KFSW_PARAM_X64:
		(void)snprintf(text, size, "0x%016" PRIx64, value->scalar.u64);
		break;
	case KFSW_PARAM_FLOAT:
		(void)snprintf(text, size, "%.6g", (double)value->scalar.f32);
		break;
	case KFSW_PARAM_DOUBLE:
		(void)snprintf(text, size, "%.12g", value->scalar.f64);
		break;
	case KFSW_PARAM_STRING:
		/* Quoted so a trailing space or an empty value is visible rather
		 * than looking like a missing one. */
		(void)snprintf(text, size, "\"%s\"", value->text);
		break;
	case KFSW_PARAM_DATA:
	case KFSW_PARAM_INVALID:
	default:
		(void)snprintf(text, size, "<unsupported>");
		break;
	}
}

static void print_param_value(const struct shell *sh, uint16_t node, const char *name,
			      const struct kfsw_param_value *value)
{
	char text[KFSW_PARAM_VALUE_TEXT_SIZE];

	format_param_value(text, sizeof(text), value);
	if (node == 0U) {
		shell_print(sh, "%s = %s", name, text);
	} else {
		shell_print(sh, "%" PRIu16 ":%s = %s", node, name, text);
	}
}

static int parse_param_value(const char *text, struct kfsw_param_value *value)
{
	char *end = NULL;
	unsigned long long unsigned_value = 0U;
	long long signed_value = 0;

	errno = 0;
	switch (value->type) {
	case KFSW_PARAM_U8:
	case KFSW_PARAM_U16:
	case KFSW_PARAM_U32:
	case KFSW_PARAM_U64:
	case KFSW_PARAM_X8:
	case KFSW_PARAM_X16:
	case KFSW_PARAM_X32:
	case KFSW_PARAM_X64:
		if (text[0] == '-') {
			return -ERANGE;
		}
		unsigned_value = strtoull(text, &end, 0);
		if ((errno != 0) || (end == text) || (*end != '\0')) {
			return -EINVAL;
		}
		break;
	case KFSW_PARAM_I8:
	case KFSW_PARAM_I16:
	case KFSW_PARAM_I32:
	case KFSW_PARAM_I64:
		signed_value = strtoll(text, &end, 0);
		if ((errno != 0) || (end == text) || (*end != '\0')) {
			return -EINVAL;
		}
		break;
	case KFSW_PARAM_FLOAT:
		value->scalar.f32 = strtof(text, &end);
		return ((errno == 0) && (end != text) && (*end == '\0')) ? 0 : -EINVAL;
	case KFSW_PARAM_DOUBLE:
		value->scalar.f64 = strtod(text, &end);
		return ((errno == 0) && (end != text) && (*end == '\0')) ? 0 : -EINVAL;
	case KFSW_PARAM_STRING: {
		size_t length = 0U;

		while ((length + 1U < sizeof(value->text)) && (text[length] != '\0')) {
			value->text[length] = text[length];
			length++;
		}
		if (text[length] != '\0') {
			return -EMSGSIZE;
		}
		value->text[length] = '\0';
		value->size = length + 1U;
		return 0;
	}
	case KFSW_PARAM_DATA:
	case KFSW_PARAM_INVALID:
	default:
		return -ENOTSUP;
	}

	switch (value->type) {
	case KFSW_PARAM_U8:
	case KFSW_PARAM_X8:
		if (unsigned_value > UINT8_MAX) {
			return -ERANGE;
		}
		value->scalar.u8 = (uint8_t)unsigned_value;
		break;
	case KFSW_PARAM_U16:
	case KFSW_PARAM_X16:
		if (unsigned_value > UINT16_MAX) {
			return -ERANGE;
		}
		value->scalar.u16 = (uint16_t)unsigned_value;
		break;
	case KFSW_PARAM_U32:
	case KFSW_PARAM_X32:
		if (unsigned_value > UINT32_MAX) {
			return -ERANGE;
		}
		value->scalar.u32 = (uint32_t)unsigned_value;
		break;
	case KFSW_PARAM_U64:
	case KFSW_PARAM_X64:
		value->scalar.u64 = (uint64_t)unsigned_value;
		break;
	case KFSW_PARAM_I8:
		if ((signed_value < INT8_MIN) || (signed_value > INT8_MAX)) {
			return -ERANGE;
		}
		value->scalar.i8 = (int8_t)signed_value;
		break;
	case KFSW_PARAM_I16:
		if ((signed_value < INT16_MIN) || (signed_value > INT16_MAX)) {
			return -ERANGE;
		}
		value->scalar.i16 = (int16_t)signed_value;
		break;
	case KFSW_PARAM_I32:
		if ((signed_value < INT32_MIN) || (signed_value > INT32_MAX)) {
			return -ERANGE;
		}
		value->scalar.i32 = (int32_t)signed_value;
		break;
	case KFSW_PARAM_I64:
		value->scalar.i64 = (int64_t)signed_value;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int cmd_param_list(const struct shell *sh, size_t argc, char **argv)
{
	struct param_list_context context = {
		.shell = sh,
		.header_printed = false,
		.local = true,
	};
	int result;

#if CONFIG_KFSW_PARAM_CSP
	if (argc == 1U) {
		result = kfsw_param_visit(print_param_info, &context);
	} else {
		uint16_t node;

		result = parse_param_node(sh, argv[1], &node);
		if (result != 0) {
			return result;
		}
		context.local = false;
		result = kfsw_param_remote_visit(node, print_param_info, &context);
	}
#else
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	result = kfsw_param_visit(print_param_info, &context);
#endif

	if (result == -ENOSPC) {
		/* The descriptor cache filled part way through the download, so
		 * the parameters after that point never arrived. Naming the
		 * option is the difference between a number and a fix.
		 */
		shell_error(sh,
			    "parameter list failed: the remote cache holds %d descriptors; "
			    "raise CONFIG_KFSW_PARAM_REMOTE_POOL_SIZE",
			    CONFIG_KFSW_PARAM_REMOTE_POOL_SIZE);
	} else if (result != 0) {
		shell_error(sh, "parameter list failed (%d)", result);
	}
	return result;
}

static int cmd_param_get(const struct shell *sh, size_t argc, char **argv)
{
	struct kfsw_param_value value;
	const char *name;
	uint16_t node = 0U;
	int result;

#if CONFIG_KFSW_PARAM_CSP
	if (argc == 2U) {
		name = argv[1];
		result = kfsw_param_get(name, &value);
	} else {
		result = parse_param_node(sh, argv[1], &node);
		if (result != 0) {
			return result;
		}
		name = argv[2];
		result = kfsw_param_remote_get(node, name, &value);
	}
#else
	ARG_UNUSED(argc);
	name = argv[1];
	result = kfsw_param_get(name, &value);
#endif

	if (result != 0) {
		return print_param_error(sh, "get", name, result);
	}
	print_param_value(sh, node, name, &value);
	return 0;
}

static int cmd_param_set(const struct shell *sh, size_t argc, char **argv)
{
	struct kfsw_param_value value;
	const char *name;
	const char *text;
	uint16_t node = 0U;
	int result;

#if CONFIG_KFSW_PARAM_CSP
	if (argc == 3U) {
		name = argv[1];
		text = argv[2];
		result = kfsw_param_get(name, &value);
	} else {
		result = parse_param_node(sh, argv[1], &node);
		if (result != 0) {
			return result;
		}
		name = argv[2];
		text = argv[3];
		result = kfsw_param_remote_get(node, name, &value);
	}
#else
	ARG_UNUSED(argc);
	name = argv[1];
	text = argv[2];
	result = kfsw_param_get(name, &value);
#endif
	if (result != 0) {
		return print_param_error(sh, "set", name, result);
	}

	result = parse_param_value(text, &value);
	if (result != 0) {
		shell_error(sh, "set: invalid %s value '%s'", kfsw_param_type_name(value.type),
			    text);
		return result;
	}

	if (node == 0U) {
		result = kfsw_param_set(name, &value);
	}
#if CONFIG_KFSW_PARAM_CSP
	else {
		result = kfsw_param_remote_set(node, name, &value);
	}
#endif
	if (result != 0) {
		return print_param_error(sh, "set", name, result);
	}
	print_param_value(sh, node, name, &value);

	/* A stored parameter that lets the operator believe it is live is the
	 * failure this whole scheme exists to prevent, so the write says which
	 * one it was rather than a bare acknowledgement.
	 */
	if (node == 0U) {
		struct kfsw_param_info info;

		if ((kfsw_param_get_info(name, &info) == 0) &&
		    (strcmp(kfsw_param_mode_name(info.flags), "b") == 0)) {
			shell_print(sh, "stored; takes effect after reboot");
		}
	}
	return 0;
}

static int cmd_param_tables(const struct shell *sh, size_t argc, char **argv)
{
	struct param_list_context context = {
		.shell = sh,
		.header_printed = false,
		.local = true,
	};
	int result;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(sh, "%3s  %-7s  %-*s  %6s", " id", "band", KFSW_PARAM_TABLE_COLUMN, "name",
		    "params");
	shell_print(sh, "%.3s  %.7s  %.*s  %.6s", "---------", "---------", KFSW_PARAM_TABLE_COLUMN,
		    "--------------------------------", "---------");

	result = kfsw_param_visit_tables(print_table_info, &context);
	if (result != 0) {
		shell_error(sh, "parameter tables failed (%d)", result);
	}
	return result;
}

#if CONFIG_KFSW_PARAM_PERSISTENCE
static int cmd_param_save(const struct shell *sh, size_t argc, char **argv)
{
	int result;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	result = kfsw_param_persist_save();
	if (result != 0) {
		shell_error(sh, "Parameter snapshot save: FAIL (%d)", result);
		return result;
	}
	shell_print(sh, "Parameter snapshot save: PASS");
	return 0;
}

static int cmd_param_load(const struct shell *sh, size_t argc, char **argv)
{
	int result;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	result = kfsw_param_persist_load();
	if (result == -ENOENT) {
		shell_error(sh, "Parameter snapshot load: no saved snapshot");
		return result;
	}
	if (result != 0) {
		shell_error(sh, "Parameter snapshot load: FAIL (%d)", result);
		return result;
	}
	shell_print(sh, "Parameter snapshot load: PASS");
	return 0;
}

static int cmd_param_defaults(const struct shell *sh, size_t argc, char **argv)
{
	int result;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	result = kfsw_param_restore_defaults();
	if (result != 0) {
		shell_error(sh, "Parameter defaults: FAIL (%d)", result);
		return result;
	}
	shell_print(sh, "Parameter defaults: PASS (saved snapshot unchanged)");
	return 0;
}

static int cmd_param_clear(const struct shell *sh, size_t argc, char **argv)
{
	int result;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	result = kfsw_param_persist_clear();
	if (result != 0) {
		shell_error(sh, "Parameter snapshot clear: FAIL (%d)", result);
		return result;
	}
	shell_print(sh, "Parameter snapshot clear: PASS (RAM unchanged)");
	return 0;
}
#endif

SHELL_STATIC_SUBCMD_SET_CREATE(
	param_commands,
#if CONFIG_KFSW_PARAM_PERSISTENCE
	SHELL_CMD_ARG(clear, NULL, "Delete the saved snapshot; RAM is unchanged.", cmd_param_clear,
		      1, 0),
	SHELL_CMD_ARG(defaults, NULL, "Restore compiled defaults in RAM only.", cmd_param_defaults,
		      1, 0),
#endif
	SHELL_CMD_ARG(get, NULL,
#if CONFIG_KFSW_PARAM_CSP
		      "Get local or remote value: get [node] <name>.", cmd_param_get, 2, 1),
#else
		      "Get a local value: get <name>.", cmd_param_get, 2, 0),
#endif
#if CONFIG_KFSW_PARAM_PERSISTENCE
	SHELL_CMD_ARG(load, NULL, "Reload the saved snapshot into RAM.", cmd_param_load, 1, 0),
#endif
	SHELL_CMD_ARG(list, NULL,
#if CONFIG_KFSW_PARAM_CSP
		      "List local or remote parameters: list [node].", cmd_param_list, 1, 1),
#else
		      "List local parameters.", cmd_param_list, 1, 0),
#endif
#if CONFIG_KFSW_PARAM_PERSISTENCE
	SHELL_CMD_ARG(save, NULL, "Atomically save persistent RAM values.", cmd_param_save, 1, 0),
#endif
	SHELL_CMD_ARG(set, NULL,
#if CONFIG_KFSW_PARAM_CSP
		      "Set local or remote value: set [node] <name> <value>.", cmd_param_set, 3, 1),
#else
		      "Set a local value: set <name> <value>.", cmd_param_set, 3, 0),
#endif
	SHELL_CMD_ARG(tables, NULL, "List registered parameter tables.", cmd_param_tables, 1, 0),
	SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(param, &param_commands, "K-FSW parameter commands.", NULL);
