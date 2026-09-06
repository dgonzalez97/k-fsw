# K-FSW reference ground station

This directory is a small, version-controlled deployment example. Each node
file selects one K-FSW Linux role, CSP address, and direct demo peer. It does
not contain reusable drivers, services, orchestration, or a routing daemon.

The assigned prototype roles are:

| Role | CSP node | Current scope |
| --- | --- | --- |
| `kfsw-gnd-uhf` | 16 | Own the Holybro UHF interface |
| `kfsw-rotctl` | 17 | Reserved: an antenna bridge driven from Gpredict ([#79](https://github.com/dgonzalez97/k-fsw/issues/79)) |
| `kfsw-beacon` | 18 | Reserved: keep housekeeping samples so a pass can be read after it ends ([#80](https://github.com/dgonzalez97/k-fsw/issues/80)) |
| `kfsw-ops` | 19 | Operator-facing shell node |

Run `tools/k-ground init` from a mission workspace to copy this configuration
into a local `ground-station/` directory. `KGROUND_STATION_DIR` can select a
different deployment explicitly.


- activate the workspace `.venv` for direct `west` commands;
- let `tools/k-ground` load the selected `nodes/*.env` file automatically; and
- source a separate exported bench file for host-specific paths.

The bench file is where CAN belongs too. An adapter comes up as `can0` on one
machine and `can1` on the next, and bringing it up needs `sudo`, so the
interface name is exactly the kind of thing that must not live in a node file
everyone shares. `tests/hil/stm32/nucleo-l496zg/can-bench.env` is the worked
example.

Do not add physical device paths to these reusable node files. See the ground
composition guide for the complete setup and verification procedure.

An installation can set `KFSW_CSP_ROUTES` in a node file.

```sh
KFSW_CSP_ROUTES='2/14 KISS,16/10 KISS 2'
```

Omitting it preserves the direct `0/0 KISS` route.

Defaulting to loopback instead was considered and left alone. A node can
already reach itself — the loopback interface is registered with the node's own
address whatever the table says — so the only thing `0/0 LOOP` would change is
what happens to traffic for *other* nodes, and libcsp drops what loopback is
not addressed. A misconfigured node would go quiet instead of reporting a
transmit error, which is the harder fault to find.
