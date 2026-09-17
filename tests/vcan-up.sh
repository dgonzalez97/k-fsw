#!/usr/bin/env bash
# Bring up a virtual CAN interface for the software CAN tests. Needs root;
# everything else runs as a normal user. Nothing physical is involved: vcan is
# a kernel interface that carries frames between processes on this host.
set -euo pipefail

interface="${1:-vcan0}"

if [[ "$(id -u)" -ne 0 ]]; then
	echo "ERROR: run this with sudo: sudo $0 $interface" >&2
	exit 1
fi

modprobe vcan 2>/dev/null || true

if ip link show "$interface" >/dev/null 2>&1; then
	ip link set "$interface" up
else
	ip link add dev "$interface" type vcan
	ip link set "$interface" up
fi

echo "$interface is up"
ip -br link show "$interface"
