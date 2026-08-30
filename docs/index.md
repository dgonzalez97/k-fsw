# K-FSW {#k_fsw_manual}

K Flight Software (K-FSW) is a small, modular flight-software framework. It
provides a reusable application structure, platform capabilities, operational
services, communications, developer tooling, and test infrastructure without
assuming one spacecraft or one processor family.

K-FSW is not defined by a particular RTOS or network protocol. Zephyr is the
current reference RTOS and hardware-integration layer. Cubesat Space Protocol
(CSP) is an optional communications capability for compositions that need
packet routing or remote services. The local parameter service, logging, time,
and shell-only targets do not require CSP.

The project is deliberately at an engineering-development stage. Its current
software gates and physical bench results are useful evidence, but they are not
flight qualification. @ref project_status records what has actually been
implemented and tested.

## How the pieces fit

```text
                         k-fsw
        application composition, targets, tests, tools, docs
                            |
             +--------------+--------------+
             |              |              |
      kfsw-services   kfsw-platform   kfsw-comms
       reusable app      time and       optional CSP,
         services         storage        routing, KISS
             |              |              |
             +--------------+--------------+
                            |
                         Zephyr
                kernel, devices, drivers, build
                            |
          +-----------------+------------------+
          |                 |                  |
       Linux          NUCLEO-L496ZG       shell bring-ups
    native_sim        STM32L496 target     FRDM / Pico W
```

The four project-owned repositories have independent Git histories. The
`k-fsw` manifest pins the exact commits that form one tested composition. A
developer normally changes reusable behavior in its owning repository and
then updates the composition pin in a separate `k-fsw` pull request.

## Current reference composition

The full reference application is configured for KFSW-Linux and
NUCLEO-L496ZG. It currently includes:

- monotonic time and reset-cause access;
- runtime-filtered logging;
- LittleFS storage mounted at `/kfsw`;
- typed local parameters with explicit, CRC-protected snapshots;
- an optional libcsp router and UART/KISS interface;
- an optional CSP parameter adapter; and
- a K-FSW-specific file-transfer service over CSP/RDP.

FRDM-K64F and Raspberry Pi Pico W profiles intentionally select a much smaller
composition: boot markers, the debug shell, and the root `status`, `version`,
and `help` paths. Their successful shell bring-up is not evidence that storage,
CSP, parameters, or FTP work on those boards.

## Reading the manual

- @subpage getting_started "Getting Started" sets up the workspace and covers
  the normal build, run, flash, serial, and debug loop.
- @subpage architecture "Architecture" explains repository ownership,
  dependency direction, configuration, and application startup.
- @subpage zephyr_integration "Zephyr Integration" introduces the Zephyr
  concepts a K-FSW developer uses day to day.
- @subpage communications "CSP and Communications" explains CSP before
  describing the K-FSW router, KISS link, and RDP usage.
- @subpage services "Services and Storage" covers logging, parameters,
  persistence, LittleFS, and K-FSW FTP.
- @subpage targets "Boards and Targets" separates Zephyr boards, K-FSW target
  profiles, and their qualification levels.
- @subpage ground "Ground Composition" explains k-ground identity, local
  ground-node operation, and the pending UHF/Holybro HIL boundary.
- @subpage commands "Shell and Command Reference" explains the interactive
  shell and every current project command.
- @subpage testing "Testing, HIL, and CI" describes what each test layer does
  and does not prove.
- @subpage development "Development Workflow" covers Git, west-managed
  dependency changes, VS Code, and documentation work.
- @subpage project_status "Project Status and Roadmap" summarizes verified
  status and broad next steps.
- @subpage api_reference "API Reference" links to generated declarations for
  project-owned public headers. It is part of the HTML site, not the printable
  engineering manual.

## First run

From a configured west workspace root:

```bash
./k-fsw/tools/kfsw-linux build
./k-fsw/tools/kfsw-linux run
```

Wait for the application readiness marker before exercising services:

```text
@BOOT sw=kfsw-dev board=native_sim/native/64 ...
@READY uptime_ms=...

kfsw:~$ status
kfsw:~$ param get test_u32
kfsw:~$ storage info
kfsw:~$ csp info
```

`kfsw:~$` is the Zephyr shell prompt configured by K-FSW. It is visual
identity, not a `kfsw` command prefix.

## Documentation boundary

The manual explains concepts, rationale, workflows, and the verified project
state. Exact function signatures and return contracts remain in the generated
@ref api_reference. Zephyr, libcsp, libparam, LittleFS, and Robot Framework
retain their own upstream documentation; links throughout this manual point to
those sources rather than reproducing their manuals.
