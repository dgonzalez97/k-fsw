# K-FSW

**Modular flight software for next space.**

K-FSW composes flight-software applications from separate platform, service,
and communications modules. The current reference integration supports Zephyr
RTOS on native simulation and the NUCLEO-L496ZG.

CSP support is provided by the configurable `kfsw-comms` module. Applications
can enable it when packet routing, UART/KISS, or CSP-based services are needed.

## At a glance

| Area | Current implementation |
| --- | --- |
| Application model | Modules selected through Kconfig profiles |
| Reference platform | Zephyr RTOS |
| Development target | KFSW-Linux on `native_sim/native/64` |
| Embedded target | NUCLEO-L496ZG |
| Optional communications | CSP routing, UART/KISS, and RDP |
| Services | Logging, parameters, persistence, LittleFS storage, and file transfer |
| Workspace | Dependencies pinned by commit in `west.yml` |

## Quick start

From a configured west workspace root:

```bash
./k-fsw/tools/kfsw-linux build
./k-fsw/tools/kfsw-linux run
```

The application starts at the Zephyr shell prompt:

```text
kfsw:~$ status
kfsw:~$ param get test_u32
kfsw:~$ storage info
kfsw:~$ csp ping 2
```

The `csp` commands are available in profiles that enable the communications
module.

## Supported profiles

| Profile | Target | Purpose |
| --- | --- | --- |
| `linux` | `native_sim/native/64` | Primary simulated node |
| `linux_node2` | `native_sim/native/64` | Software integration peer |
| `linux_uart` | `native_sim/native/64` | Physical UART bridge |
| `nucleo_l496zg` | NUCLEO-L496ZG | Base embedded image |
| `nucleo_l496zg_uart` | NUCLEO-L496ZG | Embedded UART/KISS node |

Hosted CI exercises the simulated profiles. Board, ST-LINK, and serial-link
checks are explicit local HIL steps.

## Development flow

1. Initialize or update the west workspace.
2. Select a profile and build the reference application.
3. Run the relevant software checks under `tools/ci/`.
4. Run physical HIL when the change depends on hardware.

See @subpage getting_started "Getting Started" for the complete commands and
@subpage development "Development" for the repository and pull-request
workflow.

## Documentation

- @subpage getting_started "Getting Started" — workspace setup, simulation,
  flashing, and debugging.
- @subpage architecture "Architecture" — composition, ownership, startup, and
  dependency pins.
- @subpage communications "Communications" — optional CSP routing, UART/KISS,
  and RDP-backed transfers.
- @subpage services "Services" — logging, parameters, persistence, storage,
  and file transfer.
- @subpage commands "Command Reference" — shell syntax and operator examples.
- @subpage testing "Testing" — unit, integration, Robot, Valgrind, and HIL
  checks.
- @subpage development "Development" — contribution and multi-repository
  workflow.
- @subpage api_reference "API Reference" — public headers, data types, and API
  groups.
