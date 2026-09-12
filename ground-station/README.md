# K-FSW reference ground station

This directory is a small, version-controlled deployment example. Each node
file selects one K-FSW Linux role, CSP address, and direct demo peer. It does
not contain reusable drivers, services, orchestration, or a routing daemon.

The assigned prototype roles are:

| Role | CSP node | Current scope |
| --- | --- | --- |
| `kfsw-gnd-uhf` | 16 | Own the Holybro UHF interface |
| `kfsw-ops` | 19 | Operator-facing shell node |

## yamcs

`yamcs/` is a submodule holding
[the mission control system](https://github.com/dgonzalez97/kfsw-yamcs), a fork
of `yamcs/quickstart` that keeps what housekeeping brings down. Run it with
`./mvnw yamcs:run` from that directory and open <http://localhost:8090>.

`tools/ground/hk-bridge.py` pulls housekeeping over CSP/KISS and forwards
frames to Yamcs over UDP. Use `--listen` to receive configured beacons.

`tools/ground/hk-report.py` generates the node's report definition and Yamcs
XTCE from the files in `reports/`, keeping field order and decoding aligned.

Addresses 17 and 18 are reserved for future ground roles and have no node files.

Run `tools/k-ground init` from a mission workspace to copy this configuration
into a local `ground-station/` directory. `KGROUND_STATION_DIR` can select a
different deployment explicitly.


- activate the workspace `.venv` for direct `west` commands;
- let `tools/k-ground` load the selected `nodes/*.env` file automatically; and
- source a separate exported bench file for host-specific paths.

Keep physical serial paths and CAN interface names in the bench environment.
See [ground setup](../docs/ground/index.md) for the procedure.

An installation can set `KFSW_CSP_ROUTES` in a node file.

```sh
KFSW_CSP_ROUTES='2/14 KISS,16/10 KISS 2'
```

Omitting it preserves the direct `0/0 KISS` route.

Loopback handles self-addressed traffic. Use a real link in the default route
to reach other nodes.
