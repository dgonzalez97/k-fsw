#!/usr/bin/env bash
# Bring the PCAN-USB adapter up. Needs root, which is the only reason this is a
# separate script: everything after it runs as an ordinary user.
set -euo pipefail

bitrate="${1:-500000}"
mode="${2:-normal}"          # normal | listen-only | loopback

ip link set can0 down 2>/dev/null || true

case "$mode" in
listen-only) opts=(listen-only on loopback off) ;;
loopback)    opts=(loopback on listen-only off) ;;
normal)      opts=(listen-only off loopback off) ;;
*) echo "unknown mode: $mode" >&2; exit 2 ;;
esac

ip link set can0 type can bitrate "$bitrate" "${opts[@]}" restart-ms 100
ip link set can0 up
echo "can0 up at ${bitrate} bps, ${mode}"
ip -details link show can0 | sed -n '3p'
