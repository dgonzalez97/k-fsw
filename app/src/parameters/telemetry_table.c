#include <stdint.h>

#include <zephyr/sys/util.h>

#include <kfsw/platform/time.h>
#include <kfsw/services/parameter.h>

#include "tables.h"

#if CONFIG_KFSW_CSP
#include <kfsw/comms/csp.h>
#endif
#if CONFIG_KFSW_STORAGE
#include <kfsw/platform/storage.h>
#endif

#define KFSW_MS_PER_S 1000U
#define KFSW_BYTES_PER_KB 1024U

/* Every value is sampled at the moment it is read rather than refreshed on a
 * timer. A housekeeping value is worth having only if it is current when it was
 * asked for; an uptime refreshed once a second is wrong by up to a second every
 * time somebody reads it.
 */
static uint32_t telemetry_uptime_s;
static uint16_t telemetry_csp_buf_free;
static uint32_t telemetry_fs_free_kb;
static uint32_t telemetry_fs_total_kb;
static uint8_t telemetry_fs_mounted;

static void sample_uptime_s(void *value)
{
	*(uint32_t *)value = (uint32_t)(kfsw_time_monotonic_ms() / KFSW_MS_PER_S);
}

static void sample_csp_buf_free(void *value)
{
#if CONFIG_KFSW_CSP
	struct kfsw_csp_info info;

	/* Buffer exhaustion is a real failure mode and is invisible without
	 * this: a node that has stopped answering because it has no buffers
	 * left looks exactly like one that has stopped answering.
	 */
	kfsw_csp_get_info(&info);
	*(uint16_t *)value = (uint16_t)info.free_buffers;
#else
	*(uint16_t *)value = 0U;
#endif
}

#if CONFIG_KFSW_STORAGE
static void sample_storage(void)
{
	struct kfsw_storage_info info;

	if (kfsw_storage_get_info(&info) != 0) {
		telemetry_fs_mounted = 0U;
		telemetry_fs_free_kb = 0U;
		telemetry_fs_total_kb = 0U;
		return;
	}
	telemetry_fs_mounted = info.ready ? 1U : 0U;
	telemetry_fs_free_kb = (uint32_t)(info.free_bytes / KFSW_BYTES_PER_KB);
	telemetry_fs_total_kb = (uint32_t)(info.total_bytes / KFSW_BYTES_PER_KB);
}
#endif

static void sample_fs_free_kb(void *value)
{
#if CONFIG_KFSW_STORAGE
	sample_storage();
#endif
	*(uint32_t *)value = telemetry_fs_free_kb;
}

static void sample_fs_total_kb(void *value)
{
#if CONFIG_KFSW_STORAGE
	sample_storage();
#endif
	*(uint32_t *)value = telemetry_fs_total_kb;
}

static void sample_fs_mounted(void *value)
{
#if CONFIG_KFSW_STORAGE
	sample_storage();
#endif
	*(uint8_t *)value = telemetry_fs_mounted;
}

static const struct kfsw_param_definition telemetry_param_definitions[] = {
	{
		.offset = 0x00U,
		.type = KFSW_PARAM_U32,
		.flags = KFSW_PARAM_FLAG_READ_ONLY,
		.name = "uptime_s",
		.unit = "s",
		.description = "Seconds since boot",
		.value = &telemetry_uptime_s,
		.default_value = {.u32 = 0U},
		.sample = sample_uptime_s,
	},
	{
		.offset = 0x04U,
		.type = KFSW_PARAM_U32,
		.flags = KFSW_PARAM_FLAG_READ_ONLY,
		.name = "fs_total_kb",
		.unit = "kB",
		.description = "Filesystem capacity, or zero while unmounted",
		.value = &telemetry_fs_total_kb,
		.default_value = {.u32 = 0U},
		.sample = sample_fs_total_kb,
	},
	{
		.offset = 0x08U,
		.type = KFSW_PARAM_U32,
		.flags = KFSW_PARAM_FLAG_READ_ONLY,
		.name = "fs_free_kb",
		.unit = "kB",
		.description = "Filesystem space available, or zero while unmounted",
		.value = &telemetry_fs_free_kb,
		.default_value = {.u32 = 0U},
		.sample = sample_fs_free_kb,
	},
	{
		.offset = 0x0cU,
		.type = KFSW_PARAM_U16,
		.flags = KFSW_PARAM_FLAG_READ_ONLY,
		.name = "csp_buf_free",
		.description = "CSP buffers available to the router",
		.value = &telemetry_csp_buf_free,
		.default_value = {.u16 = 0U},
		.sample = sample_csp_buf_free,
	},
	{
		.offset = 0x0eU,
		.type = KFSW_PARAM_U8,
		.flags = KFSW_PARAM_FLAG_READ_ONLY,
		.name = "fs_mounted",
		.description = "Whether the filesystem is mounted and usable",
		.value = &telemetry_fs_mounted,
		.default_value = {.u8 = 0U},
		.sample = sample_fs_mounted,
	},
};

const struct kfsw_param_definition_set kfsw_telemetry_param_definitions = {
	.table = KFSW_PARAM_TABLE_TELEMETRY,
	.name = KFSW_PARAM_TABLE_TELEMETRY_NAME,
	.definitions = telemetry_param_definitions,
	.count = ARRAY_SIZE(telemetry_param_definitions),
};
