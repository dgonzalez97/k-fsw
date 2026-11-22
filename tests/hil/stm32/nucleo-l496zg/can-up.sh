#!/usr/bin/env bash
# Bring the PCAN-USB adapter up. Needs root; everything else runs as a normal user.
set -euo pipefail

case "${1:-500000}" in
125000 | 250000 | 500000 | 1000000) ;;
*) echo "unsupported bitrate: ${1:-}" >&2; exit 2 ;;
esac

bitrate="${1:-500000}"
txqueuelen=1000
mode="${2:-normal}"          # normal | listen-only | loopback | down

ip link set can0 down 2>/dev/null || true

if [[ "$mode" == "down" ]]; then
	# A case that needs nobody acknowledging takes the bus away deliberately.
	echo "can0 down"
	exit 0
fi

case "$mode" in
listen-only) opts=(listen-only on loopback off) ;;
loopback)    opts=(loopback on listen-only off) ;;
normal)      opts=(listen-only off loopback off) ;;
*) echo "unknown mode: $mode" >&2; exit 2 ;;
esac

ip link set can0 type can bitrate "$bitrate" "${opts[@]}" restart-ms 100
ip link set can0 txqueuelen "$txqueuelen"
actual_txqueuelen="$(cat /sys/class/net/can0/tx_queue_len)"
if [[ "$actual_txqueuelen" != "$txqueuelen" ]]; then
	echo "can0 txqueuelen is $actual_txqueuelen; expected $txqueuelen" >&2
	exit 1
fi
ip link set can0 up
echo "can0 up at ${bitrate} bps, ${mode}, txqueuelen ${txqueuelen}"
ip -details link show can0 | sed -n '3p'
