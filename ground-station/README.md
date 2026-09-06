# K-FSW reference ground station

This directory is a small, version-controlled deployment example. Each node
file selects one K-FSW Linux role, CSP address, and direct demo peer. It does
not contain reusable drivers, services, orchestration, or a routing daemon.

The assigned prototype roles are:

| Role | CSP node | Current scope |
| --- | --- | --- |
| `kfsw-gnd-uhf` | 16 | Own the Holybro UHF interface |
| `kfsw-rotctl` | 17 | Reserved antenna-control bridge | @dd add a todo here to create a rotctl daemon controller, that reads info from gpredic, add that as a todo in the issues
| `kfsw-beacon` | 18 | Reserved beacon handler |  @dd todo with the housekeeping, to save everything in a database and then
| `kfsw-ops` | 19 | Operator-facing shell node |

Run `tools/k-ground init` from a mission workspace to copy this configuration
into a local `ground-station/` directory. `KGROUND_STATION_DIR` can select a
different deployment explicitly.


- activate the workspace `.venv` for direct `west` commands;
- let `tools/k-ground` load the selected `nodes/*.env` file automatically; and
- source a separate exported bench file for host-specific USB/serial paths. @dd maybe here is also can? on easy to set kind of stuff to ease everything?

Do not add physical device paths to these reusable node files. See the ground
composition guide for the complete setup and verification procedure.

An installation can set `KFSW_CSP_ROUTES` in a node file.

```sh
KFSW_CSP_ROUTES='2/14 KISS,16/10 KISS 2'
```

Omitting it preserves the legacy direct `0/0 -> KISS` route. @dd maybe i would like to set loop by default
