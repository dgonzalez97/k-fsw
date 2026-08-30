# K-FSW reference ground station

This directory is a small, version-controlled deployment example. Each node
file selects one K-FSW Linux role, CSP address, and direct demo peer. It does
not contain reusable drivers, services, orchestration, or a routing daemon.

The assigned prototype roles are:

| Role | CSP node | Current scope |
| --- | --- | --- |
| `kfsw-gnd-uhf` | 16 | Own the Holybro UHF interface |
| `kfsw-rotctl` | 17 | Reserved antenna-control bridge |
| `kfsw-beacon` | 18 | Reserved beacon handler |
| `kfsw-ops` | 19 | Operator-facing shell node |

Run `tools/k-ground init` from a mission workspace to copy this configuration
into a local `ground-station/` directory. `KGROUND_STATION_DIR` can select a
different deployment explicitly.

The environment boundaries are intentional:

- activate the workspace `.venv` for direct `west` commands;
- let `tools/k-ground` load the selected `nodes/*.env` file automatically; and
- source a separate exported bench file for host-specific USB/serial paths.

Do not add physical device paths to these reusable node files. See the ground
composition guide for the complete setup and verification procedure.
