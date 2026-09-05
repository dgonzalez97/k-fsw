#include <errno.h>
#include <stdint.h>

#include <zephyr/sys/util.h>

#include <kfsw/comms/csp.h>
/* Attributes this file's messages, so its level can be raised alone. */
#define KFSW_LOG_MODULE KFSW_LOG_MODULE_CSP
#include <kfsw/services/log.h>
#include <kfsw/services/parameter.h>

#include "tables.h"

/* Interface counters, summed across every registered interface. A per-interface
 * table would need one table per composition; the totals answer the question an
 * operator actually asks first, which is whether anything is moving at all.
 */
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

/* Live rather than stored. A route table is the one setting that can put a node
 * out of reach, so a wrong one must not survive a reboot: the compiled table
 * comes back on the next boot and the mistake costs a pass, not the node.
 */
static char csp_route_table[KFSW_CSP_ROUTE_TABLE_MAX_LENGTH + 1U] = CONFIG_KFSW_CSP_ROUTE_TABLE;

/* Empty is accepted and means "leave the composed routes alone". A composition
 * with one link derives its route implicitly and has no compiled table, so
 * empty is the honest starting value; refusing it would stop the whole
 * parameter table from registering on exactly the compositions that work. It
 * is also the safe reading of an empty write, which would otherwise be a way
 * to take a node off the network by typing nothing.
 */
static int validate_route_table(const char *text)
{
	size_t entries = 0U;
	int result;

	if (text[0] == '\0') {
		return 0;
	}

	result = kfsw_csp_route_table_check(text, &entries);
	if (result == -ENETDOWN) {
		/* Registration happens before CSP starts, so the interfaces a
		 * compiled table names do not exist yet and nothing here can
		 * judge it. Accepting is the only option that leaves the table
		 * registered, and it costs nothing: the same table is checked
		 * again when CSP applies it, which is where a bad compiled
		 * value is reported. Any write an operator makes arrives after
		 * CSP is up and gets the full check.
		 */
		return 0;
	}
	if (result != 0) {
		return -EINVAL;
	}
	return (entries == 0U) ? -EINVAL : 0;
}

static void apply_route_table(const char *text)
{
	if (text[0] == '\0') {
		kfsw_log_info("CSP: empty route table ignored; composed routes kept");
		return;
	}
	if (kfsw_csp_route_table_apply(text) != 0) {
		/* Validation already accepted it, so a failure here means the
		 * router refused what it had agreed was well formed. The old
		 * routes are gone either way, and saying so is the only way an
		 * operator learns why the node went quiet. */
		kfsw_log_error("CSP: the router refused a validated route table");
	} else {
		kfsw_log_info("CSP: route table replaced with '%s'", text);
	}
}

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
		.offset = 0x20U,
		.type = KFSW_PARAM_STRING,
		.capacity = KFSW_CSP_ROUTE_TABLE_MAX_LENGTH + 1U,
		.flags = KFSW_PARAM_FLAG_CONFIGURATION,
		.name = "route_table",
		.description = "Static routes in libcsp CIDR syntax; applied immediately",
		.value = csp_route_table,
		.default_text = CONFIG_KFSW_CSP_ROUTE_TABLE,
		.validate_text = validate_route_table,
		.changed_text = apply_route_table,
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
