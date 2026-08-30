# Architecture {#architecture}

## Architectural intent

K-FSW separates reusable behavior from product composition. A service should
not know which development board was selected, and a UART implementation
should not own the application startup policy. The composition repository
selects capabilities, binds hardware, orders startup, and proves that the
selected repository revisions work together.

That separation supports three practical goals:

1. Run the same current application and service code on Linux native
   simulation and an MCU target.
2. Version reusable platform, service, and communications code independently.
3. Keep optional capabilities optional, especially communications.

The implementation is intentionally smaller than the long-term design notes
that preceded it. There is no current local message bus, command registry,
watchdog service, CAN transport, update service, or production equipment
module. Those concepts must not be used to explain code that does not exist.

## Repository ownership

The current tested composition contains five project-owned Git repositories.

| Repository | Current ownership | Does not own |
| --- | --- | --- |
| `k-fsw` | Executable application, `west.yml`, K-FSW targets, Kconfig composition, devicetree overlays, shell adapters, developer tools, integration/HIL tests, CI, aggregate manual | Reusable service implementations or upstream source |
| `kfsw-platform` | Monotonic time, reset-cause access, and LittleFS storage lifecycle over Zephyr | Mission policy, file-transfer rules, board selection |
| `kfsw-services` | Boot/readiness markers, logging, local parameters, persistence, optional CSP parameter adapter, and FTP client/server | CSP interfaces/routes or raw flash layout |
| `kfsw-comms` | Optional libcsp lifecycle, one router, routes, packet ownership, and UART/KISS adapter | Parameter semantics, FTP protocol, shell parsing |
| `kfsw-modules` | Compile-time-selectable mission-specific device and subsystem modules | Generic platform, service, or communications mechanisms |

`kfsw-modules` is a normal west dependency and Zephyr module with Kconfig and
CMake extension points. Its foundation contributes no runtime source or public
API. Production modules are added only with a real interface, ownership,
implementation, selection contract, and verification.

Upstream dependencies keep their own histories and licenses:

- Zephyr supplies the RTOS, device drivers, build system, shell, native
  simulator, LittleFS integration, and test tools.
- [libcsp](https://github.com/libcsp/libcsp) supplies CSP packet, connection,
  route, interface, KISS, RDP, and buffer-pool implementation.
- [libparam](https://github.com/spaceinventor/libparam) supplies the remote
  parameter wire codec used by the optional adapter.
- [LittleFS](https://github.com/littlefs-project/littlefs) is brought into the
  workspace by Zephyr and mounted through Zephyr's filesystem layer.
- [robot-terminal-runner](https://github.com/dgonzalez97/robot-terminal-runner)
  is a Git submodule used by operator-style Robot tests, not flight code.

## Dependency direction

The application may depend on all four reusable repositories. Services may
depend on the platform layer, and only CSP-backed services depend on the
communications layer. Communications depends on the platform layer and
libcsp. Individual device/subsystem modules declare only the generic
dependencies they actually need. Dependencies do not point back into
`k-fsw/app`.

```text
                              k-fsw/app
                      composition and lifecycle
                      /        |        |        \
                     v         v        v         v
          kfsw-platform  kfsw-services  kfsw-comms  kfsw-modules
                              |    \        |   \
                              |     \-------/    v
                              v   optional use  libcsp
                    optional libparam codec

All project layers use selected Zephyr APIs.
```

The apparent services-to-comms edge is conditional. A build with
`KFSW_PARAM=y`, `KFSW_PARAM_PERSISTENCE=y`, `KFSW_PARAM_CSP=n`, and
`KFSW_CSP=n` compiles the local table and persistence without libcsp. FTP is a
different service with an explicit CSP dependency.

## Composition rather than a monolith

`k-fsw/app/CMakeLists.txt` registers the repository paths as Zephyr modules,
then links the application against their project-owned targets. Each module
uses Kconfig to decide which source files exist in that image. When CSP is
disabled, `kfsw-comms` contributes an interface library so the top-level link
shape stays simple without pulling in libcsp behavior.

The application source stays focused on order and failure reporting:

- initialize and mount storage if configured;
- initialize the parameter table and restore a snapshot if configured;
- initialize CSP and any selected interfaces;
- register the optional parameter CSP endpoints;
- start the one CSP router;
- initialize and start FTP if both its prerequisites and router are ready;
- emit boot/readiness markers; and
- leave ongoing work to service and Zephyr threads.

Service algorithms do not belong in `main.c`. Conversely, a reusable service
does not decide that a particular product must enable it.

## Application startup

The full Linux/NUCLEO composition follows this lifecycle. Dashed choices are
compiled out when their Kconfig symbol is disabled.

```text
Zephyr kernel and configured subsystems start
                    |
              enter main()
                    |
         log "application starting"
                    |
       [storage] init -> mount LittleFS
                    |
       [parameters] validate local table
                    |
       [persistence] restore valid snapshot
                    |
        [CSP] initialize identity/routes/interface
                    |
        [PARAM CSP] register CSP endpoints
                    |
             [CSP] start router
                    |
        [FTP] initialize -> start workers
                    |
       read/clear reset cause; emit @BOOT
                    |
                emit @READY
                    |
         main thread sleeps indefinitely
```

The ordering has concrete dependencies. Persistence needs an initialized local
table and mounted storage. The remote parameter server needs both the local
table and CSP initialization. FTP needs mounted storage and a running CSP
router. Registering service endpoints before starting the router prevents a
valid incoming packet from reaching an unprepared service.

Startup errors are logged but are not currently aggregated into a system
health decision. `main.c` continues through independent stages and calls the
boot service even when an optional stage failed. Consequently, `@READY` means
that the startup sequence completed; it does **not** certify that every
configured service initialized successfully. Tests that require a service
also assert that service's own output.

## Shell readiness and application readiness

The Zephyr shell backend is a configured subsystem with its own execution
context. It is not started by `kfsw_boot_service_start()`, and its prompt may
become visible while application startup messages are still being printed.
Operators and automated tests should use `@READY` as the point after which the
K-FSW startup sequence has finished, then query `storage info`, `csp info`, or
another service-specific status before relying on that service.

```text
Zephyr shell thread:  backend init -------- prompt/commands -------->
K-FSW main thread:    service initialization ---- @BOOT -- @READY -->
```

The shell is a development and operations adapter. Its command handlers call
the same public K-FSW APIs available to other application code. Remote
operations send defined CSP service messages; they do not transmit shell
strings to another node.

## State and ownership

Current mutable state has one clear owner:

| State | Owner | Protection/lifecycle |
| --- | --- | --- |
| Storage mount and backend readiness | `kfsw-platform` | Storage mutex; application initializes and mounts once |
| Runtime log threshold | `kfsw-services` logging | Atomic value; parameter callback updates it |
| Parameter definitions and backing values | Owning application/service/test component | Compile-time definition sets; owner validation and callbacks |
| Aggregated local parameter index | `kfsw-services` PARAM core | Parameter mutex; bounded static index assembled by the executable composition |
| Snapshot workspace and file | Parameter persistence | Persistence mutex plus parameter-table lock |
| CSP identity, routes, interfaces, router | `kfsw-comms` | Initialize once; one statically defined router thread |
| Remote parameter descriptor cache | PARAM CSP adapter | Fixed pool selected by Kconfig |
| FTP client workspace | FTP client | One mutex serializes client operations |
| FTP server connection | FTP server | One static worker; overlapping connection reports busy |

libcsp packet buffers follow libcsp's zero-copy ownership contract. A sender
that hands a packet to a CSP send operation no longer owns it, including on a
later interface failure. This rule is part of the architecture because a
double-free or retry with a transferred buffer crosses every service boundary.

## Time semantics

`kfsw-platform` exposes elapsed monotonic milliseconds and microseconds. They
are suitable for timeouts, durations, and relative scheduling. They are not
UTC, TAI, GNSS time, or synchronized spacecraft time.

The microsecond path uses Zephyr's 64-bit cycle counter when configured and
otherwise converts uptime ticks. The API promises monotonic elapsed time, not
a specific hardware clock resolution. Absolute spacecraft time and clock
correlation are not implemented in the current composition.

## Configuration is part of the product

A supported result is the combination of source, exact dependency revisions,
Kconfig, devicetree, board, and toolchain—not source alone.

```text
west.yml revisions
       +
target -> Zephyr board
       +
prj.conf + board .conf
       +
board DTS + K-FSW overlay
       +
Zephyr 4.4.0 build/toolchain
       |
       v
one K-FSW image and its generated configuration
```

This is why target configurations live with the application composition and
why CI rebuilds from a fresh west workspace. A locally available dependency
branch or edited generated `.config` is not part of a reproducible result.

## Exact dependency pins

At the time of this manual revision, `west.yml` selects:

| Project | Revision |
| --- | --- |
| Zephyr | tag `v4.4.0` |
| `kfsw-platform` | `359c7195b19b27a34c235b7601537ea0c793bf46` |
| `kfsw-services` | `ad7b101f54e7cee0d696c4ee0de68df01bb3f175` |
| `kfsw-comms` | `905a2a776f7ab117f31a0f9bc7608467916017f3` |
| `kfsw-modules` | `a23c2ba21bdc5f3e316877efdef2c9b7b6f801d3` |
| libcsp | `097a039701c85e4ceb98e91f380810662e23878a` |
| libparam | `c296dfb6055a3c360f44dcbbd6ad108e98c76640` |

The manifest is authoritative when these values change. The table is useful
for reviewing this documented composition, not a substitute for
`west manifest --resolve` or `west.yml`.

## Current boundaries and deliberate omissions

The following are not current K-FSW capabilities:

- a local application message bus;
- a generic command dispatcher or remote command service;
- CAN/CFP, SocketCAN, ZMQ, or a production radio interface;
- health monitoring, watchdog policy, or fault-management coordination;
- MCUboot image selection, application update, or rollback control;
- synchronized absolute time; and
- committed spacecraft equipment modules.

Some appear in open issues or older architecture planning material. They may
shape future interfaces, but they must be implemented and verified before
being described as part of the running architecture. See @ref project_status
for the restrained roadmap.
