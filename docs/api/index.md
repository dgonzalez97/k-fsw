# API Reference {#api_reference}

The generated reference covers only K-FSW-owned public headers from:

- `kfsw-platform/include/kfsw/`
- `kfsw-services/include/kfsw/`
- `kfsw-comms/include/kfsw/`

Browse the **API Groups** tab for the conceptual map and **Public Headers** for
the complete declarations, structures, macros, arguments, and return contracts.

## Platform API

- @ref kfsw_platform_time — monotonic elapsed time
- @ref kfsw_platform_storage — LittleFS lifecycle and capacity
- @ref kfsw_platform_reset — reset-cause access

## Services API

- @ref kfsw_services_boot — boot/readiness markers
- @ref kfsw_services_logging — compile-time and runtime log filtering
- @ref kfsw_services_param — local/remote parameters and snapshots
- @ref kfsw_services_ftp — sandboxed CSP/RDP file transfer

## Communications API

- @ref kfsw_comms_csp — CSP lifecycle, interfaces, routes, and ping
- @ref kfsw_comms_uart — UART/KISS status and peer verification

## Scope

The API build explicitly excludes Zephyr, bootloaders, imported modules,
libcsp, libparam, robot-terminal-runner, third-party trees, and generated build
output. Their symbols remain implementation or upstream interfaces and do not
form part of the K-FSW SDK reference.

The generated reference describes the currently shipped interfaces only. It
does not create API placeholders for roadmap functionality.
