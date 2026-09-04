#include <errno.h>
#include <stdint.h>

#include <zephyr/sys/util.h>

#include <kfsw/services/parameter.h>

#include "tables.h"

#if CONFIG_KFSW_HEALTH
#define KFSW_APP_REPORT_MS_DEFAULT (CONFIG_KFSW_APP_HEALTH_DEADLINE_MS / 4U)
/* A report period at or beyond half the deadline leaves no room for an ordinary
 * scheduling delay, so one late cycle would look like a stopped thread and
 * reset a working board. Refused rather than accepted: this is the one value
 * here that can reset a healthy satellite by being set to a plausible number.
 */
#define KFSW_APP_REPORT_MS_MAX (CONFIG_KFSW_APP_HEALTH_DEADLINE_MS / 2U)
#else
#define KFSW_APP_REPORT_MS_DEFAULT 1000U
#define KFSW_APP_REPORT_MS_MAX UINT16_MAX
#endif

/* Read once at start-up, before any service runs, for a marginal power budget.
 * Stored rather than live: by the time it could be applied the boot it delays
 * has already happened.
 */
static uint16_t system_boot_delay_ms;
static uint16_t system_app_report_ms = KFSW_APP_REPORT_MS_DEFAULT;

static int validate_app_report_ms(const union kfsw_param_scalar *value)
{
	if ((value->u16 == 0U) || (value->u16 > KFSW_APP_REPORT_MS_MAX)) {
		return -ERANGE;
	}
	return 0;
}

static const struct kfsw_param_definition system_param_definitions[] = {
	{
		.offset = 0x00U,
		.type = KFSW_PARAM_U16,
		.flags = KFSW_PARAM_FLAG_CONFIGURATION | KFSW_PARAM_FLAG_PERSISTENT,
		.name = "boot_delay_ms",
		.unit = "ms",
		.description = "Delay before services start on the next boot",
		.value = &system_boot_delay_ms,
		.default_value = {.u16 = 0U},
	},
	{
		.offset = 0x02U,
		.type = KFSW_PARAM_U16,
		/* Declared live without a callback: the application loop reads
		 * this every cycle, so the next cycle already uses a new value
		 * and there is nothing for a callback to apply.
		 */
		.flags = KFSW_PARAM_FLAG_CONFIGURATION | KFSW_PARAM_FLAG_PERSISTENT |
			 KFSW_PARAM_FLAG_LIVE,
		.name = "app_report_ms",
		.unit = "ms",
		.description = "How often the application thread reports it is running",
		.value = &system_app_report_ms,
		.default_value = {.u16 = KFSW_APP_REPORT_MS_DEFAULT},
		.validate = validate_app_report_ms,
	},
};

const struct kfsw_param_definition_set kfsw_system_param_definitions = {
	.table = KFSW_PARAM_TABLE_SYSTEM,
	.name = KFSW_PARAM_TABLE_SYSTEM_NAME,
	.definitions = system_param_definitions,
	.count = ARRAY_SIZE(system_param_definitions),
};

uint16_t kfsw_system_boot_delay_ms(void)
{
	return system_boot_delay_ms;
}

uint16_t kfsw_system_app_report_ms(void)
{
	return system_app_report_ms;
}
