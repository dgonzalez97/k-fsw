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
- byte arrays: that one is written and validated whole or not at all, that a
  short or over-long write is refused, and that an owner judges the array
  together rather than element by element;
- the parameter table scheme: that every core table registers under the
  identifier its owner is allocated and in that owner's band, that a reserved,
  unallocated or unnamed table is refused, that a name past 32 characters is
  refused rather than truncated, that two parameters cannot share one offset
  while one offset may repeat in another table, that the wire identifier
  decodes back to table and offset, that a sampled value advances between two
  reads rather than reporting a copy taken at start-up, that an unbound
  watchdog reports no timeout instead of the configured one, and that a report
  period which would reset a healthy board is refused;
- `boton_test` initial state, first/multiple presses, held/released behavior,
  timestamp flooring, count/time saturation, coherent typed status, button
  and LED parameter definitions, LED validation, and GPIO-emulator paths that
  exercise button edges plus active-low LED polarity;
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
owner state exposed through `kfsw_boton_test_get_status()` and the two counter
parameters at offsets 0x00 and 0x04 of table 67
through 10.

### Firmware upload between two nodes

`tests/k-ground-fwu-lite-smoke.sh` sends a 20000-byte stand-in image from one
node to another over CSP, across 105 blocks, and checks the receiving node
holds exactly what was sent.

It asserts the byte count **and** the checksum: either alone would pass a
transfer that lost a whole block and gained a duplicate. It also checks the
receiving node stays in `receiving` until it is told to flash, because sending
stops at a verified image and committing it is a separate command.

The block protocol itself is unit tested without a link, so every rejection is
exercised directly: a block out of order, a block failing its own checksum and
then succeeding when resent, a short block in the middle, a second transfer
while one is running, and an unknown opcode being answered rather than ignored.

### MCUboot rollback acceptance

`tests/hil/mcuboot/rollback.sh` is the acceptance for `k-fsw#2`. It proves the
two flows the issue names, and three properties that make them mean something.

Images A and B are the **same binary signed with different versions**, so any
difference in behaviour is the bootloader's doing rather than a difference
between two programs.

| Check | Proves |
| --- | --- |
| Signed artefact verifies against the project key, and **not** against MCUboot's default | the bootloader carries the right public key |
| `A` → test `B` → no confirm → `A` | automatic revert |
| `A` → test `B` → confirm → `B`, across two reboots | permanent upgrade, and no late revert |
| An image signed with MCUboot's public default key is refused | the signature is actually checked |
| A value written under `A` is readable at the very end | the swap machinery leaves the storage partition alone |

Without the wrong-key case, "the bootloader ran the image" would prove only
that it ran something, not that it verified anything.

Recorded on 3 September 2026 against `4dfd520`, all ten checks passing:

```
install    running A (1.0.0), confirmed and persists
case 1     swapped to B (2.0.0), unconfirmed, REVERTED to A (1.0.0)
case 2     swapped to B, confirmed, persists across a further reboot
case 3     wrong-key image refused; still running 2.0.0
storage    the filesystem at 0xf0000 still mounts
```

The storage check deliberately writes a witness value under image `A` and reads
it back after every swap, revert and reboot. Checking only that the filesystem
mounts would prove almost nothing: the test erases the whole chip before
installing, so the filesystem it would find is a fresh one it made itself.

**A firmware update must be written one sector into the secondary slot**
(`0x08068800`, not `0x08068000`). MCUboot runs in `BOOT_SWAP_USING_OFFSET` mode,
where writing at the slot start is a silent no-op: no error and no swap, with
the old image still running. This test failed exactly that way before the
offset was applied.

### Health monitoring acceptance

`tests/hil/health/health-reset.sh` proves the chain end to end: a component
stops reporting, the feed is withheld, the board resets itself, and the next
boot says why.

The fault is real rather than simulated. There is no command to stall a thread
and there will not be one, because that means shipping a way to hang the flight
software; instead `health watch <name> <ms> confirm` registers a component that
nothing reports, so health has something genuinely overdue.

Surviving three watchdog timeouts while healthy comes first. Without it, a
board resetting every few seconds for an unrelated reason would pass a test
that only checked a reset happened.

Recorded on 4 September 2026 against `f9973be`, eight checks:

```
health owns the watchdog    feeding: yes, platform keep-alive released
survives 26 s healthy       three times the watchdog timeout, no reset
component overdue           feed withheld
reset                       reset_cause=watchdog
recovered                   health supervising again
```

### Watchdog hardware acceptance

`tests/hil/watchdog/watchdog-reset.sh` proves the platform watchdog on a board,
in one uninterrupted serial capture so the reset and the boot that follows it
are a single observation rather than two hopeful ones.

The sequence is: arm and confirm the feed counter advances; survive twice the
timeout while being fed; stop the feed; observe the reset inside a bounded
window; and read the cause on the next boot.

The survival step is what makes the rest mean anything. Without it a board
resetting every few seconds for an unrelated reason would pass a test that only
checked that a reset happened.

Recorded on 3 September 2026 against `25f03f9` on a NUCLEO-L496ZG with an
8000 ms timeout:

```
armed      state=running device=bound feeds=1
survives   18 s fed, feeds 1 -> 8, no reset
starve     reset observed within 13 s
next boot  reset=0x00000011 reset_rc=0 reset_cause=watchdog
rearmed    state=running
```

`0x11` is the pin and watchdog bits latched together, and the decoder reported
`watchdog`. That multi-cause preference is asserted in the unit suite and was
confirmed here on hardware.

The unit suite runs on `native_sim`, which has no watchdog driver at all. That
is deliberate: it pins the feed-interval arithmetic and the reset-cause
decoding, which are pure and must hold everywhere, and it pins what a
composition sees on a board with no watchdog hardware, where every operation
reports `-ENODEV` rather than appearing to succeed.

### `boton_test` hardware acceptance

`tests/hil/boton-test/button-acceptance.sh` is the manual fixture for the three
claims the automated suites cannot settle: that a physical press increments the
counter and an untouched board does not, that a held button counts once rather
than repeating, and that the LEDs physically light through both the shell and
the parameter table.

It flashes the opt-in profile, records a baseline with the board untouched, then
prints one line per press the module reports, carrying the host time, the
counter and the module's own `last_press_s`. Both LED paths are exercised in
turn and read back through `boton_test status`, and the two counter parameters
in table 67 are compared against that status. `--no-flash` reuses the image already on the
board, which lets an operator be told to start pressing at a known moment.

The fixture prints the timeline and does not grade it. An earlier version tried
to keep step with the operator by opening a window per gesture and advancing
when the counter moved; it straddled gestures whenever the operator worked
ahead of it, and reported per-step deltas that were wrong even though the total
was right. Recording a timeline and reading the gestures back out of it
afterwards removes that failure mode.

Recorded on 3 September 2026 against a NUCLEO-L496ZG: a ten-second untouched
baseline held `press_count` at 0, physical presses advanced it to 7, the
operator observed green, blue and red lighting through `test led` and through
`param set hw_test_led_*`, and `param get press_count` agreed with
`boton_test status`. Per-gesture attribution was lost to the window design and
is not claimed.

The software acceptance scope is:

- zeroed state after initialization/reset;
- stable released-to-pressed counting, hold suppression, and release rearm;
- monotonic millisecond-to-second floor conversion;
- saturation of `press_count` and `last_press_s` at `UINT32_MAX`;
- a coherent typed five-field snapshot registered as table 67 in the module
  band;
- PARAM inclusion when enabled, live value reflection, writable boolean LED
  validation, rejected writes, and no persistence flag;
- shell and PARAM control through the same LED owner setter; and
- clean NUCLEO profile configuration/linking with four chosen GPIO bindings.

The focused run passed 2 configurations and 28 cases. The final hardware-test
candidate then passed the full Twister run (13 configurations, 67 cases), the
affected native integration smoke, and all Robot software scenarios.
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
no hardware-test parameters, while the enabled composition initializes five
zeroed values, preserves read-only button state, exercises all three LED shell
commands, and rejects invalid colours, operations, and boolean writes. This
runner does not enable GPIO or synthesize physical input.

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

### Command registry and event record

`tests/unit/services_command` covers registry validation: duplicate identifiers
and names, a missing handler, an oversized argument list, argument type and
count checking, the unknown-command path, and a handler that returns an error
without setting a status.

`tests/unit/services_event` covers ring behaviour: read by age, sequence
continuing across a clear, wrap overwriting the oldest record while counting
the loss, rejection of an oversized payload and an unknown source, an empty
payload, and a visitor stopping the walk early.

`tests/csp-smoke.sh` additionally invokes a command locally, invokes one across
the two-node link, and checks that an unknown name is rejected.

### Ground-node file transfer

`tests/k-ground-ftp-smoke.sh` builds the two configured ground roles, bridges
their KISS PTYs, and moves one file between them. The operator node 19 uploads
`test.txt`, reads its metadata back, downloads it again, and compares the two
local copies; the gateway node 16 then lists and stats the received file
through its own address, which exercises the local-node path without a
connection. The missing-file negative path runs in the same session. The CRC
reported by the generator, by the remote `stat`, and by the returned copy must
be identical.

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
FTP, firmware update and parameter tables. Reports are written below
`build/robot/dry-run/` and `build/robot/software/` by the wrapper.

### Parameter tables

`tests/hil/param-tables.robot` wraps `tests/param-tables-smoke.sh`. It checks
that every table a composition declares is present under its own identifier and
band, that one offset repeats across tables the way the scheme intends, and
that the listing reports each parameter's write mode.

Recorded on 5 September 2026 against `43ac957`: `PARAM TABLES RESULT: PASS`
read from a NUCLEO-L496ZG over its ST-LINK debug UART, across eleven tables.

It is deliberately about existence and addressing rather than content. A value
is only as good as the layer underneath it, so asserting a particular
free-space figure would test LittleFS rather than the table scheme. The first
three cases carry `software` and run hosted; the fourth carries `physical` and
reads the same listing from a NUCLEO over its debug UART, where the tables are
built from real hardware.

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

### Holybro UHF CSP/KISS and file-transfer fixture

`tests/hil/radio-uhf/holybro/csp-kiss-smoke.sh` is the radio acceptance. It
builds ground node 16 and NUCLEO node 2, flashes the NUCLEO, bridges the ground
KISS PTY to the USB-side Holybro at 57600 baud, and then verifies over RF:

- bidirectional CSP ping between node 16 and node 2, and each node reaching
  itself;
- production remote PARAM read, write, owner callback, and the invalid-value
  restore-to-default oracle;
- commanding across the link, including a self-addressed command, and reading
  the remote node's event record;
- a complete file round trip — `generate`, `mkdir`, `put`, `stat`, `ls`, `get`,
  `verify` — with the same CRC on both nodes and on both local copies;
- the flight node reading its own FTP root through its own address;
- missing-file and unreachable-node negative paths; and
- clean, nonzero KISS counters on both ends afterwards.

It reads the bench device paths from a host-specific environment file and never
changes a persistent SiK or AT setting. Host serial settings are saved and
restored around the run.

### Recorded result

Run of 5 September 2026, `k-fsw` main `43ac957`, NUCLEO-L496ZG as CSP node 2
and a `native_sim` ground node 16 over a Holybro SiK pair at 57600 8N1. No
persistent radio setting was changed.

```text
ping        16 -> 2 success rtt_ms=230
self        csp ping 16: this node, no link traversed
command     noop node=16: OK noop from node 0       ran locally, not sent
            noop node=2:  OK noop from node 16      across the radio
            info node=2:  OK uptime_ms=12733 storage=ready free_bytes=53248
event       event_stats node=2: OK held=5/32 recorded=5 overwritten=0 rejected=0
parameters  the whole descriptor list arrived: every table read across the link
            2:uid = "kfsw-2"                        a string, read as text
            2:ftp_timeout_ms = 20000                a setting written from the ground
transfer    put/get 256 B, crc32=0ce9d363 on both nodes and both local copies
negative    csp ping 3 failed; unknown command rejected; missing file not found
counters    NUCLEO KISS tx=127 rx=81  all errors 0
```

Two results here changed meaning rather than value. A node addressed to itself
now answers without traversing a link, and a command addressed to this node
runs here rather than being sent into the network and back, which is why the
reply names source node 0.

The `event_tail` payload is the flight node's own record of the command that
produced it: command `0x0004` from node `0x0010`, status `0x00`. Commanding,
the event record and the wire format are confirmed together.

This is functional bench evidence for one named fixture. It is not a throughput
characterisation, a soak, or any claim of qualification.

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
