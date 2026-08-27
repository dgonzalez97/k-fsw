#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/shell/shell.h>
#include <zephyr/shell/shell_string_conv.h>
#include <zephyr/sys/util.h>
#include <zephyr/version.h>

#if CONFIG_KFSW_STORAGE
#include <zephyr/fs/fs.h>
#endif

#if CONFIG_KFSW_CSP
#include <kfsw/comms/csp.h>
#endif
#if CONFIG_KFSW_CSP_KISS_UART
#include <kfsw/comms/uart.h>
#endif
#if CONFIG_KFSW_STORAGE
#include <kfsw/platform/storage.h>
#endif
#include <kfsw/platform/time.h>
#include <kfsw/services/log.h>
#if CONFIG_KFSW_PARAM
#include <kfsw/services/parameter.h>
#endif

#if CONFIG_KFSW_PARAM
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

static int cmd_kfsw_param_list(const struct shell *sh, size_t argc, char **argv)
{
	struct param_list_context context = {.shell = sh};
	int result;

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

	if (result != 0) {
		shell_error(sh, "parameter list failed (%d)", result);
	}
	return result;
}

static int cmd_kfsw_param_get(const struct shell *sh, size_t argc, char **argv)
{
	struct kfsw_param_value value;
	const char *name;
	uint16_t node = 0U;
	int result;

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

	if (result != 0) {
		return print_param_error(sh, "get", name, result);
	}
	print_param_value(sh, node, name, &value);
	return 0;
}

static int cmd_kfsw_param_set(const struct shell *sh, size_t argc, char **argv)
{
	struct kfsw_param_value value;
	const char *name;
	const char *text;
	uint16_t node = 0U;
	int result;

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
	if (result != 0) {
		return print_param_error(sh, "set", name, result);
	}

	result = parse_param_value(text, &value);
	if (result != 0) {
		shell_error(sh, "set: invalid %s value '%s'", kfsw_param_type_name(value.type),
			    text);
		return result;
	}

	result = (node == 0U) ? kfsw_param_set(name, &value)
			      : kfsw_param_remote_set(node, name, &value);
	if (result != 0) {
		return print_param_error(sh, "set", name, result);
	}
	print_param_value(sh, node, name, &value);
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(
	kfsw_param_commands,
	SHELL_CMD_ARG(get, NULL,
		      "Get local or remote value: get [node] <name>.",
		      cmd_kfsw_param_get, 2, 1),
	SHELL_CMD_ARG(list, NULL, "List local or remote parameters: list [node].",
		      cmd_kfsw_param_list, 1, 1),
	SHELL_CMD_ARG(set, NULL,
		      "Set local or remote value: set [node] <name> <value>.",
		      cmd_kfsw_param_set, 3, 1),
	SHELL_SUBCMD_SET_END);
#endif

#if CONFIG_KFSW_CSP
#define KFSW_CSP_PING_TIMEOUT_MS 1000U
#define KFSW_CSP_PING_PAYLOAD_SIZE 10U

static int cmd_kfsw_csp_info(const struct shell *sh, size_t argc, char **argv)
{
	struct kfsw_csp_info info;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	kfsw_csp_get_info(&info);
	shell_print(sh, "CSP node: %u", info.address);
	shell_print(sh, "hostname: %s", info.hostname);
	shell_print(sh, "model: %s", info.model);
	shell_print(sh, "revision: %s", info.revision);
	shell_print(sh, "initialized: %s", info.initialized ? "yes" : "no");
	shell_print(sh, "router: %s", info.router_running ? "running" : "stopped");
	shell_print(sh, "free_buffers: %zu", info.free_buffers);

	return 0;
}

static bool print_csp_interface(const struct kfsw_csp_interface_info *interface_info, void *context)
{
	const struct shell *sh = context;

	shell_print(sh,
		    "%s addr=%u/%u default=%s tx=%u rx=%u txerr=%u "
		    "rxerr=%u drop=%u",
		    interface_info->name, interface_info->address, interface_info->prefix_length,
		    interface_info->is_default ? "yes" : "no", interface_info->tx_packets,
		    interface_info->rx_packets, interface_info->tx_errors,
		    interface_info->rx_errors, interface_info->dropped_packets);

	return true;
}

static int cmd_kfsw_csp_interfaces(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	kfsw_csp_visit_interfaces(print_csp_interface, (void *)sh);
	return 0;
}

static bool print_csp_route(const struct kfsw_csp_route_info *route_info, void *context)
{
	const struct shell *sh = context;

	if (route_info->has_via) {
		shell_print(sh, "%u/%u -> %s via %u", route_info->address,
			    route_info->prefix_length, route_info->interface_name, route_info->via);
	} else {
		shell_print(sh, "%u/%u -> %s direct", route_info->address,
			    route_info->prefix_length, route_info->interface_name);
	}

	return true;
}

static int cmd_kfsw_csp_routes(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	kfsw_csp_visit_routes(print_csp_route, (void *)sh);
	return 0;
}

static int cmd_kfsw_csp_ping(const struct shell *sh, size_t argc, char **argv)
{
	unsigned long node;
	uint32_t round_trip_ms;
	int parse_error = 0;
	int result;

	ARG_UNUSED(argc);

	node = shell_strtoul(argv[1], 10, &parse_error);
	if (parse_error != 0 || node > 16383U) {
		shell_error(sh, "CSP node must be in range 0..16383");
		return -EINVAL;
	}

	result = kfsw_csp_ping((uint16_t)node, KFSW_CSP_PING_TIMEOUT_MS, KFSW_CSP_PING_PAYLOAD_SIZE,
			       &round_trip_ms);
	if (result != 0) {
		shell_error(sh, "CSP ping %lu: failed (%d)", node, result);
		return result;
	}

	shell_print(sh, "CSP ping %lu: success, rtt_ms=%u", node, round_trip_ms);
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(kfsw_csp_commands,
	SHELL_CMD_ARG(info, NULL, "Show local CSP identity and router state.",
		      cmd_kfsw_csp_info, 1, 0),
	SHELL_CMD_ARG(interfaces, NULL, "Show registered CSP interfaces.",
		      cmd_kfsw_csp_interfaces, 1, 0),
	SHELL_CMD_ARG(ping, NULL, "Ping a CSP node: ping <node>.",
		      cmd_kfsw_csp_ping, 2, 0),
	SHELL_CMD_ARG(routes, NULL, "Show the CSP static routing table.",
		      cmd_kfsw_csp_routes, 1, 0),
	SHELL_SUBCMD_SET_END
);
#endif

#if CONFIG_KFSW_CSP_KISS_UART
#define KFSW_UART_TEST_TIMEOUT_MS 1000U

static int cmd_kfsw_uart_info(const struct shell *sh, size_t argc, char **argv)
{
	struct kfsw_uart_info info;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	kfsw_uart_get_info(&info);
	shell_print(sh, "UART transport");
	shell_print(sh, "device: %s", info.device_name);
	shell_print(sh, "baudrate: %u", info.baudrate);
	shell_print(sh, "configuration: 8N1, flow control none");
	shell_print(sh, "ready: %s", info.ready ? "yes" : "no");
	shell_print(sh, "CSP interface: %s", info.interface_name);
	shell_print(sh, "CSP node: %u", info.node);
	shell_print(sh, "CSP peer: %u", info.peer);
	shell_print(sh, "KISS tx=%u rx=%u txerr=%u rxerr=%u drop=%u frame=%u", info.tx_packets,
		    info.rx_packets, info.tx_errors, info.rx_errors, info.dropped_packets,
		    info.frame_errors);

	return 0;
}

static int cmd_kfsw_uart_test(const struct shell *sh, size_t argc, char **argv)
{
	struct kfsw_uart_test_result test_result;
	int result;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	result = kfsw_uart_test(KFSW_UART_TEST_TIMEOUT_MS, &test_result);
	if (result != 0) {
		shell_error(sh, "UART CSP test: FAIL (%d)", result);
		return result;
	}

	shell_print(sh, "UART CSP test: PASS");
	shell_print(sh, "peer: %u", test_result.peer);
	shell_print(sh, "interface: %s", test_result.interface_name);
	shell_print(sh, "rtt_ms: %u", test_result.round_trip_ms);

	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(kfsw_uart_commands,
	SHELL_CMD_ARG(info, NULL, "Show dedicated CSP UART status.",
		      cmd_kfsw_uart_info, 1, 0),
	SHELL_CMD_ARG(test, NULL, "Ping the configured peer over UART/KISS.",
		      cmd_kfsw_uart_test, 1, 0),
	SHELL_SUBCMD_SET_END
);
#endif

#if CONFIG_KFSW_STORAGE
#define KFSW_STORAGE_TEST_PATH KFSW_STORAGE_MOUNT_POINT "/.storage-test"
#define KFSW_STORAGE_PERSISTENCE_PATH KFSW_STORAGE_MOUNT_POINT "/.persistence-test"
#define KFSW_STORAGE_TEST_VALUE_MAX_SIZE 48U

static int storage_write_file(const char *path, const char *value)
{
	struct fs_file_t file;
	const size_t value_size = strlen(value);
	ssize_t written;
	int close_result;
	int result;

	fs_file_t_init(&file);
	result = fs_open(&file, path, FS_O_CREATE | FS_O_WRITE | FS_O_TRUNC);
	if (result != 0) {
		return result;
	}

	written = fs_write(&file, value, value_size);
	if (written == (ssize_t)value_size) {
		result = fs_sync(&file);
	} else {
		result = (written < 0) ? (int)written : -EIO;
	}

	close_result = fs_close(&file);
	return (result != 0) ? result : close_result;
}

static int storage_read_file(const char *path, const char *expected)
{
	struct fs_file_t file;
	char value[KFSW_STORAGE_TEST_VALUE_MAX_SIZE + 1U];
	const size_t expected_size = strlen(expected);
	ssize_t bytes_read;
	int close_result;
	int result = 0;

	if (expected_size > KFSW_STORAGE_TEST_VALUE_MAX_SIZE) {
		return -EMSGSIZE;
	}

	fs_file_t_init(&file);
	result = fs_open(&file, path, FS_O_READ);
	if (result != 0) {
		return result;
	}

	bytes_read = fs_read(&file, value, sizeof(value));
	if (bytes_read < 0) {
		result = (int)bytes_read;
	} else if ((size_t)bytes_read != expected_size) {
		result = -EIO;
	} else {
		value[bytes_read] = '\0';
		if (memcmp(value, expected, expected_size) != 0) {
			result = -EIO;
		}
	}

	close_result = fs_close(&file);
	return (result != 0) ? result : close_result;
}

static int cmd_kfsw_storage_info(const struct shell *sh, size_t argc, char **argv)
{
	struct kfsw_storage_info info;
	int result;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	result = kfsw_storage_get_info(&info);
	if (result != 0) {
		shell_error(sh, "Storage info: FAIL (%d)", result);
		return result;
	}

	shell_print(sh, "K-FSW storage");
	shell_print(sh, "filesystem: %s", info.filesystem);
	shell_print(sh, "backend: %s", info.backend);
	shell_print(sh, "mount_point: %s", info.mount_point);
	shell_print(sh, "ready: %s", info.ready ? "yes" : "no");
	shell_print(sh, "total_bytes: %" PRIu64, info.total_bytes);
	shell_print(sh, "free_bytes: %" PRIu64, info.free_bytes);
	return 0;
}

static int storage_basic_test(const struct shell *sh)
{
	static const char initial_value[] = "kfsw-storage-create";
	static const char overwritten_value[] = "kfsw-storage-overwrite";
	int result;

	result = storage_write_file(KFSW_STORAGE_TEST_PATH, initial_value);
	if (result == 0) {
		result = storage_write_file(KFSW_STORAGE_TEST_PATH, overwritten_value);
	}
	if (result == 0) {
		result = storage_read_file(KFSW_STORAGE_TEST_PATH, overwritten_value);
	}
	if (result == 0) {
		result = fs_unlink(KFSW_STORAGE_TEST_PATH);
	}

	if (result != 0) {
		(void)fs_unlink(KFSW_STORAGE_TEST_PATH);
		shell_error(sh, "Storage test: FAIL (%d)", result);
		return result;
	}

	shell_print(sh, "Storage test: PASS");
	return 0;
}

static int storage_persistence_test(const struct shell *sh, const char *operation,
				    const char *value)
{
	int result;

	if ((value[0] == '\0') || (strlen(value) > KFSW_STORAGE_TEST_VALUE_MAX_SIZE)) {
		shell_error(sh, "Storage persistence value must contain 1..%u characters",
			    KFSW_STORAGE_TEST_VALUE_MAX_SIZE);
		return -EMSGSIZE;
	}

	if (strcmp(operation, "write") == 0) {
		result = storage_write_file(KFSW_STORAGE_PERSISTENCE_PATH, value);
		if (result == 0) {
			shell_print(sh, "Storage persistence write: PASS");
			return 0;
		}
	} else if (strcmp(operation, "read") == 0) {
		result = storage_read_file(KFSW_STORAGE_PERSISTENCE_PATH, value);
		if (result == 0) {
			result = fs_unlink(KFSW_STORAGE_PERSISTENCE_PATH);
		}
		if (result == 0) {
			shell_print(sh, "Storage persistence read: PASS");
			return 0;
		}
	} else {
		shell_error(sh, "Usage: kfsw storage test [write|read <value>]");
		return -EINVAL;
	}

	shell_error(sh, "Storage persistence %s: FAIL (%d)", operation, result);
	return result;
}

static int cmd_kfsw_storage_test(const struct shell *sh, size_t argc, char **argv)
{
	if (!kfsw_storage_is_ready()) {
		shell_error(sh, "Storage test: FAIL (not ready)");
		return -EACCES;
	}

	if (argc == 1U) {
		return storage_basic_test(sh);
	}
	if (argc == 3U) {
		return storage_persistence_test(sh, argv[1], argv[2]);
	}

	shell_error(sh, "Usage: kfsw storage test [write|read <value>]");
	return -EINVAL;
}

SHELL_STATIC_SUBCMD_SET_CREATE(
	kfsw_storage_commands,
	SHELL_CMD_ARG(info, NULL, "Show K-FSW filesystem storage status.",
		      cmd_kfsw_storage_info, 1, 0),
	SHELL_CMD_ARG(test, NULL, "Run storage test: test [write|read <value>].",
		      cmd_kfsw_storage_test, 1, 2),
	SHELL_SUBCMD_SET_END);
#endif

static int cmd_kfsw_status(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(sh, "K-FSW status");
	shell_print(sh, "board: %s", CONFIG_BOARD_TARGET);
	shell_print(sh, "uptime_ms: %llu", (unsigned long long)kfsw_time_monotonic_ms());

	return 0;
}

static int cmd_kfsw_time(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(sh, "monotonic_ms: %llu", (unsigned long long)kfsw_time_monotonic_ms());
	shell_print(sh, "monotonic_us: %llu", (unsigned long long)kfsw_time_monotonic_us());

	return 0;
}

static int cmd_kfsw_version(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(sh, "K-FSW: kfsw-dev");
	shell_print(sh, "Zephyr: %s", KERNEL_VERSION_STRING);
	shell_print(sh, "Board: %s", CONFIG_BOARD_TARGET);

	return 0;
}

static int cmd_kfsw_log_test(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	ARG_UNUSED(sh);

	kfsw_log_error("K-FSW shell log test: error");
	kfsw_log_warning("K-FSW shell log test: warning");
	kfsw_log_info("K-FSW shell log test: info");
	kfsw_log_debug("K-FSW shell log test: debug");

	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(kfsw_log_commands,
	SHELL_CMD_ARG(test, NULL, "Exercise all K-FSW log levels.",
		      cmd_kfsw_log_test, 1, 0),
	SHELL_SUBCMD_SET_END
);

SHELL_STATIC_SUBCMD_SET_CREATE(kfsw_commands,
#if CONFIG_KFSW_CSP_KISS_UART
	SHELL_CMD(uart, &kfsw_uart_commands, "K-FSW CSP UART commands.", NULL),
#endif
#if CONFIG_KFSW_CSP
	SHELL_CMD(csp, &kfsw_csp_commands, "K-FSW CSP commands.", NULL),
#endif
#if CONFIG_KFSW_PARAM
	SHELL_CMD(param, &kfsw_param_commands, "K-FSW parameter commands.", NULL),
#endif
#if CONFIG_KFSW_STORAGE
	SHELL_CMD(storage, &kfsw_storage_commands, "K-FSW filesystem storage commands.", NULL),
#endif
	SHELL_CMD(log, &kfsw_log_commands, "K-FSW logging commands.", NULL),
	SHELL_CMD_ARG(status, NULL, "Show basic K-FSW runtime status.",
		      cmd_kfsw_status, 1, 0),
	SHELL_CMD_ARG(time, NULL, "Show K-FSW monotonic time.",
		      cmd_kfsw_time, 1, 0),
	SHELL_CMD_ARG(version, NULL, "Show K-FSW build information.",
		      cmd_kfsw_version, 1, 0),
	SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(kfsw, &kfsw_commands,
		   "K-FSW developer/debug commands.", NULL);
