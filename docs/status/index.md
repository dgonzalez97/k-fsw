# Project Status and Roadmap {#project_status}

## Status baseline

This status was updated on 31 August 2026 for the `boton_test` reference module
tracked by [kfsw-modules issue 6](https://github.com/dgonzalez97/kfsw-modules/issues/6).
It also reflects the published multi-KISS dependency, merged UHF and routing
compositions, exact `west.yml` pins, current Kconfig/target profiles, project
tests, workflows, issues, and pull requests. Automated software evidence is
kept separate from pending manual USER-button acceptance. The named Holybro
bench retains its previously reviewed raw and CSP/KISS evidence boundaries.

Status here is an engineering summary, not a substitute for the issue tracker
or a release qualification record.

## Capability matrix

| Area | Implementation | Software evidence | Physical evidence | Current limits |
| --- | --- | --- | --- | --- |
| Platform/time | Monotonic ms/us and reset-cause API implemented over Zephyr | Time ztest; used by native boot/shell tests | Reset/boot path exercised on NUCLEO; no separate clock-accuracy qualification | No UTC/TAI/GNSS or clock correlation |
| Logging | Fixed-buffer DEBUG/INFO/WARNING/ERROR with compile/runtime filters | Shell and integration diagnostics | Console logging observed in board HIL | Not a structured/persistent event service; no rate limiting |
| CSP core | Optional libcsp identity, loopback, validated native static routes with destination/prefix/interface/VIA, ping, one router | Route validation/precedence ztest; two-node and three-node native integration | Bidirectional CSP ping on NUCLEO/FTDI UART bench | No dynamic route mutation, redundant-link failover policy, or flight routing plan |
| UART/KISS | Legacy chosen UART or generic independently named devicetree instances with separate state/counters | Two-node PTY tests plus simultaneous `KISS_1`/`KISS_2` direct selection and bidirectional transit | One NUCLEO USART3/FTDI and one Holybro link physically verified | Multiple links are software-verified only; 115200 reference profiles and 57600 Holybro overlays |
| k-ground | Configured `native_sim` roles using the normal K-FSW shell/services and optional route string | UHF node 16 and ops node 19 report role-specific identity and ping both ways | No physical evidence required for the local profile | Launcher connects one direct peer link; no generic router orchestration or mission-control framework |
| radio-uhf module | Compile-time implementation selection, generic identity/expected-configuration API, bounded `uhf status`; Holybro SiK implementation | Dedicated module ztest and node-16/NUCLEO composition builds | Reuses the separately verified Holybro bench | No live modem readback/control, radio parameters, worker, or data-plane API |
| Holybro UHF fixture | Separate NUCLEO raw-byte peer and CSP/KISS HIL entry points under `radio-uhf/holybro` | Scripts validate module identity, production PARAM, file transfer, negative behavior, interfaces/routes/counters | Raw 100/100 with no invalid/timeout; bidirectional node 16 ↔ 2 CSP ping; remote production PARAM validation/callback; 256-byte file upload, remote stat/list, download and byte comparison; KISS counters with zero errors | Functional bench evidence only; no RF performance/soak campaign or qualification |
| `boton_test` / `hw_test` reference module | Module-owned 30 ms debounce, coherent five-field typed status, PARAM IDs 6–10, three LED owner controls, chosen-GPIO binding, table ID 67 reserved | **SOFTWARE VERIFIED**: focused state/GPIO tests, Linux shell/PARAM smoke, and NUCLEO profile build; final matrix recorded with the feature commit | **PARTIALLY PHYSICALLY VERIFIED** on 3 September 2026: an untouched board held `press_count` at 0 for 10 s, physical presses advanced it to 7, both LED paths were observed lit by the operator and read back through `boton_test status`, and PARAM ID 6 agreed with the module status. Per-gesture attribution is not yet isolated | NUCLEO example is opt-in; no dedicated thread/allocation, LED persistence, CSP dependency, or HK collector yet |
| Firmware update | Streams an image into the secondary slot at the swap offset, verifies a whole-image CRC32, and confirms the bootloader scheduled a swap. Two routes: a reserved file-transfer path, and a direct block protocol with per-block checksums and repeat | 52 unit cases across the update service and the block protocol, on simulated flash; a two-node integration transfer of 20000 bytes over CSP | **SOFTWARE VERIFIED**; the radio path and the resulting boot are not yet claimed | No authenticity beyond the bootloader signature; one transfer at a time; blocks must be sequential and full except the last; ground must hold the image to send it |
| MCUboot boot and rollback | Bootloader plus two 352 KB slots and a reserved 192 KB golden region, ECDSA P-256 signature check, swap with automatic revert, `mcuboot` shell for confirm and upgrade | Opt-in sysbuild composition builds bootloader and signed application; the signed artefact is checked against the project key and against MCUboot's default | **PHYSICALLY VERIFIED** on 3 September 2026: an unconfirmed test image reverted to its predecessor, a confirmed one persisted across two reboots, an image signed with MCUboot's public default key was refused, and a value written to storage under the first image was still readable after every swap and revert | Update transport is out of scope; golden region reserved but unwritten and not selectable; images must be written one sector into the secondary slot |
| Platform watchdog | Chosen-bound device, timeout and keep-alive at a third of it, deliberate starvation, reset-cause decoding with the watchdog preferred among latched causes | Six-case `native_sim` suite covering the interval margin across the configurable range, cause decoding, and the no-hardware `-ENODEV` contract | **PHYSICALLY VERIFIED** on 3 September 2026: armed on a NUCLEO-L496ZG, survived 18 s fed with the feed counter advancing, reset within 13 s of deliberate starvation, and the next boot reported `reset_cause=watchdog` from a mask that also latched the pin bit | Mechanism only, no health policy; one timeout channel; cannot be disarmed once armed on the STM32 independent watchdog |
| Local parameters | Static typed table, exact scalar checks, read-only flags, callbacks | CSP-disabled ztest and local/full shell integration | Local table runs in NUCLEO composition; physical bench checks remote access to it | Base table is small; button profile adds two live read-only values; string/data/arrays absent |
| PARAM CSP adapter | Optional libparam-compatible server/client/cache | Two-node native remote list/get/set plus Robot errors | Holybro RF bench passed production list/get/set and the valid owner callback; the corrected reset-to-default oracle passed physically on 3 September 2026 (`1` to `3`, then invalid `5` restoring the compiled `1`) | Fixed remote descriptor pool; no remote persistence command |
| Parameter persistence | Explicit bounded versioned CRC snapshot and defaults/load/save/clear | CSP-disabled unit suite, cross-process integration, corrupt snapshot fallback, Valgrind | No dedicated NUCLEO reboot/persistence acceptance | Local only; no migration framework beyond name/type compatibility |
| Storage | LittleFS lifecycle at `/kfsw`, cautious first-format policy, capacity API | Storage ztest and native cross-process integration | NUCLEO storage info/test passes in UART HIL | Linux/NUCLEO full profiles only; one 64 KiB volume per profile |
| K-FSW FTP | LIST/STAT/MKDIR/PUT/GET, 192-byte chunks, CRC, sandbox, atomic `.part` finalization; operation/transfer/transport layering with one transport backend; own-node LIST/STAT/MKDIR served locally | Protocol ztest, native transfers from 0 bytes through 8 KiB, ground-role round trip, Robot workflow | 4 KiB and 16 KiB round trips on NUCLEO UART bench; one 256-byte round trip over the Holybro RF bench with matching CRC and clean counters | K-FSW protocol, not Internet FTP; one server worker/client workspace; PUT/GET need two nodes; no RF throughput characterisation |
| Self-addressing | A node reaches its own address through an ordinary interface, so the source address is applied; registered only when no other interface covers the address | Two-node integration asserts ping and a command to the local node | Ground node 16 and NUCLEO node 2 both register the interface; self-addressed ping and command pass on the Holybro bench | Not registered on a node whose own address sits in another interface's subnet, such as the multi-interface router |
| Command service | Frozen compile-time registry; one definition reached by name from the shell and by numeric identifier over CSP port 11; two-level validation; handlers on a dedicated thread | Registry, duplicate rejection, argument type/count checks, unknown and silent-failure paths in ztest; local and remote invocation in two-node integration | Commands served across the Holybro link from ground node 16 to NUCLEO node 2, including a self-addressed command and an unknown-command rejection | Synchronous only; no accepted-plus-identifier form; no authentication, though the request context reserves the fields; an unknown name is rejected locally before reaching the wire |
| Event record | Numeric records in a bounded RAM ring with stable identifier, monotonic timestamp, sequence number, severity and opaque payload; identifiers owned by the producing component; boot, command and FTP emit | Ring wrap and overwrite counting, read-by-age, rejection counting, visitor early stop in ztest | The NUCLEO's record read from the ground node over the Holybro link, counters and one decoded record | RAM only, so it does not survive a reset; no persistent journal, rate limiting, coalescing or downlink stream; payloads are opaque and decoded by ground tooling |
| Shell | Zephyr root commands with history, completion, help, and K-FSW prompt | Native shell/integration/Robot tests | Full NUCLEO shell; FRDM and Pico physical shell bring-up | Debug/operations adapter, not a command authorization service |
| Linux target | Full reference composition on `native_sim/native/64` | Hosted build, unit, integration, Valgrind, Robot | Physical verification not applicable | Simulation does not prove MCU/electrical/timing properties |
| NUCLEO-L496ZG | Full reference composition, flash layout, dual serial paths | Hosted clean build and native-equivalent service tests | Boot/readiness, storage, UART/KISS, CSP, remote PARAM, FTP bench | No CAN, radio, MCUboot, watchdog, or full mission qualification |
| FRDM-K64F | Shell-only target profile | Local build path exists; not in hosted matrix | Prompt, status, version, help physically verified | CSP, PARAM, storage, FTP disabled and unqualified |
| Raspberry Pi Pico W | USB CDC shell-only target profile | Local build path exists; not in hosted matrix | Prompt, status, version, help physically verified | Wi-Fi and full services disabled and unqualified |
| Test/CI | Hosted build, quality, Twister, integration, Valgrind, Robot, Doxygen; Pages deploy from main | Latest reviewed main runs successful | Manual checked-in HIL paths | No coverage threshold, self-hosted HIL, resource locking, or hosted PDF gate |

## What “supported” means here

Linux and NUCLEO-L496ZG are the full reference targets. Their default Kconfig
compositions are built by hosted CI, and the NUCLEO has the physical evidence
listed above.

FRDM-K64F and Pico W are real K-FSW target descriptors with merged physical
shell bring-up evidence. They are intentionally not labeled full reference
targets because their configurations disable the current storage,
communications, and parameter services.

No row means flight-qualified. Physical verification is tied to a named bench
and acceptance behavior; it does not automatically transfer to a new board,
radio, cable, routing plan, or Kconfig combination.

`boton_test` was **SOFTWARE VERIFIED** on 31 August 2026 by its automated
state, debounce, saturation, typed API, PARAM, LED owner controls, Linux
integration, and NUCLEO composition gates. On 3 September 2026 part of it
became physical: on a NUCLEO-L496ZG running the opt-in profile, an untouched
board held `press_count` at 0 for ten seconds, a sequence of physical presses
advanced it to 7, the operator observed the green, blue and red LEDs light
through both `test led` and `param set hw_test_led_*`, and PARAM ID 6 read back
the same count as `boton_test status`.

What is still not claimed is per-gesture attribution. The first fixture opened
a window per gesture and advanced when the counter moved, so it straddled
gestures whenever the operator worked ahead of it; the recorded total was
consistent with the sequence asked for, but the individual steps cannot be
decomposed from it. One count per press, and a held button counting once rather
than repeating, therefore remain pending user
interaction on the named bench.

## Known limitations

### Startup health

Startup logs errors and continues through independent stages. `@READY` marks
completion of the startup sequence but does not aggregate service health. A
future health/FDIR design will need explicit required/optional service policy.

### Communications topology

One-interface profiles retain a direct `0/0` KISS default suitable for a
two-node test link. Multi-interface profiles use validated static libcsp routes
and have software evidence for two different UART/KISS links plus bidirectional
transit. There is no CAN/CFP, SocketCAN/vcan, ZMQ, redundant link selection, or
dynamic route management. The Holybro module complements the existing serial
KISS path; it does not replace or wrap that data plane. Only one Holybro/KISS
link has physical evidence; `KISS_2` has not been physically tested. After the
bench power/USB arrangement was corrected, raw traffic and bidirectional CSP
passed without RF parameter changes. The raw HIL peer also required removal
of blocking polling behavior; the production UART/KISS receive path remains
interrupt-driven.

CRC32 provides accidental-corruption detection, not authentication. HMAC,
encryption, keys, command authorization, and operational security policy are
not implemented.

### Service scope

The parameter table is deliberately small. Persistence has one snapshot and
basic name/type compatibility rather than schema versions and mission
migrations. FTP has one static worker and no multi-user or authorization
model.

The command service is synchronous only: a handler runs to completion and
there is no accepted-plus-identifier form for a long operation. It carries no
authentication. The request context reserves a source and an authentication
result, but the flag is always false.

The event record is a fixed RAM ring. It does not survive a reset, so it
answers what a node has done rather than what happened before it restarted.
There is no persistent journal, rate limiting, coalescing, or downlink stream.
Logging remains console-oriented and separate; events are the record, not a
replacement for log messages.

There is no current local message bus, housekeeping, health/watchdog policy,
flight planner, telemetry serialization, or firmware update application.

### Qualification and release

There is no current coverage gate, stack-usage qualification, worst-case timing
campaign, long-duration soak, fault-injection campaign, formal safety process,
signed release process, or traceable flight qualification baseline. The
existing test system is a strong development foundation, not evidence that
those activities are complete.

## Near-term project direction

The basic command service
([k-fsw issue #4](https://github.com/dgonzalez97/k-fsw/issues/4)) is
implemented and closed. Root shell commands for a specific service remain
direct adapters to that service's API; the `cmd` root is the generic registry,
and its shell front end resolves to the same definition and validation a remote
caller passes through.

The remaining active area on the public board is a self-hosted physical HIL
runner ([k-fsw issue #11](https://github.com/dgonzalez97/k-fsw/issues/11)).
All physical HIL is still manually invoked.

The platform watchdog
([kfsw-platform issue #3](https://github.com/dgonzalez97/kfsw-platform/issues/3))
is the next capability. It is the prerequisite for any health policy, and for a
firmware update that must recover from a candidate image that stops
responding.

## Broad roadmap

The remaining public backlog groups naturally into a few technical steps.
There are no promised dates.

### Make physical verification repeatable in CI

Add hardware resource locking and fixtures
([k-fsw issue #12](https://github.com/dgonzalez97/k-fsw/issues/12)), then connect
the proven manual HIL behaviors to a controlled self-hosted runner. Preserve
the distinction between reusable test behavior and bench-specific devices,
power switching, serial paths, and recovery.

### Establish boot recovery and watchdog mechanisms

Define and physically prove an MCUboot primary/secondary image layout,
test/confirm/revert behavior
([k-fsw issue #2](https://github.com/dgonzalez97/k-fsw/issues/2)). Add the
platform watchdog and reset diagnostics needed by later health policy
([kfsw-platform issue #3](https://github.com/dgonzalez97/kfsw-platform/issues/3)).
Transporting a candidate image is separate from bootloader recovery mechanics.

### Add the next physical network

Implement raw CAN and then libcsp CAN/CFP with a Linux SocketCAN equivalent
([kfsw-comms issue #2](https://github.com/dgonzalez97/kfsw-comms/issues/2)).
Keep current UART/KISS working and add routing/error-state visibility before
describing CAN as supported.

### Improve measurement and fault evidence

Add hosted coverage reporting
([k-fsw issue #8](https://github.com/dgonzalez97/k-fsw/issues/8)), then grow
fault injection, stack/timing measurement, and soak testing around implemented
services. A coverage percentage should support review; it should not replace
behavioral or physical acceptance criteria.

### Grow flight services deliberately

After the command boundary is established, future services such as structured
events, health, housekeeping, and scheduling, plus equipment modules beyond
the narrow `boton_test` reference example, should be introduced one contract at
a time. Older architecture/reference notes are design input, not an
already-approved feature list. Each new capability needs an owner, Kconfig
boundary, target composition, tests, operational semantics, and an honest
status row.

## Keeping this page current

When a feature merges:

1. verify the final manifest and target defaults;
2. identify the software and physical tests that actually ran;
3. update the relevant explanatory chapter;
4. adjust one status row without turning the manual into an issue mirror; and
5. keep future work separate from current behavior.

GitHub issues and the project board remain the detailed work queue. This page
should answer “what can I rely on today?” and “what direction is next?” without
inventing schedule or qualification claims.
