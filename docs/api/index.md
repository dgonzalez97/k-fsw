# API Reference {#api_reference}

The reference covers the K-FSW public headers in:

- `kfsw-platform/include/kfsw/`
- `kfsw-services/include/kfsw/`
- `kfsw-comms/include/kfsw/`
- `kfsw-modules/radio-uhf/include/kfsw/`
- `kfsw-modules/boton-test/include/kfsw/`
- `kfsw-modules/temperature-sensor-example/include/kfsw/`

Under **API Reference**, use **API Groups** to browse by component or
**Headers** for declarations, types, arguments, and return values.

## Platform API

- @ref kfsw_platform_time for monotonic time
- @ref kfsw_platform_storage for the LittleFS mount and capacity
- @ref kfsw_platform_reset for the reset cause
- @ref kfsw_platform_watchdog for the watchdog

## Services API

- @ref kfsw_services_boot for the boot and readiness markers
- @ref kfsw_services_logging for logging and log levels
- @ref kfsw_services_param for local and remote parameters and snapshots
- @ref kfsw_services_ftp for file transfer over CSP/RDP
- @ref kfsw_services_command for local and remote commands
- @ref kfsw_services_event for the event record
- @ref kfsw_services_health for health monitoring
- @ref kfsw_services_fwu for firmware update
- @ref kfsw_services_fwu_lite for FWU lite uploads
- @ref kfsw_services_fbo for file based operations

## Communications API

- @ref kfsw_comms_csp for CSP setup, interfaces, routes and ping
- @ref kfsw_comms_uart for UART/KISS status and the peer test

## Reusable module APIs

- `kfsw/modules/radio_uhf.h`: radio identity, expected serial settings, link
  status and encryption
- @ref kfsw_modules_boton_test for the button, LEDs and parameters
- @ref kfsw_modules_temp_example for the temperature example

## Scope

Private sources, upstream libraries and build output are not included.
