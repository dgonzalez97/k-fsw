#include <stdint.h>

#include <zephyr/sys/util.h>

#include <kfsw/platform/watchdog.h>
#include <kfsw/services/parameter.h>

#include "tables.h"

/* Read-only throughout. The timeout is installed in hardware once at
 * initialization, so a value written here would be stored and never applied.
 * The platform also sits below the parameter service and cannot read it back,
 * which is what would be needed to make it take effect on the next boot.
 */
static uint32_t watchdog_timeout_ms;
static uint32_t watchdog_feed_interval_ms;
static uint32_t watchdog_feeds;
static uint32_t watchdog_since_feed_ms;
static uint8_t watchdog_state;
static uint8_t watchdog_device_bound;
static uint8_t watchdog_keepalive_owned;

static void sample_watchdog(void)
{
	struct kfsw_platform_watchdog_info info;

	if (kfsw_platform_watchdog_get_info(&info) != 0) {
		watchdog_device_bound = 0U;
		return;
	}
	watchdog_timeout_ms = info.timeout_ms;
	watchdog_feed_interval_ms = info.feed_interval_ms;
	watchdog_feeds = info.feeds;
	watchdog_since_feed_ms = info.since_feed_ms;
	watchdog_state = info.state;
	watchdog_device_bound = info.device_bound ? 1U : 0U;
	watchdog_keepalive_owned = info.keepalive_owned ? 1U : 0U;
}

static void sample_timeout_ms(void *value)
{
	sample_watchdog();
	*(uint32_t *)value = watchdog_timeout_ms;
}

static void sample_feed_interval_ms(void *value)
{
	sample_watchdog();
	*(uint32_t *)value = watchdog_feed_interval_ms;
}

static void sample_feeds(void *value)
{
	sample_watchdog();
	*(uint32_t *)value = watchdog_feeds;
}

static void sample_since_feed_ms(void *value)
{
	sample_watchdog();
	*(uint32_t *)value = watchdog_since_feed_ms;
}

static void sample_state(void *value)
{
	sample_watchdog();
	*(uint8_t *)value = watchdog_state;
}

static void sample_device_bound(void *value)
{
	sample_watchdog();
	*(uint8_t *)value = watchdog_device_bound;
}

static void sample_keepalive_owned(void *value)
{
	sample_watchdog();
	*(uint8_t *)value = watchdog_keepalive_owned;
}

static const struct kfsw_param_definition watchdog_param_definitions[] = {
	{
		.offset = 0x00U,
		.type = KFSW_PARAM_U32,
		.flags = KFSW_PARAM_FLAG_READ_ONLY,
		.name = "timeout_ms",
		.unit = "ms",
		.description = "Timeout installed in hardware at initialization",
		.value = &watchdog_timeout_ms,
		.default_value = {.u32 = 0U},
		.sample = sample_timeout_ms,
	},
	{
		.offset = 0x04U,
		.type = KFSW_PARAM_U32,
		.flags = KFSW_PARAM_FLAG_READ_ONLY,
		.name = "feed_interval_ms",
		.unit = "ms",
		.description = "Interval the keep-alive feeds at",
		.value = &watchdog_feed_interval_ms,
		.default_value = {.u32 = 0U},
		.sample = sample_feed_interval_ms,
	},
	{
		.offset = 0x08U,
		.type = KFSW_PARAM_U32,
		.flags = KFSW_PARAM_FLAG_READ_ONLY,
		.name = "feeds",
		.description = "Feeds issued since boot; saturates",
		.value = &watchdog_feeds,
		.default_value = {.u32 = 0U},
		.sample = sample_feeds,
	},
	{
		.offset = 0x0cU,
		.type = KFSW_PARAM_U32,
		.flags = KFSW_PARAM_FLAG_READ_ONLY,
		.name = "since_feed_ms",
		.unit = "ms",
		.description = "Milliseconds since the most recent feed",
		.value = &watchdog_since_feed_ms,
		.default_value = {.u32 = 0U},
		.sample = sample_since_feed_ms,
	},
	{
		.offset = 0x10U,
		.type = KFSW_PARAM_U8,
		.flags = KFSW_PARAM_FLAG_READ_ONLY,
		.name = "state",
		.description = "Watchdog lifecycle state",
		.value = &watchdog_state,
		.default_value = {.u8 = 0U},
		.sample = sample_state,
	},
	{
		.offset = 0x11U,
		.type = KFSW_PARAM_U8,
		.flags = KFSW_PARAM_FLAG_READ_ONLY,
		.name = "device_bound",
		.description = "Whether a watchdog device was bound at initialization",
		.value = &watchdog_device_bound,
		.default_value = {.u8 = 0U},
		.sample = sample_device_bound,
	},
	{
		.offset = 0x12U,
		.type = KFSW_PARAM_U8,
		.flags = KFSW_PARAM_FLAG_READ_ONLY,
		.name = "keepalive_owned",
		.description = "Whether the platform keep-alive is the one feeding",
		.value = &watchdog_keepalive_owned,
		.default_value = {.u8 = 0U},
		.sample = sample_keepalive_owned,
	},
};

const struct kfsw_param_definition_set kfsw_watchdog_param_definitions = {
	.table = KFSW_PARAM_TABLE_WATCHDOG,
	.name = KFSW_PARAM_TABLE_WATCHDOG_NAME,
	.definitions = watchdog_param_definitions,
	.count = ARRAY_SIZE(watchdog_param_definitions),
};
