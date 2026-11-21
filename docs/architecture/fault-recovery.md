# Fault detection and recovery {#fault_recovery}

[TOC]

K-FSW has no single fault service. Each mechanism watches one thing and does
one thing when it goes wrong, and most only tell the ground. The ones that act
escalate in a fixed order: report, then a command the operator wrote in
advance, then a reset, and after enough unexplained resets, the recovery
image. @ref services describes each one.

```mermaid
flowchart TD
    HEALTH["Health: a component missed its deadline"]
    FATAL["Fatal error handler"]
    SUPPLY["Supply detector: brownout"]
    GNDWDT["Ground watchdog: no feed in time"]
    LIMITS["Limits: a parameter crossed a bound"]
    MONITORS["Resmon and scrub: stack or image changed"]
    NOTE["Last words note in retained RAM"]
    EVENT["Event and log for the ground"]
    ACTION["Command released through FBO"]
    IWDG["Hardware watchdog reset"]
    REBOOT["Software reboot"]
    BOOT["Next boot: reset cause, note, boot attempts"]
    GOLDEN["Golden installed"]

    MONITORS --> EVENT
    LIMITS --> EVENT
    LIMITS --> ACTION
    HEALTH --> NOTE
    HEALTH -- "stops feeding" --> IWDG
    FATAL --> NOTE
    FATAL -- "CPU halts" --> IWDG
    SUPPLY --> NOTE
    SUPPLY -- "brownout reset" --> BOOT
    GNDWDT --> REBOOT
    IWDG --> BOOT
    REBOOT --> BOOT
    BOOT -- "no attempts left" --> GOLDEN
```

## Watching

Two monitors only report. The resource monitor (`KFSW_RESMON`) sweeps the
kernel's thread list and raises an event when a thread's stack use reaches
the alert level. The image scrub (`KFSW_SCRUB`) re-reads the image slots and
the Golden partition, compares a CRC32 against a reference the operator
learned, and raises an event when one changes. Neither changes anything on the
node. An operator reads the events, or a limit acts on the parameters they
publish.

## Acting on a value

Limits (`KFSW_LIMITS`) watch numeric parameters. A rule names a parameter, a
comparison, a bound, how many consecutive sweeps it must hold, and a command
to release. A rule that fires records a `limits` event, and the command goes
through the file based operations scheduler, so the release is logged and can
address another node. This is where a mission
writes its local recovery: switch off a load, change a mode, restart a
subsystem. The rule file is uploaded like any other file, and nothing in the
image has to change.

## Resetting

Three mechanisms reset the node, each for a different question.

Health (`KFSW_HEALTH`) asks whether the threads still run. Each watched
component has a deadline: the main loop (`app`) and a loopback ping through
the CSP router (`csp`) in the reference build. While all of them report in
time, health feeds the hardware watchdog. When one misses, health writes a
`starved` note and stops feeding, and the watchdog resets the MCU. On the
NUCLEO that is the independent watchdog with an 8 s timeout. `main()` arms
it before anything else, and a keep-alive on the system workqueue feeds it
until health takes over at the end of startup.

The ground watchdog (`KFSW_GNDWDT`) asks whether anybody still talks to the
node. Only a feed command from the ground restarts its countdown, 24 hours by
default. At zero it reboots the node, on the assumption that a node nobody
has reached for a day has lost its way and should start again from a known
state.
The Golden recovery image runs it too, with the same timeout: its only
other supervision is a keep-alive that feeds the hardware watchdog while the
scheduler runs, which a wedged link does not stop.

A fatal error writes the faulting address into the last words note
(`app/src/fatal.c`) and halts the CPU, and the hardware watchdog resets it.
A fault before `main()` arms the watchdog has nothing to reset it.

## Across a reset

The last words note is a small record in RAM that a reset does not clear. The
code that causes a reset writes why: `commanded` from a reboot command,
`starved` from health, `fatal` with the faulting address, `brownout` from the
supply detector where the board has one. At the next boot,
`kfsw-services/src/boot.c` reads it together with the hardware reset cause,
and table 32 shows both (`last_reason`, `last_detail`, `last_uptime_ms`,
`boot_reset_cause`). A power cut clears the RAM, so no note means either a
power loss or a reset nothing explained.

With `CONFIG_KFSW_BOOT_BUDGET`, every reset that is not a commanded reboot
spends one boot attempt before `main()`. A run of resets the node cannot
explain therefore ends with the Golden recovery image installed, after the
bootloader has had the chance to revert an unconfirmed update. A run that
stays healthy for `CONFIG_KFSW_BOOT_BUDGET_STABLE_S` refills the count, so
only startups that never became stable add up.

A refused Golden ends the chain. The install checks the recovery image's own
SHA-256 before it erases the secondary slot, so a damaged one is refused with
the slot untouched; `tests/unit/services_golden` covers that case as
`test_damaged_image_refused_before_the_slot_erase`. The node then keeps running
the image that was failing to confirm, with no attempts left to spend and
nothing further to try on its own.

That is not a dead node. The flight image is still running and still
commandable, so the ground can upload a new one and recover the node over the
link. What the ground cannot repair is the golden partition itself. Nothing in
the node ever writes it: the service only reads it, to check its digest, to
copy it into the secondary slot, and for the scrub to compare a CRC32. It is
written by a programmer, through `tools/golden.sh flash`, and by nothing else.
So a damaged recovery image stays damaged until the board is in front of
someone, and the scrub is how an operator learns that before it matters.

Neither of those mechanisms exists in the recovery image: `KFSW_BOOT_BUDGET`
depends on `!KFSW_GOLDEN_IMAGE`, so Golden compiles neither the spend at
startup nor the stable-run refill. Golden spends no attempts and refills the
record once at its own startup, which is the retry for an install-time refill
that failed.
@ref firmware_update has the sequence.

The persistent journal (`KFSW_JOURNAL`) keeps boot reports and events of
warning severity and above in flash, and the trial diagnostics in `/kfsw/boot-trial.dat` count
the unconfirmed boots of the running image. A ground station reads both at
the next pass to learn what happened while nobody was listening.

## What is not here

There is no central fault manager and no spacecraft mode. A mission that
needs one builds it on these pieces: the events and parameters say what is
wrong, limits and procedures act on them, and the resets are the last line.
