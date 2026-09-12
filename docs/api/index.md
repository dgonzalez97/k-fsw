# API Reference {#api_reference}

The generated reference covers only K-FSW-owned public headers from:

- `kfsw-platform/include/kfsw/`
- `kfsw-services/include/kfsw/`
- `kfsw-comms/include/kfsw/`
- `kfsw-modules/radio-uhf/include/kfsw/`
- `kfsw-modules/boton-test/include/kfsw/`
- `kfsw-modules/temperature-sensor-example/include/kfsw/`

Under **API Reference**, use **API Groups** to browse by component or
**Headers** for declarations, types, arguments, and return values.

## Platform API

- @ref kfsw_platform_time — monotonic elapsed time
- @ref kfsw_platform_storage — LittleFS lifecycle and capacity
- @ref kfsw_platform_reset — reset-cause access

## Services API

- @ref kfsw_services_boot — boot/readiness markers
- @ref kfsw_services_logging — compile-time and runtime log filtering
- @ref kfsw_services_param — local/remote parameters and snapshots
- @ref kfsw_services_ftp — sandboxed CSP/RDP file transfer
- @ref kfsw_services_command — one path for invoking an operation, local or remote
- @ref kfsw_services_event — bounded numeric record of what a node has done

## Communications API

- @ref kfsw_comms_csp — CSP lifecycle, interfaces, routes, and ping
- @ref kfsw_comms_uart — UART/KISS status and peer verification

## Reusable module APIs

- `kfsw/modules/radio_uhf.h` — selected UHF implementation identity,
  build-time serial expectations, and bounded hardware/link status
- @ref kfsw_modules_boton_test — initialization, coherent press status, and
  module-owned live parameter definitions

## Scope

The reference covers the checked-out public headers. Private implementation,
upstream libraries, and build output are excluded.
