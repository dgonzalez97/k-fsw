#include <stdint.h>

#include <zephyr/sys/util.h>

#include <kfsw/platform/reset.h>
#include <kfsw/services/parameter.h>

#include "tables.h"

#if CONFIG_KFSW_CSP
#include <kfsw/comms/csp.h>
#define KFSW_BOARD_NODE_ID_DEFAULT CONFIG_KFSW_CSP_ADDRESS
#else
#define KFSW_BOARD_NODE_ID_DEFAULT 0U
#endif

/* Every value here is read-only. The configuration rows this table is meant to
 * carry -- a writable CSP address, the KISS baud rate -- would have to be read
 * back by kfsw-comms and kfsw-platform, which sit below the parameter service.
 * Publishing them as writable before that path exists would let an operator
 * change a value that nothing applies, which is the one failure this table
 * exists to prevent.
 */
static uint16_t board_node_id = KFSW_BOARD_NODE_ID_DEFAULT;
static uint32_t board_reset_cause;
static uint8_t board_csp_enabled = IS_ENABLED(CONFIG_KFSW_CSP);
static uint8_t board_kiss_enabled = IS_ENABLED(CONFIG_KFSW_CSP_KISS_UART);
static uint8_t board_shell_enabled = IS_ENABLED(CONFIG_SHELL);
static uint8_t board_storage_enabled = IS_ENABLED(CONFIG_KFSW_STORAGE);

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
		.offset = 0x02U,
		.type = KFSW_PARAM_U8,
		.flags = KFSW_PARAM_FLAG_READ_ONLY | KFSW_PARAM_FLAG_SYSTEM_INFO,
		.name = "csp_enabled",
		.description = "Whether CSP is composed into this build",
		.value = &board_csp_enabled,
		.default_value = {.u8 = IS_ENABLED(CONFIG_KFSW_CSP)},
	},
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
