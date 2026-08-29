# Architecture {#architecture}

## Repository layout

K-FSW separates reusable capabilities from product composition.

| Repository | Ownership |
| --- | --- |
| `k-fsw` | Application composition, west manifest, supported targets, developer tools, integration tests, CI, and aggregate documentation |
| `kfsw-platform` | Zephyr-backed time, reset-cause, and storage lifecycle APIs |
| `kfsw-services` | Boot/readiness, logging, parameters, persistence, and FTP services |
| `kfsw-comms` | CSP lifecycle, routing, packet ownership, and UART/KISS APIs |
| `kfsw-modules` | Reusable spacecraft equipment and subsystem clients |

Zephyr, libcsp, libparam, and robot-terminal-runner retain their upstream
ownership. They are inputs to the workspace, not K-FSW API documentation
sources.

## Application composition

The `k-fsw/app/` image selects Kconfig, devicetree, and repository-owned
modules. `main.c` contains the lifecycle order and stays small:

```text
platform storage init + mount
            |
parameter table init + snapshot restore
            |
       CSP init
            |
parameter endpoint registration
            |
       CSP router start
            |
      FTP init + start
            |
     @BOOT and @READY
```

Service behavior belongs in its owning repository. The application controls
initialization order and reports failures; it does not reimplement service
logic.

## Platform

`kfsw-platform` is a small capability layer over Zephyr. It owns
monotonic time, reset cause, and the lifecycle of the configured LittleFS
volume at `/kfsw`. Services use Zephyr filesystem operations only after the
platform reports the volume ready.

Board overlays select the fixed storage partition. The platform never embeds a
raw flash address. A first-boot format is allowed only when the complete
partition still has its erase value; non-erased media that cannot mount is not
silently reformatted.

## Services

`kfsw-services` owns reusable application services and their public interfaces.
The current composition includes boot/readiness markers, runtime-filtered
logging, local and CSP parameters, explicit parameter snapshots, and a
sandboxed file-transfer service.

## Communications

`kfsw-comms` owns the shared CSP instance, router thread, interfaces, routes,
and packet-ownership boundary. A target that enables UART/KISS selects its UART
with devicetree; generic communications code does not identify boards or MCU
peripherals. Services register CSP endpoints after initialization and never
start a second router.

## Dependency management

`k-fsw/west.yml` pins exact commits for the project-owned dependency
repositories and upstream libraries. This makes a local result reproducible in
hosted CI only when every pinned commit is available from its declared remote.

```text
kfsw-services feature commit
              |
              | exact SHA in west.yml
              v
       k-fsw composition PR
```

When a composition change needs an unpublished dependency commit, push and
open the dependency PR first. Then update the `k-fsw` pin and open the
composition PR. Merge the dependency PR before the composition PR. Prefer a
merge commit when preserving the exact tested SHA matters; rebasing or
squashing the dependency changes the commit that west resolves.

See @ref development for the complete branch and review policy.
