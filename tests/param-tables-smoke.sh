#!/usr/bin/env bash
# Checks that every parameter table of the composition is registered with its
# ID and band, and that the listing addresses parameters by table and offset.
#
# Runs against the hosted image by default. Pass --serial to use a board's debug
# UART instead, with a by-id path.

set -euo pipefail

KFSW_TESTS_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
KFSW_REPO_DIR="$(dirname "$KFSW_TESTS_DIR")"
KFSW_ROOT="$(dirname "$KFSW_REPO_DIR")"

executable="$KFSW_ROOT/build/linux/zephyr/zephyr.exe"
serial=""
work_dir="$(mktemp -d)"
failures=0

trap 'rm -rf "$work_dir"' EXIT

while [[ $# -gt 0 ]]; do
	case "$1" in
	--serial)
		serial="${2:-}"
		shift 2
		;;
	--executable)
		executable="${2:-}"
		shift 2
		;;
	*)
		printf 'unknown argument: %s\n' "$1" >&2
		exit 2
		;;
	esac
done

expect()
{
	if grep -aqF "$1" "$work_dir/output.log"; then
		printf '  [ok]   %s\n' "$1"
	else
		printf '  [FAIL] missing: %s\n' "$1" >&2
		failures=$((failures + 1))
	fi
}

if [[ -n "$serial" ]]; then
	[[ "$serial" == /dev/serial/by-id/* ]] ||
		{ printf 'the serial device must be a stable /dev/serial/by-id path\n' >&2; exit 1; }
	[[ -e "$serial" ]] || { printf '%s is not present\n' "$serial" >&2; exit 1; }

	stty_saved="$(stty -F "$serial" -g)"
	trap 'stty -F "$serial" "$stty_saved" 2>/dev/null || true; rm -rf "$work_dir"' EXIT
	stty -F "$serial" 115200 cs8 -cstopb -parenb -crtscts raw -echo
	timeout 30s cat "$serial" >"$work_dir/output.log" &
	capture_pid=$!
	sleep 1
	printf '\r' >"$serial"
	sleep 0.5
	printf 'param tables\r' >"$serial"
	sleep 2
	printf 'param list\r' >"$serial"
	sleep 3
	kill "$capture_pid" 2>/dev/null || true
	wait "$capture_pid" 2>/dev/null || true
else
	[[ -x "$executable" ]] || { printf '%s is not built\n' "$executable" >&2; exit 1; }
	printf '%s\n' 'param tables' 'param list' |
		"$executable" --uart_stdinout --stop_at=6.0 --no-color \
			-flash="$work_dir/flash.bin" >"$work_dir/output.log" 2>&1 || true
fi

printf '\n=== Tables ===\n'
# ID, band and name together, since the wire uses the ID.
expect '  1  core     board'
expect '  2  core     system'
expect '  3  core     telemetry'
expect '  4  core     csp'
expect '  5  core     storage'
expect ' 25  service  log'
expect ' 26  service  param'
expect ' 27  service  event'
expect ' 28  service  command'
expect ' 29  service  ftp'
expect ' 32  service  boot'
expect ' 33  service  hk'

printf '\n=== Addressing ===\n'
# Offset zero in several tables.
expect 'board       0x00  node_id'
expect 'system      0x00  boot_delay_ms'
expect 'telemetry   0x00  uptime_s'
expect 'csp         0x00  tx_packets'
expect 'storage     0x00  total_kb'
expect 'log         0x00  log_level'
expect 'hk          0x00  hk_reports'

printf '\n=== Strings ===\n'
# Strings are quoted, so an empty value is visible.
expect 'board       0x10  uid                               string'
expect 'board       0x30  revision                          string'
expect 'csp         0x20  route_table                       string'

printf '\n=== Modes ===\n'
# The mode column comes from the definition.
expect 'uptime_s                          u32     r'
expect 'boot_delay_ms                     u16     wpb'
expect 'app_report_ms                     u16     wp'
# Routes are selected by the build before the router starts.
expect 'route_table                       string  r'
# Off by default: the shell repeats every input byte, so a scripted session
# would show each command twice.
expect 'echo_enabled                      u8      w     0'
# Read-only counters the services keep, and the settings they apply.
expect 'ftp_timeout_ms                    u32     w'
expect 'cmd_invoked                       u32     r'
expect 'boot_image                        string  r'
# One level per module, rendered as a list because the elements mean something
# positionally and a hex blob would hide which module is which.
expect 'log_levels                        data    wp'

printf '\n=== Result ===\n'
if [[ "$failures" -eq 0 ]]; then
	tables="$(grep -acE '^ *[0-9]+  (core|service|module)' "$work_dir/output.log" || true)"
	printf 'PARAM TABLES RESULT: PASS tables=%s\n' "$tables"
else
	printf 'PARAM TABLES RESULT: FAIL %d check(s) failed\n' "$failures"
	exit 1
fi
