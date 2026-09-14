# K-FSW - Modular flight software on Zephyr, for small satellites {#k_fsw_manual}

K-FSW gives spacecraft components the common services a mission needs:
a console, ground links, parameters, files, logs, events, commands,
housekeeping, and firmware updates. Enable what an OBC, radio, ADCS, EPS,
or payload needs, then write the mission-specific code.

## Start here

| Task | Page |
| --- | --- |
| Build and run a Linux node | @ref getting_started |
| Flash a board and check its interfaces | @ref targets |
| Connect a ground node or record telemetry in Yamcs | @ref ground |

## Reference

| Topic | Page |
| --- | --- |
| Shell syntax and examples | @ref commands |
| Parameters, persistence, logging, files, commands, events | @ref services |
| CSP addresses, routes, UART/KISS, and CAN | @ref communications |
| Upload, boot, confirm, and roll back an image | @ref firmware_update |
| Public C headers and functions | @ref api_reference |

## Repository layout

![K-FSW repositories](media/layout.svg)

`k-fsw` selects the modules and services, binds hardware, and orders startup.
The other four repositories hold reusable code. `west.yml` pins the
revisions used together.

## Zephyr

Zephyr provides the RTOS, device drivers, board support, Kconfig, devicetree,
and build tools. Its native simulator runs the application on Linux, so
service and link tests can run before hardware is available.

K-FSW is not tied to one MCU vendor or board. A new Zephyr target supplies
its hardware description and configuration. Modules and services can also
be reused in another RTOS port, but their Zephyr kernel, driver, and
filesystem calls need adapting. See @ref zephyr_integration.

CSP is optional. Local parameters, persistence, logging and the shell work
without it.

## First run

From a configured west workspace:

```bash
./k-fsw/tools/kfsw-linux build
./k-fsw/tools/kfsw-linux run
```

After `@READY`, try `status`, `param tables`, `storage info`, and `csp info`.
See @ref getting_started for workspace setup.

## Development

- @ref architecture covers repositories, startup and configuration.
- @ref development covers branches, dependency changes and pull requests.
- @ref testing covers software checks and hardware tests.
- @ref project_status lists bench results and known limits.

Linux and NUCLEO-L496ZG are the reference targets. FRDM-K64F and Pico W run
shell-only profiles.
