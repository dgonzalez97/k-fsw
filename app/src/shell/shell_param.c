#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

#include <zephyr/shell/shell.h>
#include <zephyr/shell/shell_string_conv.h>
#include <zephyr/sys/util.h>

#include <kfsw/services/parameter.h>

struct param_list_context {
	const struct shell *shell;
};

static bool print_param_info(const struct kfsw_param_info *info, void *context)
{
	const struct param_list_context *list_context = context;

	shell_print(list_context->shell, "%u:%u %-16s %-6s %s", info->node, info->id, info->name,
		    kfsw_param_type_name(info->type), info->read_only ? "ro" : "rw");
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

static void print_param_value(const struct shell *sh, uint16_t node, const char *name,
			      const struct kfsw_param_value *value)
{
	char label[64];

	if (node == 0U) {
		(void)snprintf(label, sizeof(label), "%s", name);
	} else {
		(void)snprintf(label, sizeof(label), "%" PRIu16 ":%s", node, name);
	}

	switch (value->type) {
	case KFSW_PARAM_U8:
		shell_print(sh, "%s = %" PRIu8, label, value->scalar.u8);
		break;
	case KFSW_PARAM_U16:
		shell_print(sh, "%s = %" PRIu16, label, value->scalar.u16);
		break;
	case KFSW_PARAM_U32:
		shell_print(sh, "%s = %" PRIu32, label, value->scalar.u32);
		break;
	case KFSW_PARAM_U64:
		shell_print(sh, "%s = %" PRIu64, label, value->scalar.u64);
		break;
	case KFSW_PARAM_I8:
		shell_print(sh, "%s = %" PRId8, label, value->scalar.i8);
		break;
	case KFSW_PARAM_I16:
		shell_print(sh, "%s = %" PRId16, label, value->scalar.i16);
		break;
	case KFSW_PARAM_I32:
		shell_print(sh, "%s = %" PRId32, label, value->scalar.i32);
		break;
	case KFSW_PARAM_I64:
		shell_print(sh, "%s = %" PRId64, label, value->scalar.i64);
		break;
	case KFSW_PARAM_X8:
		shell_print(sh, "%s = 0x%02" PRIx8, label, value->scalar.u8);
		break;
	case KFSW_PARAM_X16:
		shell_print(sh, "%s = 0x%04" PRIx16, label, value->scalar.u16);
		break;
	case KFSW_PARAM_X32:
		shell_print(sh, "%s = 0x%08" PRIx32, label, value->scalar.u32);
		break;
	case KFSW_PARAM_X64:
		shell_print(sh, "%s = 0x%016" PRIx64, label, value->scalar.u64);
		break;
	case KFSW_PARAM_FLOAT:
		shell_print(sh, "%s = %.6g", label, (double)value->scalar.f32);
		break;
	case KFSW_PARAM_DOUBLE:
		shell_print(sh, "%s = %.12g", label, value->scalar.f64);
		break;
	case KFSW_PARAM_STRING:
	case KFSW_PARAM_DATA:
	case KFSW_PARAM_INVALID:
	default:
		shell_print(sh, "%s = <unsupported>", label);
		break;
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
	case KFSW_PARAM_STRING:
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
	struct param_list_context context = {.shell = sh};
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
		result = kfsw_param_remote_visit(node, print_param_info, &context);
	}
#else
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	result = kfsw_param_visit(print_param_info, &context);
#endif

	if (result != 0) {
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
	return 0;
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
	SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(param, &param_commands, "K-FSW parameter commands.", NULL);
