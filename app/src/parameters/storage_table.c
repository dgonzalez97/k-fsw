#include <stdint.h>

#include <zephyr/sys/util.h>

#include <kfsw/platform/storage.h>
#include <kfsw/services/parameter.h>

#include "tables.h"

#define KFSW_BYTES_PER_KB 1024U
#define KFSW_PERCENT 100U

static uint32_t storage_total_kb;
static uint32_t storage_free_kb;
static uint8_t storage_used_pct;
static uint8_t storage_mounted;

static void sample_storage(void)
{
	struct kfsw_storage_info info;
	uint64_t used;

	if (kfsw_storage_get_info(&info) != 0) {
		storage_mounted = 0U;
		storage_total_kb = 0U;
		storage_free_kb = 0U;
		storage_used_pct = 0U;
		return;
	}

	storage_mounted = info.ready ? 1U : 0U;
	storage_total_kb = (uint32_t)(info.total_bytes / KFSW_BYTES_PER_KB);
	storage_free_kb = (uint32_t)(info.free_bytes / KFSW_BYTES_PER_KB);

	/* Reported as zero while unmounted rather than as a full filesystem: an
	 * unmounted volume is not 100% used, and saying so would trigger the
	 * wrong response on the ground.
	 */
	if ((info.total_bytes == 0U) || (info.free_bytes > info.total_bytes)) {
		storage_used_pct = 0U;
		return;
	}
	used = info.total_bytes - info.free_bytes;
	storage_used_pct = (uint8_t)((used * KFSW_PERCENT) / info.total_bytes);
}

static void sample_total_kb(void *value)
{
	sample_storage();
	*(uint32_t *)value = storage_total_kb;
}

static void sample_free_kb(void *value)
{
	sample_storage();
	*(uint32_t *)value = storage_free_kb;
}

static void sample_used_pct(void *value)
{
	sample_storage();
	*(uint8_t *)value = storage_used_pct;
}

static void sample_mounted(void *value)
{
	sample_storage();
	*(uint8_t *)value = storage_mounted;
}

static const struct kfsw_param_definition storage_param_definitions[] = {
	{
		.offset = 0x00U,
		.type = KFSW_PARAM_U32,
		.flags = KFSW_PARAM_FLAG_READ_ONLY,
		.name = "total_kb",
		.unit = "kB",
		.description = "Filesystem capacity, or zero while unmounted",
		.value = &storage_total_kb,
		.default_value = {.u32 = 0U},
		.sample = sample_total_kb,
	},
	{
		.offset = 0x04U,
		.type = KFSW_PARAM_U32,
		.flags = KFSW_PARAM_FLAG_READ_ONLY,
		.name = "free_kb",
		.unit = "kB",
		.description = "Filesystem space available, or zero while unmounted",
		.value = &storage_free_kb,
		.default_value = {.u32 = 0U},
		.sample = sample_free_kb,
	},
	{
		.offset = 0x08U,
		.type = KFSW_PARAM_U8,
		.flags = KFSW_PARAM_FLAG_READ_ONLY,
		.name = "used_pct",
		.unit = "%",
		.description = "Filesystem space in use, or zero while unmounted",
		.value = &storage_used_pct,
		.default_value = {.u8 = 0U},
		.sample = sample_used_pct,
	},
	{
		.offset = 0x09U,
		.type = KFSW_PARAM_U8,
		.flags = KFSW_PARAM_FLAG_READ_ONLY,
		.name = "mounted",
		.description = "Whether the volume is mounted and usable",
		.value = &storage_mounted,
		.default_value = {.u8 = 0U},
		.sample = sample_mounted,
	},
};

const struct kfsw_param_definition_set kfsw_storage_param_definitions = {
	.table = KFSW_PARAM_TABLE_STORAGE,
	.name = KFSW_PARAM_TABLE_STORAGE_NAME,
	.definitions = storage_param_definitions,
	.count = ARRAY_SIZE(storage_param_definitions),
};
