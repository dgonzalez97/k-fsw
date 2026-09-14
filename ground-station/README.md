# K-FSW reference ground station

An example ground station configuration. Each node file selects a K-FSW Linux
role, its CSP address and its peer. There are no drivers or services here.

| Role | CSP node | Use |
| --- | --- | --- |
| `kfsw-gnd-uhf` | 16 | Opens the Holybro UHF radio |
| `kfsw-gnd-uhf-bench` | 16 | UHF gateway routed to flight node 2 |
| `kfsw-gnd-can` | 16 | Reaches a flight node over CAN |
| `kfsw-ops` | 19 | Operator shell |

## yamcs

`yamcs/` is a submodule with the
[mission control configuration](https://github.com/dgonzalez97/kfsw-yamcs),
based on `yamcs/quickstart`. Run it with `./mvnw yamcs:run` from that directory
and open <http://localhost:8090>.

`tools/ground/hk-bridge.py` pulls housekeeping over CSP/KISS and sends the
frames to Yamcs over UDP. Use `--listen` to receive beacons.

`tools/ground/hk-report.py` generates the node's report definition and the
Yamcs XTCE from the files in `reports/`, so both use the same field order.

## Setup

Run `tools/k-ground init` in a workspace to copy this directory to
`ground-station/`. `KGROUND_STATION_DIR` selects a different one.

- Activate the workspace `.venv` for plain `west` commands.
- `tools/k-ground` loads the selected `nodes/*.env` file.
- Put serial paths and CAN interface names for your host in a separate bench
  file and source it.

See [ground setup](../docs/ground/index.md) for the full procedure.

A node file can set a route table:

```sh
KFSW_CSP_ROUTES='2/14 KISS,16/10 KISS 2'
```

Without it the node uses the direct `0/0 KISS` route. Traffic to the node's
own address goes through loopback.
