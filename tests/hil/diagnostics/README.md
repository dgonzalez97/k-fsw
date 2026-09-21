# HK and CSP diagnostics

Physical execution is pending. The same fixture runs against native_sim without
devices. It checks two HK reports, capture/replay byte equality, a failed poll,
interface statistics, and optionally the CSP tools ping, PCAP capture, remote
text logs and `neighbors` inventory. The host-tool fixture checks repeated
reads, severity filters, ring overwrite and deadline/partial-file handling.

Build the Linux image and optional host tool, then run from `k-fsw`:

```bash
tools/kfsw-linux build
tools/kfsw-linux csp-tools build
tools/kfsw-linux diagnostics ../build/diagnostics/my-run
```

Use a new output directory for each run. Logs, JSONL captures, PCAP and the
native flash file are kept there. The fixture creates its own Linux process.

For the later bench run, use a test image with HK, parameter tables 1 and 3,
valid wall time, a KISS link and `config/profiles/log-history.conf`. Use the
default log level (info), module levels, and a history depth of 32. Addresses
100 and 101 must be unused; source address 16 is the host's. The fixture
emits 39 log-test messages, overwriting the older RAM log history. It also
replaces report definitions 0
and 1, enables their periods/beacons, then disables their periods/beacons on
success. HK autosave persists these test settings. Use a dedicated bench
configuration and restore its reports afterwards, including after a failure.

Set the shell UART and separate CSP UART explicitly. A radio link can be used
for the latter; set its baud rate. No flashing or device discovery is performed.

```bash
export KFSW_DIAGNOSTICS_SHELL=/dev/serial/by-id/your-console
export KFSW_DIAGNOSTICS_KISS=/dev/serial/by-id/your-csp-link
export KFSW_DIAGNOSTICS_NODE=2
export KFSW_DIAGNOSTICS_BAUD=115200
export KFSW_CSP_TOOLS_TEST_BINARY="$PWD/../tools/kfsw-csp-tools/target/release/csp-kiss"
KFSW_ROBOT_OUT_DIR="$PWD/../build/robot/diagnostics-bench-1" \
  tests/hil/run.sh --include diagnosticsANDphysical
```

The ping issued during passive capture has no answering ground node and times
out on purpose; its outgoing packet is what the PCAP check inspects. A
missing interface also times out because CMP has no not-found reply.

For host protocol fault tests without a node:

```bash
KFSW_CSP_TOOLS_TEST_BINARY="$PWD/../tools/kfsw-csp-tools/target/release/csp-kiss" \
  ../.venv/bin/python -m unittest discover -s tests/ground -v
```

These use private PTYs to simulate multiple peers, missing/invalid identities,
old log nonces, dropped/duplicate/truncated records and an absent end reply.
