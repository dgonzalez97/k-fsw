#!/usr/bin/env python3
"""Exercise the production FTP or FWU-lite path between two native KFSW nodes."""

import argparse
from contextlib import ExitStack
import hashlib
import json
import os
from pathlib import Path
import pickle
import re
import shlex
import shutil
import subprocess
import sys
import tempfile
import time
import zlib

from firmware_fixture import NativeNode

REPO = Path(__file__).resolve().parents[1]
WORKSPACE = REPO.parent


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def configuration(path):
    return dict(line.split('=', 1) for line in path.read_text().splitlines()
                if line.startswith('CONFIG_') and '=' in line)


def stop(process):
    if process.poll() is None:
        process.terminate()
        try:
            process.wait(timeout=3)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()


def build_nodes(work, protocol, version, env):
    station = work / 'ground-station'
    (station / 'nodes').mkdir(parents=True)
    common = '\n'.join((
        'CONFIG_KFSW_FWU=y',
        'CONFIG_KFSW_FWU_STANDARD=' + ('y' if protocol == 'ftp' else 'n'),
        'CONFIG_KFSW_FWU_MCUBOOT=n',
        'CONFIG_KFSW_FWU_SLOT_OFFSET_SECTORS=1',
        'CONFIG_KFSW_FWU_LITE=' + ('n' if protocol == 'ftp' else 'y'),
        'CONFIG_KFSW_FWU_LITE_CSP=' + ('n' if protocol == 'ftp' else 'y'),
        'CONFIG_KFSW_FWU_LITE_BLOCK_SIZE=160' if protocol == 'lite' else '',
        'CONFIG_KFSW_FWU_LITE_TIMEOUT_MS=200' if protocol == 'lite' else '',
        'CONFIG_KFSW_FWU_LITE_BLOCK_RETRIES=2' if protocol == 'lite' else '',
        'CONFIG_KFSW_FWU_LITE_RDP=n' if protocol == 'lite' else '',
    ))
    env = dict(env, KGROUND_STATION_DIR=str(station))
    for name, peer in (('kfsw-gnd-uhf', 19), ('kfsw-ops', 16)):
        config = station / 'nodes' / (name + '.env')
        shutil.copy2(REPO / 'ground-station/nodes' / config.name, config)
        with config.open('a') as stream:
            for key, value in dict(KFSW_CSP_ROUTES=f'{peer} KISS',
                                   KFSW_CSP_VERSION=str(version),
                                   KFSW_EXTRA_KCONFIG=common,
                                   KFSW_EXTRA_OVERLAY=str(REPO / 'tests/support/fwu-native.overlay')).items():
                stream.write(f'{key}={shlex.quote(value)}\n')
        with (work / (name + '-build.log')).open('w') as log:
            result = subprocess.run([str(REPO / 'tools/k-ground'), 'build', name],
                                    env=env, stdout=log, stderr=subprocess.STDOUT)
        require(result.returncode == 0, f'{name} build failed; see {log.name}')
    suffix = '-csp1' if version == 1 else ''
    root = Path(env['KGROUND_BUILD_ROOT'])
    paths = [root / f'{name}-node-{node}{suffix}' for name, node in
             (('kfsw-gnd-uhf', 16), ('kfsw-ops', 19))]
    for path in paths:
        config = configuration(path / 'zephyr/.config')
        require(config.get(f'CONFIG_KFSW_CSP_VERSION_{version}') == 'y', 'Wrong CSP version')
        require(config.get('CONFIG_KFSW_FWU_STANDARD', 'n') == ('y' if protocol == 'ftp' else 'n'),
                'Wrong standard updater composition')
        require(config.get('CONFIG_KFSW_FWU_LITE', 'n') == ('y' if protocol == 'lite' else 'n'),
                'Wrong lite updater composition')
    return paths


def check_flash(build, flash, image):
    sys.path.insert(0, str(WORKSPACE / 'zephyr/scripts/dts/python-devicetree/src'))
    with (build / 'zephyr/edt.pickle').open('rb') as stream:
        edt = pickle.load(stream)
    partition = edt.chosen_node('kfsw,fwu-partition')
    config = configuration(build / 'zephyr/.config')
    sector = partition.parent.parent.props['erase-block-size'].val
    offset = partition.regs[0].addr + sector * int(config['CONFIG_KFSW_FWU_SLOT_OFFSET_SECTORS'])
    actual = flash.read_bytes()[offset:offset + len(image)]
    require(actual == image, 'Target slot differs from the source artifact')
    return hashlib.sha256(actual).hexdigest()


def remote_parameter(node, field, value):
    pattern = rf'16:{field} = {re.escape(value)}(?:\r?\n|$)'
    for attempt in range(3):
        try:
            match = node.command(f'param get 16 {field}', pattern, timeout=5)
            print(match[0].strip())
            return
        except AssertionError:
            if attempt == 2:
                raise


def lite_transfer(source, target, path, size, contacts):
    progress = []
    for attempt in range(16):
        result = source.command(f'fwu_lite send 16 {path}',
                                r'Upload (?:complete|stopped)[^\r\n]*', timeout=90)[0]
        state = target.command('fwu_lite status',
                               r'state: (\w+)[\s\S]*?received: (\d+)[\s\S]*?committed: (\d+)')
        accepted = int(state[2])
        require(not progress or accepted >= progress[-1], 'Resume lost accepted bytes')
        progress.append(accepted)
        if result.startswith('Upload complete'):
            break
        time.sleep(2.1 if contacts else .25)
    else:
        raise AssertionError('Upload did not finish within the bounded contact attempts')
    require(state[1] == 'receiving' and accepted == size, 'Upload implicitly verified or activated')
    source.command('fwu_lite flash 16', r'Image activation failed')
    source.command('fwu_lite validate 16', r'Stored image verified; activation not requested', timeout=30)
    target.command('fwu_lite status', r'state: verified')
    for _ in range(2):
        source.command('fwu_lite flash 16', r'Node 16 image ready; swap scheduled: no', timeout=30)
    target.command('fwu_lite status', r'state: ready')
    if contacts:
        require(any(0 < accepted < size for accepted in progress), 'No partial contact was observed')
        require(len(progress) >= 3, 'Fewer than three contacts exercised')
    return progress


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--protocol', choices=('lite', 'ftp'), required=True)
    parser.add_argument('--lossy', action='store_true')
    parser.add_argument('--contacts', action='store_true')
    args = parser.parse_args()
    require(not args.contacts or args.protocol == 'lite', 'Contact resume belongs to lite')
    version = int(os.environ.get('KFSW_CSP_VERSION', '2'))
    require(version in (1, 2), 'Unsupported CSP version')
    env = dict(os.environ)
    root = Path(env.get('KGROUND_BUILD_ROOT',
                str(Path(env.get('KFSW_OUTPUT_ROOT', WORKSPACE / 'build')) / 'k-ground-fwu')))
    root.mkdir(parents=True, exist_ok=True)
    env['KGROUND_BUILD_ROOT'] = str(root)
    work = Path(tempfile.mkdtemp(prefix=f'{args.protocol}-csp{version}-', dir=root))
    label = 'K-GROUND FWU-' + args.protocol.upper()
    print(f'{label}: evidence {work}', flush=True)
    try:
        with ExitStack() as stack:
            supplied = os.environ.get('KFSW_FWU_TEST_IMAGE')
            if supplied:
                path = Path(supplied).resolve(strict=True)
            else:
                fd, name = tempfile.mkstemp(prefix='.fwu-test-', suffix='.bin', dir=REPO)
                os.close(fd)
                path = Path(name)
                path.write_bytes(bytes((index * 17 + 3) & 255 for index in range(20017)))
                stack.callback(path.unlink)
            image = path.read_bytes()
            require(bool(image), 'Empty test artifact')
            crc = f'{zlib.crc32(image) & 0xffffffff:08x}'
            target_build, source_build = build_nodes(work, args.protocol, version, env)
            source_flash = work / 'source-flash.bin'
            if args.protocol == 'ftp':
                subprocess.run([sys.executable, str(REPO / 'tools/ground/stage-file.py'),
                                '--flash', str(source_flash), '--offset', '0xfc000',
                                '--size', '0x200000', '--flash-size', '0x400000',
                                str(path), '/ftp/build/image.bin'], check=True)
            target = stack.enter_context(NativeNode(target_build / 'zephyr/zephyr.exe', work / 'target'))
            source = stack.enter_context(NativeNode(source_build / 'zephyr/zephyr.exe',
                                                   work / 'source', flash=source_flash))
            stats = work / 'stats.json'
            ready = work / 'bridge.ready'
            if args.protocol == 'lite':
                command = [sys.executable, str(REPO / 'tests/support/fwu-link.py'),
                           '--left', target.device, '--right', source.device,
                           '--stats', str(stats), '--ready-file', str(ready),
                           '--csp-version', str(version)]
                if args.lossy:
                    command.append('--lossy')
                if args.contacts:
                    command.append('--contacts')
            else:
                command = [sys.executable, str(REPO / 'tests/support/lossy-link.py'),
                           '--left', target.device, '--right', source.device,
                           '--drop-every', '3000' if args.lossy else '1000000000',
                           '--drop-bytes', '32', '--ftp-stats', str(stats),
                           '--ready-file', str(ready), '--csp-version', str(version)]
            log = stack.enter_context((work / 'bridge.log').open('w'))
            bridge = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT)
            stack.callback(stop, bridge)
            deadline = time.monotonic() + 10
            while not ready.exists() and bridge.poll() is None and time.monotonic() < deadline:
                time.sleep(.02)
            require(ready.exists(), 'Bridge failed to start')
            source.command('csp ping 16', r'CSP ping 16: success')
            if args.protocol == 'lite':
                progress = lite_transfer(source, target, path, len(image), args.contacts)
                remote_parameter(source, 'fwu_started', '1')
                remote_parameter(source, 'fwu_completed', '1')
                source.command('ftp generate /build/probe.bin 32', r'crc32: [0-9a-f]{8}')
                source.command('ftp put 16 /build/probe.bin firmware.bin',
                               r'FTP put 16 /build/probe.bin -> firmware.bin: PASS')
                source.command('ftp get 16 firmware.bin /build/probe-copy.bin',
                               r'FTP get 16 .*: PASS')
                source.command('ftp verify /build/probe.bin /build/probe-copy.bin',
                               r'FTP verify .*: PASS')
                target.command('fwu_lite status', r'state: ready')
            else:
                progress = []
                source.command('ftp put 16 /build/image.bin firmware.bin',
                               r'FTP put 16 /build/image.bin -> firmware.bin: PASS', timeout=600)
                target.command('fwu status', r'state: ready')
                target.command('ftp stat firmware.bin', r'not found')
            received_sha = check_flash(target_build, target.flash, image)
            remote_parameter(source, 'fwu_received', str(len(image)))
            remote_parameter(source, 'fwu_actual_crc', '0x' + crc)
            remote_parameter(source, 'fwu_expected_crc', '0x' + crc)
            observed = json.loads(stats.read_text())
            block_size = 160 if args.protocol == 'lite' else 192
            require(observed['blocks'] == (len(image) + block_size - 1) // block_size,
                    'Bridge did not observe every image block')
            if args.lossy:
                require(observed['resent'] > 0 and observed['dropped'] > 0, 'Loss did not cause retry')
                if args.protocol == 'lite':
                    require(observed['begin_reply_dropped'] == observed['tail_reply_dropped'] == 1,
                            'BEGIN or final-block acknowledgement loss was not exercised')
            if args.contacts:
                require(observed['contacts'] == 3, 'Three interrupted contacts were not exercised')
            result = dict(protocol=args.protocol, csp_version=version, bytes=len(image), crc32=crc,
                          sha256=received_sha, progress=progress, **observed)
            (work / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
            loss = ' lossy=yes' if args.lossy else ''
            print(f'{label} RESULT: PASS crc32={crc} bytes={len(image)} '
                  f'blocks={observed["blocks"]}{loss} resent={observed["resent"]}')
        return 0
    except Exception as error:
        print(f'{label} RESULT: FAIL {error}\nEvidence: {work}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
