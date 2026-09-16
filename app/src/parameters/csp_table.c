#include <errno.h>
#include <stdint.h>

#include <zephyr/sys/util.h>

#include <kfsw/comms/csp.h>
#define KFSW_LOG_MODULE KFSW_LOG_MODULE_CSP
#include <kfsw/services/log.h>
#include <kfsw/services/parameter.h>

#include "tables.h"

/* Interface counters, summed over all interfaces. */
struct csp_totals {
	uint32_t tx_packets;
	uint32_t rx_packets;
	uint32_t tx_errors;
	uint32_t rx_errors;
	uint32_t dropped;
	uint8_t interfaces;
};

static uint32_t csp_tx_packets;
static uint32_t csp_rx_packets;
static uint32_t csp_tx_errors;
static uint32_t csp_rx_errors;
static uint32_t csp_dropped;
static uint8_t csp_interfaces;
static uint8_t csp_router_running;

/* Read-only: libcsp's active CIDR table cannot be replaced while routing. */
static char csp_route_table[KFSW_CSP_ROUTE_TABLE_MAX_LENGTH + 1U] = CONFIG_KFSW_CSP_ROUTE_TABLE;

static bool accumulate_interface(const struct kfsw_csp_interface_info *info, void *context)
{
	struct csp_totals *totals = context;

	totals->tx_packets += info->tx_packets;
	totals->rx_packets += info->rx_packets;
	totals->tx_errors += info->tx_errors;
	totals->rx_errors += info->rx_errors;
	totals->dropped += info->dropped_packets;
	if (totals->interfaces < UINT8_MAX) {
		totals->interfaces++;
	}
	return true;
}

static void sample_totals(void)
{
	struct csp_totals totals = {0};

	kfsw_csp_visit_interfaces(accumulate_interface, &totals);
	csp_tx_packets = totals.tx_packets;
	csp_rx_packets = totals.rx_packets;
	csp_tx_errors = totals.tx_errors;
	csp_rx_errors = totals.rx_errors;
	csp_dropped = totals.dropped;
	csp_interfaces = totals.interfaces;
}

static void sample_tx_packets(void *value)
{
	sample_totals();
	*(uint32_t *)value = csp_tx_packets;
}

static void sample_rx_packets(void *value)
{
	sample_totals();
	*(uint32_t *)value = csp_rx_packets;
}

static void sample_tx_errors(void *value)
{
	sample_totals();
	*(uint32_t *)value = csp_tx_errors;
}

static void sample_rx_errors(void *value)
{
	sample_totals();
	*(uint32_t *)value = csp_rx_errors;
}

static void sample_dropped(void *value)
{
	sample_totals();
	*(uint32_t *)value = csp_dropped;
}

static void sample_interfaces(void *value)
{
	sample_totals();
	*(uint8_t *)value = csp_interfaces;
}

/* libcsp's own error counters, sampled together. */
static uint8_t csp_buffer_out;
static uint8_t csp_conn_out;
static uint8_t csp_conn_ovf;
static uint8_t csp_conn_noroute;
static uint8_t csp_invalid_reply;
static uint8_t csp_last_error;
static uint8_t csp_last_can_error;

static void sample_counters(void)
{
	struct kfsw_csp_counters counters;

	kfsw_csp_get_counters(&counters);
	csp_buffer_out = counters.buffer_out;
	csp_conn_out = counters.conn_out;
	csp_conn_ovf = counters.conn_overflow;
	csp_conn_noroute = counters.conn_noroute;
	csp_invalid_reply = counters.invalid_reply;
	csp_last_error = counters.last_error;
	csp_last_can_error = counters.last_can_error;
}

static void sample_buffer_out(void *value)
{
	sample_counters();
	*(uint8_t *)value = csp_buffer_out;
}

static void sample_conn_out(void *value)
{
	sample_counters();
	*(uint8_t *)value = csp_conn_out;
}

static void sample_conn_ovf(void *value)
{
	sample_counters();
	*(uint8_t *)value = csp_conn_ovf;
}

static void sample_conn_noroute(void *value)
{
	sample_counters();
	*(uint8_t *)value = csp_conn_noroute;
}

static void sample_invalid_reply(void *value)
{
	sample_counters();
	*(uint8_t *)value = csp_invalid_reply;
}

static void sample_last_error(void *value)
{
	sample_counters();
	*(uint8_t *)value = csp_last_error;
}

static void sample_last_can_error(void *value)
{
	sample_counters();
	*(uint8_t *)value = csp_last_can_error;
}

static void sample_router_running(void *value)
{
	struct kfsw_csp_info info;

	kfsw_csp_get_info(&info);
	*(uint8_t *)value = info.router_running ? 1U : 0U;
}

static const struct kfsw_param_definition csp_param_definitions[] = {
	{
		.offset = 0x00U,
		.type = KFSW_PARAM_U32,
		.flags = KFSW_PARAM_FLAG_READ_ONLY,
		.name = "tx_packets",
		.description = "Packets sent across every interface",
		.value = &csp_tx_packets,
		.default_value = {.u32 = 0U},
		.sample = sample_tx_packets,
	},
	{
		.offset = 0x04U,
		.type = KFSW_PARAM_U32,
		.flags = KFSW_PARAM_FLAG_READ_ONLY,
		.name = "rx_packets",
		.description = "Packets received across every interface",
		.value = &csp_rx_packets,
		.default_value = {.u32 = 0U},
		.sample = sample_rx_packets,
	},
	{
		.offset = 0x08U,
		.type = KFSW_PARAM_U32,
		.flags = KFSW_PARAM_FLAG_READ_ONLY,
		.name = "tx_errors",
		.description = "Transmit errors across every interface",
		.value = &csp_tx_errors,
		.default_value = {.u32 = 0U},
		.sample = sample_tx_errors,
	},
	{
		.offset = 0x0cU,
		.type = KFSW_PARAM_U32,
		.flags = KFSW_PARAM_FLAG_READ_ONLY,
		.name = "rx_errors",
		.description = "Receive errors across every interface",
		.value = &csp_rx_errors,
		.default_value = {.u32 = 0U},
		.sample = sample_rx_errors,
	},
	{
		.offset = 0x10U,
		.type = KFSW_PARAM_U32,
		.flags = KFSW_PARAM_FLAG_READ_ONLY,
		.name = "dropped",
		.description = "Packets dropped across every interface",
		.value = &csp_dropped,
		.default_value = {.u32 = 0U},
		.sample = sample_dropped,
	},
	{
		.offset = 0x14U,
		.type = KFSW_PARAM_U8,
		.flags = KFSW_PARAM_FLAG_READ_ONLY,
		.name = "interfaces",
		.description = "Interfaces registered with the router",
		.value = &csp_interfaces,
		.default_value = {.u8 = 0U},
		.sample = sample_interfaces,
	},
	{
		.offset = 0x16U,
		.type = KFSW_PARAM_U8,
		.flags = KFSW_PARAM_FLAG_READ_ONLY,
		.name = "buffer_out",
		.description = "Times no packet buffer was free",
		.value = &csp_buffer_out,
		.default_value = {.u8 = 0U},
		.sample = sample_buffer_out,
	},
	{
		.offset = 0x17U,
		.type = KFSW_PARAM_U8,
		.flags = KFSW_PARAM_FLAG_READ_ONLY,
		.name = "conn_out",
		.description = "Times no connection slot was free",
		.value = &csp_conn_out,
		.default_value = {.u8 = 0U},
		.sample = sample_conn_out,
	},
	{
		.offset = 0x18U,
		.type = KFSW_PARAM_U8,
		.flags = KFSW_PARAM_FLAG_READ_ONLY,
		.name = "conn_ovf",
		.description = "Packets dropped by a full connection queue",
		.value = &csp_conn_ovf,
		.default_value = {.u8 = 0U},
		.sample = sample_conn_ovf,
	},
	{
		.offset = 0x19U,
		.type = KFSW_PARAM_U8,
		.flags = KFSW_PARAM_FLAG_READ_ONLY,
		.name = "conn_noroute",
		.description = "Packets dropped with no route to the destination",
		.value = &csp_conn_noroute,
		.default_value = {.u8 = 0U},
		.sample = sample_conn_noroute,
	},
	{
		.offset = 0x1aU,
		.type = KFSW_PARAM_U8,
		.flags = KFSW_PARAM_FLAG_READ_ONLY,
		.name = "invalid_reply",
		.description = "Replies that matched no open connection",
		.value = &csp_invalid_reply,
		.default_value = {.u8 = 0U},
		.sample = sample_invalid_reply,
	},
	{
		.offset = 0x1bU,
		.type = KFSW_PARAM_U8,
		.flags = KFSW_PARAM_FLAG_READ_ONLY,
		.name = "last_error",
		.description = "Code of the last libcsp error",
		.value = &csp_last_error,
		.default_value = {.u8 = 0U},
		.sample = sample_last_error,
	},
	{
		.offset = 0x1cU,
		.type = KFSW_PARAM_U8,
		.flags = KFSW_PARAM_FLAG_READ_ONLY,
		.name = "last_can_error",
		.description = "Code of the last CAN framing error",
		.value = &csp_last_can_error,
		.default_value = {.u8 = 0U},
		.sample = sample_last_can_error,
	},
	{
		.offset = 0x20U,
		.type = KFSW_PARAM_STRING,
		.capacity = KFSW_CSP_ROUTE_TABLE_MAX_LENGTH + 1U,
		.flags = KFSW_PARAM_FLAG_READ_ONLY,
		.name = "route_table",
		.description = "Compiled routes in libcsp CIDR syntax",
		.value = csp_route_table,
		.default_text = CONFIG_KFSW_CSP_ROUTE_TABLE,
	},
	{
		.offset = 0x15U,
		.type = KFSW_PARAM_U8,
		.flags = KFSW_PARAM_FLAG_READ_ONLY,
		.name = "router_running",
		.description = "Whether the CSP router thread is running",
		.value = &csp_router_running,
		.default_value = {.u8 = 0U},
		.sample = sample_router_running,
	},
};

const struct kfsw_param_definition_set kfsw_csp_param_definitions = {
	.table = KFSW_PARAM_TABLE_CSP,
	.name = KFSW_PARAM_TABLE_CSP_NAME,
	.definitions = csp_param_definitions,
	.count = ARRAY_SIZE(csp_param_definitions),
};
