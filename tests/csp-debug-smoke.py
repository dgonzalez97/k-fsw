"""Focused debug controls on supplied hosted images; no build or hardware."""

import argparse
from contextlib import ExitStack
from pathlib import Path
import subprocess
import tempfile

from firmware_fixture import NativeNode

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--flight', required=True, type=Path)
parser.add_argument('--ground', required=True, type=Path)
args = parser.parse_args()

with tempfile.TemporaryDirectory(prefix='kfsw-csp-debug-') as directory, ExitStack() as stack:
    root = Path(directory)
    flight = stack.enter_context(NativeNode(args.flight, root / 'flight'))
    ground = stack.enter_context(NativeNode(args.ground, root / 'ground'))
    bridge_log = stack.enter_context((root / 'bridge.log').open('w'))
    bridge = subprocess.Popen(['socat', f'{flight.device},raw,echo=0',
                               f'{ground.device},raw,echo=0'], stderr=bridge_log)
    stack.callback(bridge.wait, timeout=3)
    stack.callback(bridge.kill)
    node = flight.parameter('node_id')
    ground.command(f'csp ping {node}', rf'CSP ping {node}: success')
    flight_fd = flight.process.stdin.fileno()
    ground_fd = ground.process.stdin.fileno()
    script = r'''
set -euo pipefail
source "$1"
send_flight() { printf '%s\n' "$@" >&FLIGHT_FD; }
send_ground() { printf '%s\n' "$@" >&GROUND_FD; }
for level in 0 1 2; do
    csp_exchange "$2" "$3" send_flight "csp rdp $level"
    csp_reply_has "CSP RDP trace: $level"
    csp_exchange "$2" "$3" send_flight 'csp rdp'
    csp_reply_has "CSP RDP trace: $level"
done
for invalid in 256 -1 text; do
    csp_refusal_check "$2" "$3" send_flight "csp rdp $invalid" 'csp rdp' \
        'level is 0 off, 1 errors, 2 with the protocol' \
        "CSP RDP trace: $invalid" 'CSP RDP trace: 2'
done
for value in 0 1; do
    csp_exchange "$4" "$5" send_ground "param set $6 packet_trace $value"
    csp_reply_has "$6:packet_trace = $value"
    csp_exchange "$4" "$5" send_ground "param get $6 packet_trace"
    csp_reply_has "$6:packet_trace = $value"
done
csp_refusal_check "$4" "$5" send_ground \
    "param set $6 packet_trace 2" "param get $6 packet_trace" \
    "param set: node $6 did not answer" "$6:packet_trace = 2" "$6:packet_trace = 1"
csp_exchange "$4" "$5" send_ground "param set $6 log_level 3"
csp_reply_has "$6:log_level = 3"
csp_refusal_check "$4" "$5" send_ground \
    "param set $6 log_level 5" "param get $6 log_level" \
    "param set: node $6 did not answer" "$6:log_level = 5" "$6:log_level = 3"
'''.replace('FLIGHT_FD', str(flight_fd)).replace('GROUND_FD', str(ground_fd))
    helper = Path(__file__).resolve().parent / 'support/csp-debug-checks.sh'
    try:
        subprocess.run(['bash', '-c', script, 'csp-debug', str(helper), str(flight.log),
                        str(flight.process.pid), str(ground.log), str(ground.process.pid),
                        str(node)], pass_fds=(flight_fd, ground_fd), check=True)
    except BaseException:
        print(flight.log.read_text())
        print(ground.log.read_text())
        raise
print('CSP debug hosted/PTY: PASS (software only)')
