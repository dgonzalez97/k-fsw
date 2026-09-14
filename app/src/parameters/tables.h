#ifndef KFSW_APP_PARAMETERS_TABLES_H
#define KFSW_APP_PARAMETERS_TABLES_H

#include <stdbool.h>

#include <kfsw/services/parameter.h>

/**
 * @file
 * Core parameter tables, IDs 1 to 24. They are in the application because the
 * platform and comms layers can't depend on the parameter service.
 */

/** Table 1: identity and the composition switches that decide reachability. */
#define KFSW_PARAM_TABLE_BOARD 1U
#define KFSW_PARAM_TABLE_BOARD_NAME "board"

/** Table 2: mission configuration the application itself applies. */
#define KFSW_PARAM_TABLE_SYSTEM 2U
#define KFSW_PARAM_TABLE_SYSTEM_NAME "system"

/** Table 3: sampled read-only housekeeping. */
#define KFSW_PARAM_TABLE_TELEMETRY 3U
#define KFSW_PARAM_TABLE_TELEMETRY_NAME "telemetry"

/** Table 4: CSP router and interface totals. */
#define KFSW_PARAM_TABLE_CSP 4U
#define KFSW_PARAM_TABLE_CSP_NAME "csp"

/** Table 5: filesystem capacity and state. */
#define KFSW_PARAM_TABLE_STORAGE 5U
#define KFSW_PARAM_TABLE_STORAGE_NAME "storage"

/** Table 6: watchdog configuration and feeding. */
#define KFSW_PARAM_TABLE_WATCHDOG 6U
#define KFSW_PARAM_TABLE_WATCHDOG_NAME "watchdog"

extern const struct kfsw_param_definition_set kfsw_board_param_definitions;
extern const struct kfsw_param_definition_set kfsw_system_param_definitions;
extern const struct kfsw_param_definition_set kfsw_telemetry_param_definitions;
#if CONFIG_KFSW_CSP
extern const struct kfsw_param_definition_set kfsw_csp_param_definitions;
#endif
#if CONFIG_KFSW_STORAGE
extern const struct kfsw_param_definition_set kfsw_storage_param_definitions;
#endif
#if CONFIG_KFSW_WATCHDOG
extern const struct kfsw_param_definition_set kfsw_watchdog_param_definitions;
#endif

/** Boot delay in milliseconds read from table 2, applied before services start. */
uint16_t kfsw_system_boot_delay_ms(void);

/** Application health reporting period in milliseconds, read from table 2. */
uint16_t kfsw_system_app_report_ms(void);

/** Longest reboot PIN, plus a terminator. */
#define KFSW_SYSTEM_REBOOT_PIN_SIZE 9U

/**
 * @brief Whether a supplied PIN matches the one this node accepts.
 *
 * The PIN is sent in clear text; it only protects against a wrong node number.
 * The comparison takes the same time whether it matches or not.
 */
bool kfsw_system_reboot_pin_matches(const char *pin);

#endif
