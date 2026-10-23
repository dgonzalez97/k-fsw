#!/usr/bin/env python3
"""Build the four updater compositions and check their actual shell commands."""

import os
from pathlib import Path
import subprocess
import sys

from firmware_fixture import NativeNode

REPO = Path(__file__).resolve().parents[1]


def main():
    root = Path(os.environ.get('KFSW_OUTPUT_ROOT', REPO.parent / 'build')) / 'fwu-composition'
    root.mkdir(parents=True, exist_ok=True)
    for name, standard, lite in [('normal', True, False), ('lite', False, True),
                                 ('both', True, True), ('neither', False, False)]:
        directory = root / name
        directory.mkdir(exist_ok=True)
        config = directory / 'frontend.conf'
        config.write_text('\n'.join((
            'CONFIG_KFSW_FWU=y', 'CONFIG_KFSW_FWU_MCUBOOT=n',
            'CONFIG_KFSW_FWU_STANDARD=' + ('y' if standard else 'n'),
            'CONFIG_KFSW_FWU_LITE=' + ('y' if lite else 'n'),
            'CONFIG_KFSW_FWU_LITE_CSP=' + ('y' if lite else 'n'),
        )) + '\n')
        build = directory / 'build'
        env = dict(os.environ, KFSW_BUILD_DIR=str(build), KFSW_EXTRA_CONF_FILE=str(config))
        with (directory / 'build.log').open('w') as log:
            result = subprocess.run([str(REPO / 'tools/build.sh'), 'linux'],
                                    env=env, stdout=log, stderr=subprocess.STDOUT)
        if result.returncode:
            raise AssertionError(f'{name} build failed; see {log.name}')
        warning = 'No firmware update front end is enabled' in Path(log.name).read_text()
        if warning != (not standard and not lite):
            raise AssertionError(f'{name}: updater warning disagrees with selected front ends')
        run = directory / 'run'
        if run.exists():
            index = 1
            while (directory / f'run-{index}').exists():
                index += 1
            run = directory / f'run-{index}'
        with NativeNode(build / 'zephyr/zephyr.exe', run) as node:
            node.command('fwu status', 'K-FSW firmware update' if standard else 'fwu: command not found')
            node.command('fwu_lite status', 'state: idle' if lite else 'fwu_lite: command not found')
            if lite:
                node.command('fwu_lite status 65536', 'Node must be')
                node.command('fwu_lite send 0 /missing.bin', 'Node must be')
                node.command('fwu_lite flash 4294967296', 'Node must be')
            if standard:
                node.command('fwu begin -1 0', 'Invalid size')
                node.command('fwu begin 4294967296 0', 'Invalid size')
        print(f'FWU COMPOSITION: {name} PASS', flush=True)
    return 0


if __name__ == '__main__':
    try:
        sys.exit(main())
    except (OSError, AssertionError) as error:
        sys.exit(f'FWU COMPOSITION: FAIL {error}')
