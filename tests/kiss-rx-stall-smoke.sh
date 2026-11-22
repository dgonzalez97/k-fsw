#!/usr/bin/env bash
set -Eeuo pipefail

source "$(dirname "$0")/../tools/_common.sh" linux

link=kiss
if [[ "${1:-}" == --can ]]; then
	link=can
	shift
fi
[[ $# -eq 0 ]] || { echo "ERROR: unexpected argument: $1"; exit 2; }
interface="${KFSW_CAN_INTERFACE:-vcan0}"
if [[ "$link" == can ]] && ! ip link show "$interface" >/dev/null 2>&1; then
	echo "CAN RX STALL RESULT: NOT RUN ($interface unavailable)"
	exit 0
fi

work_dir="$(mktemp -d /tmp/kfsw-rx-stall.XXXXXX)"
trap 'rm -rf -- "$work_dir"' EXIT
build_dir="${KFSW_OUTPUT_ROOT:-$KFSW_ROOT/build}/tests/linux-$link-rx-stall"
confs="$KFSW_REPO_DIR/tests/config/linux-rx-stall.conf"
overlays="$KFSW_REPO_DIR/tests/config/linux-watchdog.overlay"
if [[ "$link" == can ]]; then
	confs+=";$KFSW_REPO_DIR/tests/config/linux-can.conf"
	overlays+=";$KFSW_REPO_DIR/tests/config/linux-can.overlay"
	"$KFSW_REPO_DIR/tools/k-ground" build kfsw-gnd-can
fi
KFSW_BUILD_DIR="$build_dir" KFSW_EXTRA_CONF_FILE="$confs" \
	KFSW_EXTRA_DTC_OVERLAY_FILE="$overlays" "$KFSW_REPO_DIR/tools/build.sh" linux

python3 - "$build_dir" "$work_dir" "$link" "$interface" \
	"${KFSW_OUTPUT_ROOT:-$KFSW_ROOT/build}/k-ground/kfsw-gnd-can-node-17" <<'PYTEST'
from pathlib import Path
import re
import subprocess
import sys
import time

build, work, link, interface, ground = sys.argv[1:]
work = Path(work)
processes = []
logs = []


def launch(directory, name, can=False):
    log = (work / (name + '.log')).open('w')
    logs.append(log)
    args = [str(Path(directory) / 'zephyr/zephyr.exe'), '--uart_stdinout', '--no-color',
            '-flash=' + str(work / (name + '.bin'))]
    if can:
        args.append('--can-if=' + interface)
    process = subprocess.Popen(args, stdin=subprocess.PIPE, stdout=log, stderr=log, text=True)
    processes.append(process)
    return process, work / (name + '.log')


def wait_until(predicate, process, timeout=10):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        if predicate():
            return
        assert process.poll() is None, 'node exited unexpectedly'
        time.sleep(0.02)
    raise AssertionError('timed out waiting for node output')


def exchange(process, log, command):
    offset = len(log.read_text())
    process.stdin.write(command + '\nstatus\n')
    process.stdin.flush()
    # The existing smoke harness uses status as an ordered reply boundary.
    wait_until(lambda: 'K-FSW status' in log.read_text()[offset:],
               process)
    return re.sub(r'\x1b\[[0-9;]*[A-Za-z]', '', log.read_text()[offset:]).replace('\r', '')


def status(process, log):
    reply = exchange(process, log, 'health status')
    feeds = re.search(r'^feeds: (\d+)', reply, re.M)
    assert feeds, reply
    return reply, int(feeds[1])


def traffic(node, node_log, peer, peer_log):
    assert 'success' in exchange(node, node_log, 'csp ping 17')
    assert 'success' in exchange(peer, peer_log, 'csp ping 2')
    for process, log in ((node, node_log), (peer, peer_log)):
        print(exchange(process, log, 'csp routes'))
        print(exchange(process, log, 'csp interfaces'))


try:
    node, node_log = launch(build, 'node', link == 'can')
    peer = peer_log = None
    if link == 'can':
        peer, peer_log = launch(ground, 'peer', True)
        wait_until(lambda: '@READY ' in peer_log.read_text(), peer)
    wait_until(lambda: '@READY ' in node_log.read_text(), node)
    if peer:
        traffic(node, node_log, peer, peer_log)
    wait_until(lambda: 'feeding: yes' in status(node, node_log)[0], node)
    initial, first_feeds = status(node, node_log)
    assert 'state: ok' in initial and 'feeding: yes' in initial, initial
    # More than three 2000 ms deadlines without any traffic or peer activity.
    time.sleep(6.3)
    quiet, quiet_feeds = status(node, node_log)
    assert 'state: ok' in quiet and 'feeding: yes' in quiet, quiet
    assert quiet_feeds > first_feeds, (first_feeds, quiet_feeds)
    assert node_log.read_text().count('@BOOT ') == 1, 'quiet link caused a reset'
    print('QUIET: PASS feeds', first_feeds, '->', quiet_feeds)

    listing = exchange(node, node_log, 'kernel thread list')
    name = 'csp_rx_CAN' if link == 'can' else 'csp_rx_uart_1'
    thread = re.search(r'(0x[0-9a-fA-F]+)\s+' + re.escape(name) + r'\b', listing)
    assert thread, listing
    print(exchange(node, node_log, 'kernel thread suspend ' + thread[1]))
    # Sample progress after suspension: the deadline is from the last report,
    # not the suspension command. Allow one receiver sample and one health check.
    info_command = 'comms ' + ('can' if link == 'can' else 'uart') + ' info'
    info_before = exchange(node, node_log, info_command)
    time.sleep(0.3)
    info_after = exchange(node, node_log, info_command)
    passes = lambda text: re.search(r'rx_passes: (\d+)', text)[1]
    assert passes(info_before) == passes(info_after), 'suspended receiver still advanced'
    print('RX PROGRESS: FROZEN', passes(info_after))
    time.sleep(2.1)
    fault, fault_feeds = status(node, node_log)
    assert 'feeding: no' in fault and 'faulted_by: ' + link in fault, fault
    time.sleep(0.3)
    again, again_feeds = status(node, node_log)
    assert 'feeding: no' in again and fault_feeds == again_feeds, (fault, again)
    print('FEED WITHHOLDING: PASS', fault.strip())
    if peer:
        assert 'success' not in exchange(peer, peer_log, 'csp ping 2')

    try:
        wait_until(lambda: node_log.read_text().count('@READY ') >= 2, node, timeout=6)
    except AssertionError:
        if node.poll() is not None:
            raise
        print('HOSTED WATCHDOG RESET: NOT VERIFIED (no reset observed)')
        print(exchange(node, node_log, 'kernel thread resume ' + thread[1]))
        time.sleep(1.3)
        recovered, _ = status(node, node_log)
        assert 'state: ok' in recovered and 'feeding: yes' in recovered, recovered
    else:
        print('HOSTED WATCHDOG RESET: PASS (second @BOOT and @READY)')
        assert node_log.read_text().count('@BOOT ') >= 2
    if peer:
        traffic(node, node_log, peer, peer_log)
    for line in node_log.read_text().splitlines():
        if line.startswith(('@BOOT ', '@READY ')):
            print(line)
    print(link.upper() + ' RX STALL RESULT: PASS (software only)')
except Exception:
    for path in work.glob('*.log'):
        print(path.name, path.read_text())
    raise
finally:
    for process in processes:
        if process.poll() is None:
            process.terminate()
        try:
            process.wait(timeout=3)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()
    for log in logs:
        log.close()
PYTEST
