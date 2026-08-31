# Testing, HIL, and CI {#testing}

## Evidence is layered

No single K-FSW test proves every property. A build can catch configuration and
link errors without running behavior. A native integration test can execute
service interactions without proving physical UART signaling. A board shell
smoke can prove flash/boot/console without qualifying storage or CSP.

```text
                         physical HIL
                 board, programmer, serial/link
                              ^
                       Robot scenarios
                 operator-visible system behavior
                              ^
                    native integration tests
               full images, processes, links, files
                              ^
                    ztest / Twister units
                focused module contracts and faults
                              ^
                quality + configuration builds
             source rules, Kconfig, devicetree, linking
```

Higher layers use more of the real system but usually cover fewer cases. Lower
layers are faster and more exhaustive but cannot claim physical verification.
Project status should name the highest relevant evidence without discarding the
lower layers that make it diagnosable.

## Local software sequence

From the workspace root:

```bash
./k-fsw/tools/ci/all.sh
```

This performs, in order:

```text
west manifest validation
        |
clean Linux + NUCLEO builds
        |
clang-format + cppcheck
        |
Twister unit suites
        |
native integration scripts
        |
Valgrind native boots
        |
Robot dry-run + software scenarios
        |
Doxygen HTML/API build
```

It is a software-only command. It does not discover, flash, reset, or operate a
physical board.

## Hosted pull-request gates

`.github/workflows/ci.yml` defines the active Software CI workflow.

| Job/check | Local entry point | What it verifies |
| --- | --- | --- |
| `BUILD / linux` | `tools/ci/build.sh linux` plus `tests/build-linux-minimal.sh` | Full Linux target and a CSP-disabled minimal application composition configure/link cleanly |
| `BUILD / nucleo_l496zg` | `tools/ci/build.sh nucleo_l496zg` | Full NUCLEO target configures and links with the ARM Zephyr toolchain |
| `QUALITY / clang-format + cppcheck` | `tools/ci/quality.sh` | Selected project-owned C/H formatting and warning/performance/portability analysis |
| `UNIT / Twister` | `tools/ci/unit.sh` | Application/repository ztest suites, including west-managed reusable modules, on `native_sim/native/64` |
| `INTEGRATION / software` | `tools/ci/integration.sh` | Full native shell, storage, PARAM, CSP/KISS, RDP, and FTP interactions |
| `MEMORY / Valgrind` | `tools/ci/valgrind.sh` | Normal and corrupt-snapshot boot paths under Memcheck |
| `ROBOT / dry-run + software` | `tools/ci/robot.sh` | All Robot suite syntax plus operator-level nonphysical scenarios |
| `DOCS / Doxygen` | `tools/ci/docs.sh` | Warning-free HTML manual and public C API generation |

Every job creates a fresh Ubuntu 24.04 west workspace from the manifest. That
proves pinned commits are reachable and prevents an uncommitted adjacent
dependency from satisfying the build.

The build matrix contains Linux and NUCLEO only. FRDM-K64F and Pico W are not
currently hosted build gates. The printable PDF is also not a hosted CI gate;
it must be built and inspected locally for documentation changes.

## Build verification

A target build resolves Kconfig, devicetree, modules, generated headers,
drivers, link memory layout, and toolchain compatibility. The Linux job also
builds `tests/config/linux-minimal.conf`, in which CSP and its dependent
services are disabled. This protects the optional communications boundary at
the application-composition level.

A successful MCU build does not prove that a board was flashed, that its pins
match the bench, or that an enabled driver can exchange real data. Those are
physical claims.

Building the opt-in `nucleo-boton-test` configuration additionally checks that
`/chosen/kfsw,boton-test-button` resolves to a GPIO device and that the module,
shell diagnostic, parameter definitions, and application composition link for
the ARM target. It still does not prove a physical button press.

## Static quality

The quality script runs the repository's clang-format policy in dry-run/error
mode over project application/test sources and selected owned repository
sources. cppcheck analyzes all project-owned C implementations with a broad
enabled feature set.

It deliberately excludes upstream Zephyr, libcsp, and libparam source from
project formatting ownership. It also does not replace compiler warnings,
runtime testing, API review, race analysis, or a future safety coding standard.

## Unit tests with ztest and Twister

[ztest](https://docs.zephyrproject.org/4.4.0/develop/test/ztest.html) is
Zephyr's unit-test framework. [Twister](https://docs.zephyrproject.org/4.4.0/develop/test/twister.html)
discovers testcase metadata, creates configurations, builds them for selected
platforms, runs compatible binaries, and records results.

K-FSW runs project-owned suites on `native_sim/native/64`. Current suites cover:

- monotonic time behavior;
- CSP pre-initialization state and error contracts;
- libcsp-native route validation, longest-prefix selection, VIA retention,
  malformed-table rejection, unknown-interface rejection, and duplicate KISS
  name rejection;
- local parameter initialization, access, type/read-only/range validation, and
  callback behavior with CSP disabled;
- the optional parameter CSP/libparam serialization path in a separate
  configuration;
- parameter persistence encoding, loading, corruption/mismatch handling,
  defaults, and clear behavior with CSP disabled;
- `boton_test` initial state, first/multiple presses, held/released behavior,
  timestamp flooring, count/time saturation, coherent typed status, live
  read-only non-persistent parameter definitions, and an active-low GPIO
  emulator path that exercises real edge callbacks and rescheduled work;
- storage initialization/mount policy, capacity, and file operations; and
- FTP protocol codec, path sandbox, CRC, and atomic commit helpers.

Run:

```bash
./k-fsw/tools/ci/unit.sh
```

Twister output and inline logs are written to `build/twister/` by default.
Set `KFSW_TWISTER_OUT_DIR` when parallel work needs isolated output.

Native ztests still use Zephyr. They do not substitute a host-only mock for the
kernel, which helps exercise module CMake/Kconfig integration as well as the C
logic.

### `boton_test` evidence boundary

The focused module suite has a GPIO-disabled state/PARAM configuration and an
active-low GPIO-emulator configuration. The latter drives Zephyr GPIO edges,
restarts the real 30 ms delayable work across bounce, and checks hold and
release/rearm behavior. Private state hooks cover deterministic time and
saturation cases; there is no production fake-press command. Both
configurations remain independent of physical hardware and exercise the same
owner state exposed through `kfsw_boton_test_get_status()` and PARAM IDs 6 and
7.

The software acceptance scope is:

- zeroed state after initialization/reset;
- stable released-to-pressed counting, hold suppression, and release rearm;
- monotonic millisecond-to-second floor conversion;
- saturation of `press_count` and `last_press_s` at `UINT32_MAX`;
- a coherent typed two-field snapshot;
- PARAM inclusion when enabled, live value reflection, rejected writes, and no
  persistence flag; and
- clean NUCLEO profile configuration/linking with the chosen GPIO binding.

The focused run passed 2 configurations and 23 cases. The combined
UHF/routing/button candidate then passed the full Twister run (13
configurations, 62 cases), native integration, and Robot software scenarios.
The opt-in NUCLEO profile and the combined UHF-plus-button NUCLEO composition
also built successfully, as did the Linux, minimal, FRDM-K64F, and Pico W
profiles. Against the clean default NUCLEO composition, the opt-in button
profile increases image usage from 139,216 to 142,756 bytes of FLASH (+3,540)
and from 38,396 to 39,740 bytes of RAM (+1,344). The resulting classification
is **SOFTWARE VERIFIED**. It does not establish physical interrupt timing,
electrical polarity, mechanical bounce behavior, or a real press on PC13. The
separate manual NUCLEO acceptance remains **PHYSICAL VERIFICATION PENDING** and
must not be promoted without user interaction and captured observations.

## Native integration tests

`tools/ci/integration.sh` builds the full Linux target and its focused native
test images, then executes the shell runners below.

### Shell and local parameters

`tests/shell-smoke.sh` checks boot/readiness markers, prompt and root command
behavior, monotonic time, compiled logging behavior, local parameter
list/get/set validation, storage status, and command help.

`tests/boton-test-smoke.sh` builds both the default Linux image and an opt-in
software-only `boton_test` image. It checks that the disabled composition has
no button parameters, while the enabled composition initializes with zeroed
live values, lists both definitions as read-only, rejects writes, and leaves
owner state unchanged. This runner does not enable GPIO or synthesize presses.

### Storage

`tests/storage-smoke.sh` exercises create, write, read, overwrite, delete, and
capacity behavior. It starts separate native processes against the same
isolated flash file to prove LittleFS persistence across execution rather than
merely re-reading RAM.

### Parameter persistence

`tests/param-persistence-smoke.sh` checks save, boot-time restore, defaults,
load, clear, and restart semantics. It corrupts snapshot storage and verifies
the application rejects it, retains safe defaults, and reaches readiness
without formatting the filesystem.

### Two-node CSP and FTP

`tests/csp-smoke.sh` starts two application processes, discovers their KISS
PTYs, joins them with `socat`, and checks:

- bidirectional CSP ping;
- route and interface state;
- remote parameter list/get/set and validation;
- storage readiness on both nodes;
- FTP LIST, STAT, MKDIR, PUT, and GET;
- zero-byte, one-packet, multi-packet, and 8 KiB files;
- byte-for-byte comparison and CRC reporting;
- missing-file and traversal errors; and
- CSP buffer recovery after transfers.

Processes, FIFOs, temporary files, and isolated flash images are cleaned by the
runners.

### Three-node multi-KISS routing

`tests/build-multi-kiss.sh` builds a router plus nodes 10 and 11.
`tests/multi-kiss-smoke.sh` gives the router two separate native PTYs named
`KISS_1` and `KISS_2`, joins each to a different leaf with `socat`, and checks:

- router-originated traffic to node 10 selects `KISS_1`;
- router-originated traffic to node 11 selects `KISS_2`;
- both independent transmit/receive counter sets become nonzero;
- `csp routes` retains `11/14 -> KISS_2 via 11`; and
- node 10 and node 11 ping through the router in both directions.

The VIA check proves that K-FSW preserves libcsp's field. KISS ignores that
link-layer next hop by design. This is deterministic software evidence for two
simultaneous interfaces and transit forwarding, not evidence for a second
physical radio.

### Ground roles

`tests/k-ground-csp-smoke.sh` builds the configured UHF node-16 and operations
node-19 roles, checks their identity/status, and verifies bidirectional CSP
ping. This remains an independent composition test; `boton_test` has no
ground-role, route, KISS-interface, or node-number dependency.

### Focused local-only composition

`tests/param-local-smoke.sh` is an additional developer runner. It builds local
parameters and persistence with `KFSW_CSP=n` and `KFSW_PARAM_CSP=n`, checks
that libcsp was not linked, and exercises local operations and snapshots. The
Twister unit configurations already gate the CSP-independent core in hosted
CI; this script is useful when reviewing the complete shell composition.

## Valgrind

`tools/ci/valgrind.sh` runs two bounded KFSW-Linux processes under Memcheck:

1. a clean/normal application boot; and
2. a boot against an intentionally corrupted parameter snapshot.

Definite and indirect leaks are errors. The second run must log the rejected
snapshot, use defaults, and reach `@READY`. Program and Valgrind logs remain
under `build/valgrind/`.

Valgrind covers code executed by these native boots. It cannot analyze code
compiled only for the MCU, prove all allocation paths, detect every concurrency
error, or measure embedded stack margins.

## Robot Framework

[Robot Framework](https://robotframework.org/) is the operator/system test
layer. Its scenarios send commands, wait for stable output, and assert visible
behavior through `robot-terminal-runner`. They reuse the same K-FSW shell and
service runners instead of reimplementing CSP, filesystem, parameter, or FTP
logic in Robot keywords.

Hosted CI performs:

```bash
./k-fsw/tests/hil/run.sh --dryrun
./k-fsw/tests/hil/run.sh --exclude physical
```

The dry run validates discovery and syntax for all suites, including physical
ones, without executing their actions. The second command runs software-tagged
terminal scenarios for identity, CSP, remote parameters, storage, persistence,
and FTP. Reports are written below `build/robot/dry-run/` and
`build/robot/software/` by the wrapper.

Robot proves an operator-visible workflow. The underlying unit/integration
tests remain necessary for detailed error cases and diagnosis.

## Physical HIL

### NUCLEO boot fixture

`tests/hil-smoke.sh` checks ST-LINK USB presence and the configured console,
builds the NUCLEO target, starts capture before flash/reset, flashes through
OpenOCD, and requires both `@BOOT` and `@READY`.

`tests/hil/boot.robot` wraps this path and carries `physical`, `smoke`, and
`nucleo` tags.

### NUCLEO UART/KISS fixture

`tests/uart-csp-smoke.sh` requires:

- a NUCLEO-L496ZG and ST-LINK console;
- a distinct FT232RL 3.3 V UART connected to USART3 PD8/PD9/GND;
- KFSW-Linux and `socat` on the host; and
- stable device paths or unambiguous USB discovery.

It builds/flashes, captures the debug console, bridges the Linux PTY to the
FTDI UART, verifies bidirectional CSP and UART tests, exercises physical
storage and FTP, performs remote PARAM afterward, and requires clean KISS
counters. `tests/hil/uart.robot` wraps it.

Run NUCLEO physical Robot tests explicitly:

```bash
KFSW_DEBUG_SERIAL=/dev/serial/by-id/<st-link-console> \
KFSW_FTDI_DEVICE=/dev/serial/by-id/<ftdi-device> \
  ./k-fsw/tests/hil/run.sh --include physical
```

No physical test is selected by `tools/ci/all.sh` or hosted GitHub Actions.

### Reusable shell-only board fixture

FRDM-K64F and Pico W use `tests/hil/shell-smoke.sh`. The behavior is common;
each target descriptor supplies board, flash runner/USB identity, console,
baud, and prompt.

```bash
./k-fsw/tests/hil/shell-smoke.sh frdm_k64f
./k-fsw/tests/hil/shell-smoke.sh rpi_pico_w
```

These commands build, optionally flash, wait for the prompt, and check
`status`, `version`, and `help`. They are manual shell acceptance paths, not
Robot suites and not full service qualification.

## Manual HIL versus self-hosted CI

Physical HIL can currently be executed manually using the checked-in scripts
and Robot wrappers. There is no self-hosted GitHub hardware runner, automated
powered-USB fixture, or repository-level hardware resource locking.

```text
current
developer/bench -> explicit HIL command -> local report

planned
GitHub job -> resource lock -> powered fixture -> flash/test -> artifacts
```

The self-hosted runner and resource/fixture work remain open project items.
Documentation and hosted workflow configuration must not imply those gates are
active before the infrastructure and repository settings exist.

## CI/CD flow

Two hosted workflows are current:

```text
pull request
     |
Software CI: build + quality + unit + integration + memory + Robot + docs
     |
review and merge
     |
push to main
     +----> Software CI repeats
     |
     +----> Documentation Pages builds Doxygen artifact
                                      |
                                deploy GitHub Pages
```

The Pages workflow has read access to repository content plus scoped Pages and
OIDC permissions. Pull requests build documentation but do not deploy it.
Physical HIL is absent from both workflows.

## Documentation validation

`tools/ci/docs.sh` runs the Doxygen build with warnings treated as errors and
requires a nonempty generated home page. Doxygen inputs are the manual and
project-owned public headers only; upstream trees and generated output are
excluded.

For a documentation change, also run the local PDF build and inspect both
outputs:

```bash
./k-fsw/tools/docs/build.sh
./k-fsw/tools/docs/pdf.sh
```

Check navigation, local links, tables, code blocks, diagrams, page breaks,
margins, blank pages, and headings. A successful converter exit does not prove
that a long table or diagram is readable on paper.

## Interpreting a green pipeline

A green current pipeline means the pinned composition built and passed the
hosted software checks listed above. It does not mean:

- FRDM or Pico was rebuilt in CI;
- a physical NUCLEO was attached;
- a radio or CAN link was tested;
- the PDF was generated;
- all source paths ran under Valgrind;
- coverage met a threshold; or
- the software received flight or safety qualification.

These distinctions are reflected in @ref project_status.
