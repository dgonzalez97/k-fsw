# Project Status and Roadmap {#project_status}

[TOC]

## Recorded status

This page records implementation limits and software and bench evidence.
Dates and image revisions identify the recorded runs; they are not a claim
that every later revision passed the same tests.

Linux and NUCLEO-L496ZG are the reference targets. FRDM-K64F and Pico W
have shell bring-up evidence. No target is flight-qualified.

## Capability matrix

| Area | Implementation | Software evidence | Physical evidence | Current limits |
| --- | --- | --- | --- | --- |
| Platform/time | Monotonic ms/us and reset-cause API implemented over Zephyr | Time ztest; used by native boot/shell tests | Reset/boot path exercised on NUCLEO; no separate clock-accuracy qualification | Monotonic API; CSP RTC time is separate and accuracy is not qualified |
| Logging | Fixed-buffer DEBUG/INFO/WARNING/ERROR with compile/runtime filters | Shell and integration diagnostics | Console logging observed in board HIL | Not a structured/persistent event service; no rate limiting |
| CSP over CAN | libcsp CFP over a CAN controller, selected by a chosen node so the same driver serves a board and a host USB adapter; bitrate published and settable from the ground | A composed NUCLEO profile and a ground node build; the route table selects CAN or the serial link per destination | **PHYSICALLY VERIFIED** on 5 September 2026: a NUCLEO-L496ZG on CAN1 and a PCAN-USB adapter on one bus at 500 kbit/s. Ping, identity and remote parameters all crossed it, the adapter's counters showed the frames, and both nodes stayed error-active. Re-verified on 10 September 2026 against `54ac87f`: `CAN SMOKE RESULT: PASS ... rtt_ms=50 ident=yes params=yes packets=rx:1258/tx:84 berr=tx0/rx0` | No transceiver on the NUCLEO, so wiring and termination are the operator's; changing the bitrate cuts the link that carried the change until the other end follows. The housekeeping bridge speaks KISS over a serial device, so it does not reach a node over CAN |
| CSP core | Optional libcsp identity, loopback, validated native static routes with destination/prefix/interface/VIA, ping, one router | Route validation/precedence ztest; two-node and three-node native integration | Bidirectional CSP ping on NUCLEO/FTDI UART bench | No dynamic route mutation, redundant-link failover policy, or flight routing plan |
| UART/KISS | Legacy chosen UART or generic independently named devicetree instances with separate state/counters | Two-node PTY tests plus simultaneous `KISS_1`/`KISS_2` direct selection and bidirectional transit | One NUCLEO USART3/FTDI and one Holybro link physically verified | Multiple links are software-verified only; 115200 reference profiles and 57600 Holybro overlays |
| k-ground | Configured `native_sim` roles using the normal K-FSW shell/services and optional route string | UHF node 16 and ops node 19 report role-specific identity and ping both ways | No physical evidence required for the local profile | Launcher connects one direct peer link and no generic router orchestration; Yamcs holds housekeeping but nothing commands through it |
| radio-uhf module | Compile-time implementation selection, generic identity/expected-configuration API, bounded `uhf status`; Holybro SiK implementation | Dedicated module ztest and node-16/NUCLEO composition builds | Reuses the separately verified Holybro bench | No live modem readback/control, radio parameters, worker, or data-plane API |
| Holybro UHF fixture | Separate NUCLEO raw-byte peer and CSP/KISS HIL entry points under `radio-uhf/holybro` | Scripts validate module identity, production PARAM, file transfer, negative behavior, interfaces/routes/counters | Raw 100/100 with no invalid/timeout; bidirectional node 16 ↔ 2 CSP ping; remote production PARAM validation/callback; 256-byte file upload, remote stat/list, download and byte comparison; KISS counters with zero errors | Functional bench evidence only; no RF performance/soak campaign or qualification |
| `boton_test` / `hw_test` reference module | Module-owned 30 ms debounce, coherent five-field typed status, five parameters in table 67, three LED owner controls, chosen-GPIO binding | **SOFTWARE VERIFIED**: focused state/GPIO tests, Linux shell/PARAM smoke, and NUCLEO profile build; final matrix recorded with the feature commit | **PARTIALLY PHYSICALLY VERIFIED** on 3 September 2026: an untouched board held `press_count` at 0 for 10 s, physical presses advanced it to 7, both LED paths were observed lit by the operator and read back through `boton_test status`, and `press_count` agreed with the module status. Per-gesture attribution is not yet isolated | NUCLEO example is opt-in; no dedicated thread/allocation, LED persistence, CSP dependency, or HK collector yet |
| Firmware update | Streams an image into the secondary slot at the swap offset, verifies a whole-image CRC32, and confirms the bootloader scheduled a swap. Two routes: a reserved file-transfer path, and a direct block protocol with per-block checksums and repeat | 52 unit cases across the update service and the block protocol, on simulated flash; a two-node integration transfer of 20000 bytes over CSP | **PHYSICALLY VERIFIED** on 5 September 2026 against `db64963`: an image was sent from a `native_sim` ground node 16 to a NUCLEO-L496ZG node 2 over a Holybro pair, flashed, and the node rebooted into it. The node reported `revision: fwu-before` before the transfer and `revision: fwu-after` after it, the CSP link recovered on its own, and `mcuboot confirm` left the new image marked `confirmed: 1` | No authenticity beyond the bootloader signature; one transfer at a time; blocks must be sequential and full except the last; ground must hold the image to send it |
| MCUboot boot and rollback | Bootloader plus two 352 KB slots and a reserved 192 KB golden region, ECDSA P-256 signature check, swap with automatic revert, `mcuboot` shell for confirm and upgrade | Opt-in sysbuild composition builds bootloader and signed application; the signed artefact is checked against the project key and against MCUboot's default | **PHYSICALLY VERIFIED** on 3 September 2026: an unconfirmed test image reverted to its predecessor, a confirmed one persisted across two reboots, an image signed with MCUboot's public default key was refused, and a value written to storage under the first image was still readable after every swap and revert | Update transport is out of scope; golden region reserved but unwritten and not selectable; images must be written one sector into the secondary slot |
| Health monitoring | Components register with a deadline and report as they run; the watchdog is fed only by a check that finds every one within its deadline. Registration can be undone so a stopped service does not reset a working board | 14 `native_sim` cases covering registration, deadlines, recovery and the refusal to supervise without a watchdog | **PHYSICALLY VERIFIED** on 4 September 2026: healthy for three watchdog timeouts with no reset, then a genuinely overdue component caused a withheld feed, a reset, and `reset_cause=watchdog` on the next boot | Liveness only, no resource or subsystem checks; the application thread is the only component watched so far |
| Platform watchdog | Chosen-bound device, timeout and keep-alive at a third of it, deliberate starvation, reset-cause decoding with the watchdog preferred among latched causes | Six-case `native_sim` suite covering the interval margin across the configurable range, cause decoding, and the no-hardware `-ENODEV` contract | **PHYSICALLY VERIFIED** on 3 September 2026: armed on a NUCLEO-L496ZG, survived 18 s fed with the feed counter advancing, reset within 13 s of deliberate starvation, and the next boot reported `reset_cause=watchdog` from a mask that also latched the pin bit | Mechanism only, no health policy; one timeout channel; cannot be disarmed once armed on the STM32 independent watchdog |
| Local parameters | Tables addressed by identifier and offset in owner bands, scalars, strings and byte arrays, exact size checks, read-only flags, validate and change callbacks, sample-on-read, derived write modes | CSP-disabled ztest, a string and array ztest, a core-table ztest, and local/full shell integration | Local tables run in the NUCLEO composition; physical bench checks remote access to them | Most configuration values in the core tables are read-only, because applying them needs the layer below to read a stored value back at start-up and that path does not exist |
| Parameter tables | Eighteen definition sets across the tree — core 1–6, services 25–33, modules 50, 51 and 67 — with 122 parameters between them, plus a test-only fixture on table 24. No single composition carries them all: the reference NUCLEO image reports 12 tables and 75 parameters | **SOFTWARE VERIFIED**: a 24-case core-table ztest, a string and array ztest, `param-tables-smoke.sh`, and Robot software scenarios | **PHYSICALLY VERIFIED** on 5 September 2026: `PARAM TABLES RESULT: PASS` read from a NUCLEO-L496ZG over its debug UART, and every table read across a Holybro link including a string value and a setting written from the ground | Housekeeping now collects a named set in one exchange; the temperature example adds table 51 only when its profile is composed, and it was read off a NUCLEO on 10 September 2026 |
| PARAM CSP adapter | Optional libparam-compatible server/client/cache | Two-node native remote list/get/set plus Robot errors | Holybro RF bench passed production list/get/set and the valid owner callback; the corrected reset-to-default oracle passed physically on 3 September 2026 (`1` to `3`, then invalid `5` restoring the compiled `1`) | Fixed remote descriptor pool; no remote persistence command |
| Parameter persistence | Explicit bounded versioned CRC snapshot and defaults/load/save/clear | CSP-disabled unit suite, cross-process integration, corrupt snapshot fallback, Valgrind | MCUboot bench preserved stored values; no full persistence acceptance matrix | Local only; no migration framework beyond name/type compatibility |
| Storage | LittleFS lifecycle at `/kfsw`, cautious first-format policy, capacity API | Storage ztest and native cross-process integration | NUCLEO storage info/test passes in UART HIL | Linux/NUCLEO full profiles only; one 64 KiB volume per profile |
| K-FSW FTP | LIST/STAT/MKDIR/PUT/GET, 192-byte chunks, CRC, sandbox, atomic `.part` finalization; operation/transfer/transport layering with one transport backend; own-node LIST/STAT/MKDIR served locally | Protocol ztest, native transfers from 0 bytes through 8 KiB, ground-role round trip, Robot workflow | 4 KiB and 16 KiB round trips on NUCLEO UART bench; one 256-byte round trip over the Holybro RF bench with matching CRC and clean counters | K-FSW protocol, not Internet FTP; one server worker/client workspace; PUT/GET need two nodes; no RF throughput characterisation |
| Self-addressing | A node reaches its own address through an ordinary interface, so the source address is applied; registered only when no other interface covers the address | Two-node integration asserts ping and a command to the local node | Ground node 16 and NUCLEO node 2 both register the interface; self-addressed ping and command pass on the Holybro bench | Not registered on a node whose own address sits in another interface's subnet, such as the multi-interface router |
| Command service | Frozen compile-time registry; one definition reached by name from the shell and by numeric identifier over CSP port 11; two-level validation; handlers on a dedicated thread | Registry, duplicate rejection, argument type/count checks, unknown and silent-failure paths in ztest; local and remote invocation in two-node integration | Commands served across the Holybro link from ground node 16 to NUCLEO node 2, including a self-addressed command and an unknown-command rejection | Synchronous only; no accepted-plus-identifier form; no authentication, though the request context reserves the fields; an unknown name is rejected locally before reaching the wire |
| A note across a restart | One small record in memory start-up does not clear, written on the way down and read on the way back up; magic and CRC, checksum last, consumed on read. An STM32L4 voltage detector writes one when the supply dips | 6 ztest cases covering validation, single reporting, last-note-wins, and a SoC without a detector reporting rather than pretending | **PARTIALLY PHYSICALLY VERIFIED** on 5 September 2026: a commanded reboot on a NUCLEO-L496ZG left `commanded ... after 6675 ms`, read back after the restart and recorded as an event; a power cycle correctly left nothing, so the checksum refuses cleared RAM rather than decoding it; the reason, the faulting address and the previous uptime read back across CAN; a withheld watchdog feed reported as `starved` with how long the component had been silent. The brown-out path is armed but **has not been seen firing** | RAM, so a power loss takes it; surviving a dip is the case it is for. Nothing yet writes one on a fault or a withheld watchdog feed |
| Event record | Numeric records in a bounded RAM ring with stable identifier, monotonic timestamp, sequence number, severity and opaque payload; identifiers owned by the producing component; boot, command and FTP emit | Ring wrap and overwrite counting, read-by-age, rejection counting, visitor early stop in ztest | The NUCLEO's record read from the ground node over the Holybro link, counters and one decoded record | RAM only, so it does not survive a reset; no persistent journal, rate limiting, coalescing or downlink stream; payloads are opaque and decoded by ground tooling |
| Shell | Zephyr root commands with history, completion, help, and K-FSW prompt | Native shell/integration/Robot tests | Full NUCLEO shell; FRDM and Pico physical shell bring-up | Debug/operations adapter, not a command authorization service |
| Linux target | Full reference composition on `native_sim/native/64` | Hosted build, unit, integration, Valgrind, Robot | Physical verification not applicable | Simulation does not prove MCU/electrical/timing properties |
| NUCLEO-L496ZG | Full reference composition, flash layout, dual serial paths | Hosted clean build and native-equivalent service tests | Boot/readiness, storage, UART/KISS, CSP, remote PARAM, FTP bench | Optional profiles need their own acceptance; no flight qualification |
| FRDM-K64F | Shell-only target profile | Local build path exists; not in hosted matrix | Prompt, status, version, help physically verified | CSP, PARAM, storage, FTP disabled and unqualified |
| Raspberry Pi Pico W | USB CDC shell-only target profile | Local build path exists; not in hosted matrix | Prompt, status, version, help physically verified | Wi-Fi and full services disabled and unqualified |
| Test/CI | Hosted build, quality, Twister, integration, Valgrind, Robot, Doxygen; Pages deploy from main | Latest reviewed main runs successful | Manual checked-in HIL paths | No coverage threshold, self-hosted HIL, resource locking, or hosted PDF gate |

## Known limits

- `@READY` marks completed startup. It does not aggregate service health.
- Routes are static; automatic link failover and dynamic routing are absent.
- CRC32 detects corruption. Command authentication and encryption are absent.
- Events are held in RAM and lost on reset.
- Parameter migration is limited to snapshot compatibility checks.
- Physical HIL is manually invoked. A new board, radio, or profile needs its
  own acceptance run.
- Timing, stack usage, endurance, long-duration soak, and flight qualification
  remain separate work.

The `boton_test` bench recorded press counts and LED operation on
3 September 2026. Individual press/hold attribution remains pending.

## Housekeeping bench

Housekeeping collects named reports and can forward samples to Yamcs.
The following measurements are from the recorded radio bench run.

**PHYSICALLY VERIFIED** on 10 September 2026, on a NUCLEO-L496ZG running
`54ac87f` with a Holybro SiK pair at 433 MHz and Yamcs 5.13.0:

- the STM32L496 die read 23.214 C over the debug UART, `temp_valid` 1, no
  failed reads, the sample counter advancing on its 1000 ms period. Readings
  move by roughly 327 mC at a time, which is one ADC count at this
  calibration, so the value is a measurement rather than a constant;
- a five-value report collected every 2000 ms across 20 collections with no
  failed collection and no absent value;
- five samples pulled over the radio in one exchange, sequences 21 to 25
  consecutive and two seconds apart. Decoded by hand, sample 25 carried
  23.214 C, matching what the shell had just printed for the same parameter;
- 63 datagrams reached Yamcs and none was invalid. The archive holds 56
  samples spanning 18:47:44 to 18:49:34 UTC with **56 distinct generation
  times**, from 22.559 C to 27.142 C as the die warmed under transmission.
  Distinct times are the point: without the bridge's envelope a pull would
  stamp every sample with one reception instant and the history the node kept
  would collapse into a moment.

The bandwidth claim, measured on the same link against reading the same five
parameters one at a time from a ground node. A round trip on this radio is
220 to 250 ms.

| | Time | For |
| --- | --- | --- |
| Individually, first read of a pass | 9.21 s | 5 values |
| Individually, descriptor list already cached | 1.17 s | 5 values |
| One housekeeping exchange | **0.28 s** | 5 values — or 40 |

With cached descriptors, five individual reads took 1.17 s; one housekeeping
exchange took 0.28 s. The same exchange could return eight five-value samples.
These timings describe this bench and report size.

`tests/hk-yamcs-smoke.sh` compares the frame received by the bridge with the
node's shell output. The bridge forwards it to Yamcs using a database
generated from the report definition.

Beacons are implemented with interval and buffer limits; see @ref ground.
Their physical results must be recorded independently of this polling run.

## Next work

- Automate the existing HIL procedures with hardware locking and recovery.
- Extend fault injection, stack/timing measurements, and soak tests.
- Complete outstanding physical acceptance for optional profiles.

Use repository issues for the current work queue.

## Keeping this page current

When a feature merges:

1. verify the final manifest and target defaults;
2. identify the software and physical tests that actually ran;
3. update the relevant explanatory chapter;
4. adjust one status row without turning the manual into an issue mirror; and
5. keep future work separate from current behavior.

Keep recorded results tied to their image, configuration, and bench.
