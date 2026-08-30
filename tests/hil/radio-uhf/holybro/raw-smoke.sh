#!/usr/bin/env bash
set -Eeuo pipefail

HOLYBRO_RAW_TOOL="$(readlink -f "${BASH_SOURCE[0]}")"
HOLYBRO_DIR="$(dirname "$HOLYBRO_RAW_TOOL")"
KFSW_REPO_DIR="$(dirname "$(dirname "$(dirname "$(dirname "$HOLYBRO_DIR")")")")"
KFSW_WORKSPACE_ROOT="$(dirname "$KFSW_REPO_DIR")"

radio_device="${KGROUND_HOLYBRO_DEVICE:-}"
radio_baud="${KGROUND_HOLYBRO_BAUD:-57600}"
sequence="${KGROUND_HOLYBRO_SEQUENCE:-0001}"
exchange_count="${KGROUND_HOLYBRO_COUNT:-1}"
test_timeout="${KGROUND_HOLYBRO_TIMEOUT:-10}"

while [[ $# -gt 0 ]]; do
	case "$1" in
	--device)
		radio_device="${2:?--device requires a path}"
		shift 2
		;;
	--sequence)
		sequence="${2:?--sequence requires four digits}"
		shift 2
		;;
	--count)
		exchange_count="${2:?--count requires an integer}"
		shift 2
		;;
	--timeout)
		test_timeout="${2:?--timeout requires seconds}"
		shift 2
		;;
	-h|--help)
		echo "Usage: raw-smoke.sh [--device PATH] [--sequence NNNN] [--count N] [--timeout SECONDS]"
		exit 0
		;;
	*)
		echo "ERROR: unknown argument: $1"
		exit 2
		;;
	esac
done

if [[ -z "$radio_device" ]]; then
	echo "HOLYBRO RAW RESULT: PENDING"
	echo "  Set KGROUND_HOLYBRO_DEVICE or pass --device after the raw peer is ready."
	exit 77
fi
if [[ ! -e "$radio_device" ]]; then
	echo "HOLYBRO RAW RESULT: PENDING"
	echo "  Radio serial device is not present: $radio_device"
	exit 77
fi

python_command=""
if [[ -x "$KFSW_WORKSPACE_ROOT/.venv/bin/python" ]] && \
	"$KFSW_WORKSPACE_ROOT/.venv/bin/python" -c 'import serial' >/dev/null 2>&1; then
	python_command="$KFSW_WORKSPACE_ROOT/.venv/bin/python"
elif command -v python3 >/dev/null 2>&1 && \
	python3 -c 'import serial' >/dev/null 2>&1; then
	python_command="$(command -v python3)"
else
	echo "HOLYBRO RAW RESULT: FAIL"
	echo "  Python and pyserial are required."
	exit 1
fi

exec "$python_command" "$HOLYBRO_DIR/raw_smoke.py" \
	--device "$radio_device" \
	--baud "$radio_baud" \
	--sequence "$sequence" \
	--count "$exchange_count" \
	--timeout "$test_timeout"
