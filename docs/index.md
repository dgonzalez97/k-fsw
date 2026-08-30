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
| Application model | Supported targets composed through Kconfig and devicetree |
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

The `csp` commands are available in configurations that enable the
communications module.

## Supported targets

| K-FSW target | Zephyr board | Tested default |
| --- | --- | --- |
| `linux` | `native_sim/native/64` | CSP UART/KISS over a simulated PTY with the current services |
| `nucleo_l496zg` | `nucleo_l496zg` | CSP UART/KISS on USART3 with the current services |

The `frdm_k64f` (`frdm_k64f/mk64f12`) and `rpi_pico_w`
(`rpi_pico/rp2040/w`) profiles are physical shell bring-ups. They verify the
K-FSW prompt plus `status`, `version`, and `help`, but are not yet fully
qualified targets.

Transport selection remains modular. These rows describe the supported
defaults, not every Kconfig combination. Hosted CI builds both targets; board,
ST-LINK, and serial-link checks are explicit local HIL steps.

## Development flow

1. Initialize or update the west workspace.
2. Select a supported target and build the reference application.
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
