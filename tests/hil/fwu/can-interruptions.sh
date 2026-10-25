#!/usr/bin/env bash
set -Eeuo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$here/../resources/board.sh"
skip() { echo "CAN INTERRUPTIONS RESULT: NOT RUN - $*"; exit 0; }
if ! hil_board nucleo_l496zg fwu-can-acceptance; then
	skip "$hil_board_reason"
fi
[[ $# -eq 1 ]] || { echo 'Usage: can-interruptions.sh OUTPUT'; exit 2; }
output="$1"
[[ "${KFSW_FWU_CAN_ACCEPTANCE_READY:-0}" == 1 ]] ||
	skip 'install and confirm the probes-enabled CAN baseline, then set KFSW_FWU_CAN_ACCEPTANCE_READY=1'
[[ -x "${KFSW_FWU_CAN_GROUND:-}" && -r "${KFSW_FWU_CAN_IMAGE:-}" && -r "${KFSW_FWU_CAN_FLASH:-}" ]] ||
	skip 'needs KFSW_FWU_CAN_GROUND executable, KFSW_FWU_CAN_IMAGE and KFSW_FWU_CAN_FLASH containing /candidate.bin'
interface="${KFSW_CAN_INTERFACE:-can0}"
command -v ip >/dev/null || skip 'ip is missing'
ip -details link show "$interface" 2>/dev/null | grep -q 'can state ERROR-ACTIVE' ||
	skip "$interface is not an active physical CAN bus"
python="$KFSW_ROOT/.venv/bin/python"
[[ -x "$python" ]] || python=python3
exec "$python" "$here/can-acceptance.py" --ground "$KFSW_FWU_CAN_GROUND" \
	--image "$KFSW_FWU_CAN_IMAGE" --flash "$KFSW_FWU_CAN_FLASH" \
	--serial "$hil_board_debug" --interface "$interface" \
	--seconds 0 --interruptions --output "$output"
