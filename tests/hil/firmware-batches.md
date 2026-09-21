# Firmware batch checks

Run native cases with `tests/hil/run.sh --include firmware-batchesANDsoftware`
and a new `KFSW_ROBOT_OUT_DIR`. Build `tools/kfsw-linux build` first. Each case
has its own native node, private flash and PTY.

Physical cases are prepared and have not been run. They require a
dedicated image with the selected feature, an idle command service and explicit
`KFSW_DIAGNOSTICS_SHELL`, `KFSW_DIAGNOSTICS_KISS`, `KFSW_DIAGNOSTICS_NODE` and
`KFSW_DIAGNOSTICS_BAUD`. Do not run other readers of the KISS link. Sources 30
and 31 are reserved for the fixture and must not be assigned on the bench.
No case flashes or discovers a device automatically.

The command retry case needs `CONFIG_KFSW_COMMAND_RETRY=y` and working entropy
on the board. It reserves a noop, executes it twice with the same ticket,
checks that `cmd_invoked` increases only once, rejects a changed command and
wrong source, then checks legacy invocation. It uses one of the default eight
cache slots for 60 seconds. The native case also emulates a remote peer
to drop a prepare reply and a result, and checks that the client reuses its
nonce/ticket. A simulated legacy peer verifies that no fallback command runs.
Physical execution selects `command-retryANDphysical`; it runs only the
server-side checks and does not change the board's command timeout.
