# K-FSW {#k_fsw_manual}

K Flight Software (K-FSW) is a small, modular flight-software framework. It
gives a spacecraft the things every mission needs before it can do anything
mission-specific: a console, a link to the ground, named settings you can read
and change from that link, files, events, commands, a watchdog, and a way to
replace the running image.

Nothing here is tied to one RTOS or one network protocol. Zephyr is the current
reference, and Cubesat Space Protocol (CSP) is optional — a composition that
only needs local parameters, logging, time and a shell builds without it.

This is engineering-development software. The test gates and bench results are
real evidence, and they are not flight qualification. @ref project_status is
the honest inventory: what exists, what has been tested in software, and what
has been read off a board.

## How the pieces fit

```text
                         k-fsw
        application composition, targets, tests, tools, docs
                            |
       +-------------+-------------+-------------+-------------+
       |             |             |             |
kfsw-services  kfsw-platform  kfsw-comms   kfsw-modules
 reusable app    time and     optional CSP,  selected device/
   services       storage      routing, KISS subsystem modules
       |             |             |             |
       +-------------+-------------+-------------+
                            |
                          Zephyr
                kernel, devices, drivers, build
                            |
          +-----------------+------------------+
          |                 |                  |
       Linux          NUCLEO-L496ZG       shell bring-ups
    native_sim        STM32L496 target     FRDM / Pico W
```

The five project-owned repositories have independent Git histories. The
`k-fsw` manifest pins the exact commits that form one tested composition. A
developer normally changes reusable behavior in its owning repository and
then updates the composition pin in a separate `k-fsw` pull request.

## Current reference composition

The full reference application runs on KFSW-Linux and NUCLEO-L496ZG:

- monotonic time, a latched reset cause, and a watchdog fed by a health policy
  rather than a timer;
- logging with a level per module, so raising CSP to debug does not drown the
  output in everything else;
- LittleFS storage mounted at `/kfsw`;
- 100 parameters across 16 tables, addressed by table and offset, carrying
  scalars, strings and byte arrays, with explicit CRC-protected snapshots;
- a libcsp router with named UART/KISS interfaces, and remote parameter access
  over it;
- file transfer over CSP with RDP;
- a command service with typed arguments and results, and a bounded event
  record with stable identifiers; and
- firmware update, streamed into the secondary slot and handed to MCUboot.

The UHF ground/NUCLEO test compositions additionally select the reusable
`radio-uhf` module with Holybro SiK identity and bounded status. They continue
to use the same `kfsw-comms` CSP/KISS/UART path.

An opt-in NUCLEO profile composes the `boton_test` module, which exists as a
worked example of owning hardware: the board USER button mapped through
devicetree, presses debounced on the system workqueue, a typed snapshot whose
five fields agree with each other, and the same values readable from the ground
through table 67. It has been accepted on hardware apart from per-gesture
attribution.

FRDM-K64F and Raspberry Pi Pico W run a much smaller composition on purpose:
boot markers, the debug shell, and `status`, `version` and `help`. A shell that
comes up on those boards says nothing about storage, CSP, parameters or files,
none of which are built into them.

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
- @subpage ground "Ground Composition" explains configured ground roles,
  local ground-node operation, and the separate UHF/Holybro HIL boundary.
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
@BOOT sw=7c592ef board=native_sim/native/64 ...
@READY uptime_ms=...

kfsw:~$ status
kfsw:~$ param get log_level
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
