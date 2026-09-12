# Architecture {#architecture}

[TOC]

## Repository ownership

`k-fsw` builds the application from four reusable repositories.
It selects features, binds hardware, and orders startup.

| Repository | Owns |
| --- | --- |
| `k-fsw` | Application startup, target configuration, shell adapters, tools, integration tests, docs, and `west.yml` |
| `kfsw-platform` | Time, reset cause, watchdog, retained reset notes, and storage over Zephyr |
| `kfsw-services` | Boot, logging, parameters, persistence, files, commands, events, health, housekeeping, and firmware update |
| `kfsw-comms` | libcsp setup, one router, routes, UART/KISS and CAN interfaces |
| `kfsw-modules` | Device and subsystem clients, including UHF, button/LED, and temperature examples |

Reusable code belongs with its owner. Shell handlers parse arguments, call a
public API, and print the result.

Zephyr supplies the kernel, drivers, filesystem integration, shell, and build
tools. [libcsp](https://github.com/libcsp/libcsp) supplies the network stack;
[libparam](https://github.com/spaceinventor/libparam) supplies the remote
parameter codec. Their source and licences stay upstream.

## Dependencies

The application can use all four repositories. Services use platform APIs
and, when needed, communications. Communications uses libcsp and platform
support. Device modules declare the dependencies they need.
None of these layers depends on `k-fsw/app`.

Local parameters and persistence can run with CSP disabled:

```text
CONFIG_KFSW_PARAM=y
CONFIG_KFSW_PARAM_PERSISTENCE=y
CONFIG_KFSW_STORAGE=y
CONFIG_KFSW_PARAM_CSP=n
CONFIG_KFSW_CSP=n
```

FTP needs CSP and storage. The remote parameter adapter needs CSP and the
local parameter core. Kconfig checks these dependencies.

## Configuration

| Input | Sets |
| --- | --- |
| `west.yml` | Exact dependency revisions |
| `config/targets/<target>.env` | Zephyr board and local tool defaults |
| `app/prj.conf`, board `.conf`, profile `.conf` | Enabled software and defaults |
| Board devicetree and overlays | Devices, wiring, and flash partitions |
| `app/src/main.c` | Initialization order and error handling |

Use `west manifest --resolve` to inspect revisions. See @ref targets for
profiles and @ref zephyr_integration for generated configuration.
Changing RTOS requires adapting the Zephyr APIs used by the selected code;
moving to another Zephyr board usually changes configuration and device bindings.

## Device modules

A module owns its device state, public API, and parameter definitions.
The application selects it and passes its definition set to PARAM.

- `radio-uhf` reports the selected Holybro SiK identity and expected serial
  configuration. `kfsw-comms` owns its UART/KISS data path.
- `boton_test` owns debouncing, press count, last-press time, and LED state.
  Table 67 exposes its values. Use `kfsw_boton_test_get_status()` for a
  consistent snapshot of all five fields.
- `temperature-sensor-example` reads a configured sensor and publishes
  samples and status in table 51.

Hardware selection belongs in devicetree. A module should not select a board
by name or duplicate a router, transport, or service.

## Application startup

`main.c` runs the following stages when their features are enabled:

1. Report the selected UHF identity.
2. Initialize and mount storage.
3. Register parameter tables, restore saved values, and apply boot settings.
4. Initialize device modules and file based operations.
5. Register command definitions.
6. Initialize CSP and interfaces, bind PARAM endpoints, and start the router.
7. Start FTP, direct firmware upload, command, and housekeeping CSP servers.
8. Initialize housekeeping and its collection worker.
9. Initialize the watchdog and start health supervision.
10. Apply shell settings, enable supply monitoring, and set native host time.
11. Emit `@BOOT` and `@READY`; enter the application health-report loop.

Persistence needs storage and registered parameter tables. Network services
need initialized CSP; FTP and the other worker servers start after the router.

Startup logs errors and continues through independent stages. **`@READY`
means startup finished.** Check `storage info`, `csp info`, and the relevant
service status before using a service. The shell prompt can appear earlier.

## State and concurrency

| State | Owner and protection |
| --- | --- |
| Storage readiness | Platform storage mutex |
| Parameter values | Component-owned storage; PARAM validation and callbacks |
| Parameter index | PARAM mutex and bounded static index |
| Persistence workspace | Persistence mutex and PARAM locking |
| Button and LED snapshot | Module mutex |
| CSP routes and interfaces | Communications; initialized once |
| UART/KISS receive state | One context per interface |
| FTP client | One static workspace, serialized by a mutex |
| FTP server | One worker; overlapping requests return busy |
| Events | Bounded RAM ring protected by a short spinlock |

Parameter reads of individual fields do not provide a consistent multi-field
snapshot. Use the owner's snapshot API when fields must agree.

Follow libcsp packet ownership: after handing a packet to a send operation,
do not reuse or free it, including after a later interface failure.

## Time

`kfsw_time_monotonic_ms()` and `kfsw_time_monotonic_us()` return elapsed
time for deadlines and durations. Resolution depends on the selected Zephyr
clock source.

CSP clock APIs provide a separate RTC time for timestamps. Setting that clock
does not change monotonic deadlines. An RTC value alone does not establish
clock accuracy or synchronization; those need their own measurements.

## Further reading

- @ref services — service behaviour, storage, and parameter formats.
- @ref communications — packet flow, interfaces, and routes.
- @ref development — changing a dependency and updating its manifest pin.
- @ref project_status — implementation limits and recorded tests.
