# API Reference {#api_reference}

The generated reference covers only K-FSW-owned public headers from:

- `kfsw-platform/include/kfsw/`
- `kfsw-services/include/kfsw/`
- `kfsw-comms/include/kfsw/`
- `kfsw-modules/radio-uhf/include/kfsw/`
- `kfsw-modules/boton-test/include/kfsw/`

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

The API build explicitly excludes Zephyr, bootloaders, upstream/imported
modules, libcsp, libparam, robot-terminal-runner, third-party trees, and
generated build output. The project-owned `kfsw-modules` public headers listed
above are intentionally included; internal Holybro descriptors and private
button state/test interfaces remain excluded.

The generated reference describes interfaces selected in the checked-out
source tree; it does not create API placeholders for roadmap functionality.
