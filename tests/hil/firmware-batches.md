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

The UTC procedure case needs FBO, CSP clock set/get, and a valid writable clock.
The software case stages its private flash before starting a node. For the
physical case, upload `tests/procedures/utc-wait.txt` to
`/kfsw/ftp/procedures/utc-wait.txt` on a dedicated test image first. Select
`fbo-utcANDphysical`. The fixture explicitly sets UTC forward/backward and
below the valid-clock floor, so keep periodic or scheduled work disabled on
that bench image. It restores the original clock plus elapsed host seconds on
exit (or restores an unset state if initially unset); it cannot restore time
if the bench disconnects. It verifies a due step, rejection of a late step,
cancellation, and an unset clock without invoking the following noop.

The journal software case starts a node, records an intentionally unknown
command as a warning, waits for it to be committed, then starts a second
process on the same flash. It checks the previous record byte-for-byte through
local commands and CSP, including its original time, and a new boot identity.

The physical `journalANDphysical` case requires `CONFIG_KFSW_JOURNAL=y` and
storage. It adds an unknown-command event and checks local/CSP retrieval. It
does not reset or power-cycle the board. For the later persistence check, save
the returned `boot=N`, reset the dedicated bench image on purpose, and run
`tests/journal-smoke.py` with the explicit `--serial`, `--kiss-device`, `--node`
and `--expected-previous-boot N` arguments and a new output directory. Qualify
power cuts during record writes and filesystem sync separately. The fixture
waits for `queued=0` before calling a record committed; unwritten queue entries
are not expected to survive. Keep other event-producing tests idle so the
expected record is not displaced while it is being checked.
