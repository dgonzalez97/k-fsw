#include <errno.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/sys/util.h>

#include <kfsw/platform/reset.h>
#include <kfsw/services/parameter.h>

#include "tables.h"

#if CONFIG_KFSW_CSP
#include <kfsw/comms/csp.h>
#if CONFIG_KFSW_CSP_CAN
#include <kfsw/comms/can.h>
#endif
#define KFSW_BOARD_NODE_ID_DEFAULT CONFIG_KFSW_CSP_ADDRESS
#else
#define KFSW_BOARD_NODE_ID_DEFAULT 0U
#endif

/* Identity is read from the running CSP configuration rather than the build
 * options, so the table reports what the node actually came up as.
 *
 * The switches below are read-only. A writable CSP address or KISS baud rate
 * would have to be read back by kfsw-comms and kfsw-platform, which sit below
 * the parameter service. Publishing them as writable before that path exists
 * would let an operator change a value that nothing applies, which is the one
 * failure this table exists to prevent.
 */
/* Sized so the identities a composition actually uses fit whole. A truncated
 * identity is worse than a missing one: it looks like a different node, and
 * the operator comparing it against a build record has no way to tell.
 */
#define KFSW_BOARD_UID_SIZE 32U
#define KFSW_BOARD_MODEL_SIZE 32U
#define KFSW_BOARD_REVISION_SIZE 40U

static char board_uid[KFSW_BOARD_UID_SIZE];
static char board_model[KFSW_BOARD_MODEL_SIZE];
static char board_revision[KFSW_BOARD_REVISION_SIZE];
static uint16_t board_node_id = KFSW_BOARD_NODE_ID_DEFAULT;
static uint32_t board_reset_cause;
static uint8_t board_csp_enabled = IS_ENABLED(CONFIG_KFSW_CSP);
static uint8_t board_kiss_enabled = IS_ENABLED(CONFIG_KFSW_CSP_KISS_UART);
static uint8_t board_shell_enabled = IS_ENABLED(CONFIG_SHELL);
static uint8_t board_storage_enabled = IS_ENABLED(CONFIG_KFSW_STORAGE);
static uint8_t board_can_enabled = IS_ENABLED(CONFIG_KFSW_CSP_CAN);
#if CONFIG_KFSW_CSP_CAN
static uint32_t board_can_speed = CONFIG_KFSW_CSP_CAN_BITRATE;
#endif

/* Copied out of the running CSP identity rather than duplicated as build
 * constants: two sources for one fact eventually disagree, and the one an
 * operator can reach would be the wrong one.
 */
static void sample_identity(char *destination, size_t size, const char *source)
{
	size_t length = 0U;

	while ((length + 1U < size) && (source != NULL) && (source[length] != '\0')) {
		destination[length] = source[length];
		length++;
	}
	destination[length] = '\0';
}

static void sample_uid(void *value)
{
#if CONFIG_KFSW_CSP
	struct kfsw_csp_info info;

	kfsw_csp_get_info(&info);
	sample_identity(value, KFSW_BOARD_UID_SIZE, info.hostname);
#else
	ARG_UNUSED(value);
#endif
}

static void sample_model(void *value)
{
#if CONFIG_KFSW_CSP
	struct kfsw_csp_info info;

	kfsw_csp_get_info(&info);
	sample_identity(value, KFSW_BOARD_MODEL_SIZE, info.model);
#else
	ARG_UNUSED(value);
#endif
}

static void sample_revision(void *value)
{
#if CONFIG_KFSW_CSP
	struct kfsw_csp_info info;

	kfsw_csp_get_info(&info);
	sample_identity(value, KFSW_BOARD_REVISION_SIZE, info.revision);
#else
	ARG_UNUSED(value);
#endif
}

static void sample_node_id(void *value)
{
#if CONFIG_KFSW_CSP
	struct kfsw_csp_info info;

	/* Read from the running configuration rather than the build option, so
	 * this reports the address the node actually came up as.
	 */
	kfsw_csp_get_info(&info);
	*(uint16_t *)value = info.address;
#else
	ARG_UNUSED(value);
#endif
}

static void sample_reset_cause(void *value)
{
	uint32_t cause = 0U;

	/* Latched by the platform at boot; a failed read reports zero rather
	 * than the previous sample, so a stale cause cannot be mistaken for a
	 * fresh one.
	 */
	if (kfsw_platform_get_reset_cause(&cause) != 0) {
		cause = 0U;
	}
	*(uint32_t *)value = cause;
}

#if CONFIG_KFSW_CSP_CAN
static void sample_can_speed(void *value)
{
	struct kfsw_can_info info;

	kfsw_can_get_info(&info);
	*(uint32_t *)value = info.bitrate;
}

/* Refused here rather than in the change callback, because a change callback
 * runs after the value has already been stored and cannot put the old one
 * back. A rate the controller would reject has to be stopped before that.
 */
static int validate_can_speed(const union kfsw_param_scalar *value)
{
	return kfsw_can_bitrate_supported(value->u32) ? 0 : -EINVAL;
}

/* Both ends of a CAN bus have to agree, so a node reconfigured on its own goes
 * quiet until whatever is at the other end follows. Writing this over CAN
 * therefore cuts the link that carried the write.
 */
static void changed_can_speed(const union kfsw_param_scalar *value)
{
	(void)kfsw_can_set_bitrate(value->u32);
}
#endif

static const struct kfsw_param_definition board_param_definitions[] = {
	{
		.offset = 0x00U,
		.type = KFSW_PARAM_U16,
		.flags = KFSW_PARAM_FLAG_READ_ONLY | KFSW_PARAM_FLAG_SYSTEM_INFO,
		.name = "node_id",
		.description = "CSP address this node is running as",
		.value = &board_node_id,
		.default_value = {.u16 = KFSW_BOARD_NODE_ID_DEFAULT},
		.sample = sample_node_id,
	},
	{
		.offset = 0x10U,
		.type = KFSW_PARAM_STRING,
		.capacity = KFSW_BOARD_UID_SIZE,
		.flags = KFSW_PARAM_FLAG_READ_ONLY | KFSW_PARAM_FLAG_SYSTEM_INFO,
		.name = "uid",
		.description = "Unit identity, as reported by csp ident",
		.value = board_uid,
		.sample = sample_uid,
	},
	{
		.offset = 0x20U,
		.type = KFSW_PARAM_STRING,
		.capacity = KFSW_BOARD_MODEL_SIZE,
		.flags = KFSW_PARAM_FLAG_READ_ONLY | KFSW_PARAM_FLAG_SYSTEM_INFO,
		.name = "model",
		.description = "Composition this build identifies as",
		.value = board_model,
		.sample = sample_model,
	},
	{
		.offset = 0x30U,
		.type = KFSW_PARAM_STRING,
		.capacity = KFSW_BOARD_REVISION_SIZE,
		.flags = KFSW_PARAM_FLAG_READ_ONLY | KFSW_PARAM_FLAG_SYSTEM_INFO,
		.name = "revision",
		.description = "Build revision, as reported by csp ident",
		.value = board_revision,
		.sample = sample_revision,
	},
	{
		.offset = 0x02U,
		.type = KFSW_PARAM_U8,
		.flags = KFSW_PARAM_FLAG_READ_ONLY | KFSW_PARAM_FLAG_SYSTEM_INFO,
		.name = "csp_enabled",
		.description = "Whether CSP is composed into this build",
		.value = &board_csp_enabled,
		.default_value = {.u8 = IS_ENABLED(CONFIG_KFSW_CSP)},
	},
	{
		.offset = 0x05U,
		.type = KFSW_PARAM_U8,
		.flags = KFSW_PARAM_FLAG_READ_ONLY | KFSW_PARAM_FLAG_SYSTEM_INFO,
		.name = "can_enabled",
		.description = "Whether the CAN interface is composed",
		.value = &board_can_enabled,
		.default_value = {.u8 = IS_ENABLED(CONFIG_KFSW_CSP_CAN)},
	},
#if CONFIG_KFSW_CSP_CAN
	{
		.offset = 0x0cU,
		.type = KFSW_PARAM_U32,
		.name = "can_speed",
		.description = "CAN bitrate: 125000, 250000, 500000, 800000 or 1000000",
		.value = &board_can_speed,
		.default_value = {.u32 = CONFIG_KFSW_CSP_CAN_BITRATE},
		.sample = sample_can_speed,
		.validate = validate_can_speed,
		.changed = changed_can_speed,
	},
#endif
	{
		.offset = 0x03U,
		.type = KFSW_PARAM_U8,
		.flags = KFSW_PARAM_FLAG_READ_ONLY | KFSW_PARAM_FLAG_SYSTEM_INFO,
		.name = "kiss_enabled",
		.description = "Whether the KISS interface is composed",
		.value = &board_kiss_enabled,
		.default_value = {.u8 = IS_ENABLED(CONFIG_KFSW_CSP_KISS_UART)},
	},
	{
		.offset = 0x04U,
		.type = KFSW_PARAM_X32,
		.flags = KFSW_PARAM_FLAG_READ_ONLY | KFSW_PARAM_FLAG_SYSTEM_INFO,
		.name = "reset_cause",
		.description = "Reset cause latched at boot",
		.value = &board_reset_cause,
		.default_value = {.u32 = 0U},
		.sample = sample_reset_cause,
	},
	{
		.offset = 0x08U,
		.type = KFSW_PARAM_U8,
		.flags = KFSW_PARAM_FLAG_READ_ONLY | KFSW_PARAM_FLAG_SYSTEM_INFO,
		.name = "shell_enabled",
		.description = "Whether the debug shell is composed",
		.value = &board_shell_enabled,
		.default_value = {.u8 = IS_ENABLED(CONFIG_SHELL)},
	},
	{
		.offset = 0x09U,
		.type = KFSW_PARAM_U8,
		.flags = KFSW_PARAM_FLAG_READ_ONLY | KFSW_PARAM_FLAG_SYSTEM_INFO,
		.name = "storage_enabled",
		.description = "Whether persistent storage is composed",
		.value = &board_storage_enabled,
		.default_value = {.u8 = IS_ENABLED(CONFIG_KFSW_STORAGE)},
	},
};

const struct kfsw_param_definition_set kfsw_board_param_definitions = {
	.table = KFSW_PARAM_TABLE_BOARD,
	.name = KFSW_PARAM_TABLE_BOARD_NAME,
	.definitions = board_param_definitions,
	.count = ARRAY_SIZE(board_param_definitions),
};
