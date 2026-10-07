# Memory footprint {#footprint}

[TOC]

Where the flash and the RAM of the NUCLEO-L496ZG image go, per directory,
file and symbol. The reports come from Zephyr's `rom_report` and `ram_report`
build targets and are drawn as sunbursts: click a ring to zoom into it.

- <a href="footprint/rom.html">Flash (ROM) sunburst</a>
- <a href="footprint/ram.html">RAM sunburst</a>
- The same trees as text: <a href="footprint/rom.txt">rom.txt</a>,
  <a href="footprint/ram.txt">ram.txt</a>

They measure the NUCLEO because it is the flight MCU composition. A
`native_sim` image is a host program, so its sizes say nothing about a board.
RAM is the tighter of the two there.

## Running them

From the workspace root, with plotly in the virtual environment
(`./.venv/bin/pip install plotly`):

```bash
./k-fsw/tools/ci/footprint.sh
```

It builds the NUCLEO and writes `build/footprint/{rom,ram}.{html,txt}`.
`KFSW_FOOTPRINT_TARGET` measures another target. For one report only:

```bash
west build -d build/nucleo_l496zg -t ram_report
```

## puncover

[puncover](https://github.com/HBehrens/puncover) adds call graphs and stack use
per function. It runs as a local web server rather than producing a page, so
it is not published here:

```bash
./.venv/bin/pip install puncover
west build -d build/nucleo_l496zg -t puncover
```

Then open the address it prints. See Zephyr's
[optimization tools](https://docs.zephyrproject.org/4.4.0/develop/optimizations/tools.html).
