#include <stdint.h>

#include <zephyr/sys/util.h>

#include <kfsw/services/parameter.h>

#include "parameter_definitions.h"

#if CONFIG_KFSW_CSP
#define KFSW_APP_NODE_ID_DEFAULT CONFIG_KFSW_CSP_ADDRESS
#else
#define KFSW_APP_NODE_ID_DEFAULT 0U
#endif

static uint16_t node_id = KFSW_APP_NODE_ID_DEFAULT;

static const struct kfsw_param_definition app_param_definitions[] = {
	{
		.id = 0U,
		.type = KFSW_PARAM_U16,
		.flags = KFSW_PARAM_FLAG_READ_ONLY | KFSW_PARAM_FLAG_SYSTEM_INFO,
		.name = "node_id",
		.description = "Build-time CSP node address",
		.value = &node_id,
		.default_value = {.u16 = KFSW_APP_NODE_ID_DEFAULT},
	},
};

const struct kfsw_param_definition_set kfsw_app_param_definitions = {
	.definitions = app_param_definitions,
	.count = ARRAY_SIZE(app_param_definitions),
};
