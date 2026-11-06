#!/usr/bin/env bash
set -Eeuo pipefail

KFSW_BOARD_TOOL="$(readlink -f "${BASH_SOURCE[0]}")"
KFSW_CI_DIR="$(dirname "$KFSW_BOARD_TOOL")"
KFSW_REPO_DIR="$(dirname "$(dirname "$KFSW_CI_DIR")")"
source "$KFSW_REPO_DIR/tools/_common.sh" linux
unset KFSW_BUILD_DIR
hil="$KFSW_REPO_DIR/tests/hil"
python="$KFSW_ROOT/.venv/bin/python"
[[ -x "$python" ]] || python=python3
out="$(mktemp -d "${TMPDIR:-/tmp}/kfsw-board.XXXXXX")"
trap 'rm -rf -- "$out"' EXIT
passed=()
failed=()
not_run=()

# preflight prints a line per missing device and then its verdict, so the
# summary keeps the shape names and the verdict alone stays on the case line.
brief() {
	local text="${1//$'\n'/ }"
	text="${text##*cannot run; }"
	echo "${text%%)*}" | tr -s ' '
}

skip() {
	echo "BOARD: $1 NOT RUN: $(brief "$2")"
	not_run+=("$1")
}

gate() {
	local name="$1" shape="$2" reason
	if ! reason="$("$hil/preflight.sh" "$shape" 2>&1)"; then
		skip "$name" "$reason"
		return 1
	fi
	if [[ "$shape" == board-can ]]; then
		if ! command -v ip >/dev/null 2>&1; then
			skip "$name" 'ip is not installed'
			return 1
		fi
		if ! ip -details link show "${KFSW_CAN_INTERFACE:-can0}" 2>/dev/null | grep -q 'can state ERROR-ACTIVE'; then
			skip "$name" 'CAN interface is not ERROR-ACTIVE'
			return 1
		fi
	fi
}

need() {
	local name="$1" script="$2" tool
	shift 2
	if [[ ! -f "$script" || ! -r "$script" || ( "$script" != *.py && ! -x "$script" ) ]]; then
		skip "$name" "missing ${script#"$KFSW_REPO_DIR/"} (readable script required; shell scripts must be executable)"
		return 1
	fi
	for tool in "$@"; do
		if ! command -v "$tool" >/dev/null 2>&1; then
			skip "$name" "$tool is not installed"
			return 1
		fi
	done
}

run_case() {
	local name="$1" effect="$2" status=0 reason
	shift 2
	echo "BOARD: $name: $effect"
	"$@" >"$out/case.log" 2>&1 || status=$?
	cat "$out/case.log"
	if [[ "$status" -ne 0 ]]; then
		failed+=("$name (exit $status)")
	elif reason="$(grep 'NOT RUN' "$out/case.log")"; then
		skip "$name" "$reason"
	else
		passed+=("$name")
	fi
}

key_gate() {
	# The same fallback preflight.sh uses, so a bench with the key in its usual
	# place does not have to export anything to sign.
	local key="${KFSW_MCUBOOT_KEY:-$HOME/.config/kfsw/mcuboot-signing-key.pem}"
	if [[ ! -f "$key" || ! -r "$key" ]]; then
		skip "$1" "no readable signing key at $key; set KFSW_MCUBOOT_KEY"
		return 1
	fi
}

"$hil/preflight.sh"
while IFS='=' read -r variable value; do
	[[ "$variable" == KFSW_MCUBOOT_KEY ]] && continue
	[[ -n "$variable" && -z "${!variable:-}" ]] && export "$variable=$value"
done < <("$hil/preflight.sh" --export)
export KGROUND_HOLYBRO_DEVICE="${KGROUND_HOLYBRO_DEVICE:-${KFSW_RADIO_SERIAL:-}}"

if gate can-acceptance board-can && need can-acceptance "$hil/fwu/can-acceptance.py" "$python"; then
	if [[ "${KFSW_FWU_CAN_ACCEPTANCE_READY:-0}" != 1 ]]; then
		skip can-acceptance 'install and confirm the probes-enabled CAN baseline, then set KFSW_FWU_CAN_ACCEPTANCE_READY=1 (tests/hil/fwu/README.md)'
	elif [[ ! -x "${KFSW_FWU_CAN_GROUND:-}" || ! -r "${KFSW_FWU_CAN_IMAGE:-}" || ! -r "${KFSW_FWU_CAN_FLASH:-}" ]]; then
		skip can-acceptance 'needs KFSW_FWU_CAN_GROUND executable, KFSW_FWU_CAN_IMAGE and KFSW_FWU_CAN_FLASH containing /candidate.bin'
	else
		run_case can-acceptance 'erases and writes secondary-slot pages and resets interrupted updates; one-hour soak' \
			"$python" "$hil/fwu/can-acceptance.py" --ground "$KFSW_FWU_CAN_GROUND" \
			--image "$KFSW_FWU_CAN_IMAGE" --flash "$KFSW_FWU_CAN_FLASH" \
			--serial "$KFSW_DEBUG_SERIAL" --interface "$KFSW_CAN_INTERFACE" --interruptions \
			--output "${KFSW_OUTPUT_ROOT:-$KFSW_ROOT/build}/board-can-acceptance-$(date +%s)"
	fi
fi

if gate radio-crypto radio && need radio-crypto "$hil/radio-uhf/holybro/crypto-smoke.py" "$python"; then
	if [[ "${KFSW_RADIO_CRYPTO_READY:-0}" != 1 ]]; then
		skip radio-crypto 'install the encrypted flight baseline, then set KFSW_RADIO_CRYPTO_READY=1 (tests/hil/radio-uhf/holybro/README.md)'
	elif [[ ! -x "${KFSW_RADIO_CRYPTO_GROUND:-}" || ! -r "${KFSW_RADIO_CRYPTO_KEY_FILE:-}" ]]; then
		skip radio-crypto 'needs KFSW_RADIO_CRYPTO_GROUND executable and readable KFSW_RADIO_CRYPTO_KEY_FILE'
	else
		run_case radio-crypto 'sets radio encryption keys, leaves the key configured, tests wrong keys and replay' \
			"$python" "$hil/radio-uhf/holybro/crypto-smoke.py" --ground "$KFSW_RADIO_CRYPTO_GROUND" \
			--serial "$KFSW_DEBUG_SERIAL" --radio "$KFSW_RADIO_SERIAL" \
			--key-file "$KFSW_RADIO_CRYPTO_KEY_FILE" \
			--output "${KFSW_OUTPUT_ROOT:-$KFSW_ROOT/build}/board-radio-crypto-$(date +%s)"
	fi
fi

for shape in board board-uart board-can radio; do
	gate "$shape" "$shape" || continue
	need "$shape" "$hil/run.sh" openocd "$python" || continue
	shape_out="${KFSW_OUTPUT_ROOT:-$KFSW_ROOT/build}/robot/board/$shape"
	rm -rf -- "$shape_out"
	echo "BOARD: $shape reflashes the selected boards and may rewrite firmware slots, Golden and test storage"
	status=0
	KFSW_ROBOT_OUT_DIR="$shape_out" "$hil/run.sh" "$shape" || status=$?
	counts_status=0
	if [[ -f "$shape_out/output.xml" ]]; then
		"$python" "$KFSW_CI_DIR/robot-counts.py" "$shape_out/output.xml" --board >"$out/counts" 2>&1 || counts_status=$?
		cat "$out/counts"
		while IFS= read -r reason; do
			[[ "$reason" == 'NOT RUN: '* ]] && not_run+=("$shape: ${reason#NOT RUN: }")
		done <"$out/counts"
	else
		counts_status=1
	fi
	if [[ "$status" -ne 0 || ( "$counts_status" -ne 0 && "$counts_status" -ne 3 ) ]]; then
		failed+=("$shape (runner exit $status, counts exit $counts_status)")
	elif [[ "$counts_status" -eq 3 ]]; then
		skip "$shape" 'no cases executed'
	else
		passed+=("$shape (executed cases only; see Robot counts)")
	fi
done

for case_name in watchdog health mcuboot golden-recovery limits-can; do
	shape=board
	[[ "$case_name" == golden-recovery || "$case_name" == limits-can ]] && shape=board-can
	gate "$case_name" "$shape" || continue
	case "$case_name" in
	watchdog) script="$hil/watchdog/watchdog-reset.sh" ;;
	health) script="$hil/health/health-reset.sh" ;;
	mcuboot)
		key_gate "$case_name" || continue
		script="$hil/mcuboot/rollback.sh" ;;
	golden-recovery)
		key_gate "$case_name" || continue
		script="$hil/golden-recovery-smoke.sh" ;;
	limits-can) script="$hil/limits/released-over-can.sh" ;;
	esac
	tools=(openocd python3)
	[[ "$shape" == board-can ]] && tools+=(ip)
	need "$case_name" "$script" "${tools[@]}" || continue
	run_case "$case_name" 'reflashes L496 firmware; MCUboot and Golden cases rewrite bootloader or recovery slots' "$script"
done

frdm_usb="$(sed -n 's/^KFSW_USB_FLASH_ID=//p' "$KFSW_REPO_DIR/config/targets/frdm_k64f.env")"
if lsusb -d "$frdm_usb" 2>/dev/null | grep -q .; then
	frdm_serial="$(source "$KFSW_REPO_DIR/config/targets/frdm_k64f.env"; printf '%s' "$KFSW_SERIAL")"
	if [[ "$frdm_serial" == /dev/serial/by-id/* && -c "$frdm_serial" ]]; then
		if need frdm-shell "$hil/shell-smoke.sh" openocd stty; then
			run_case frdm-shell 'reflashes FRDM-K64F shell image' "$hil/shell-smoke.sh" frdm_k64f
		fi
	else
		skip frdm-shell 'FRDM-K64F needs a live /dev/serial/by-id console (KFSW_SERIAL)'
	fi
else
	skip frdm-shell "FRDM-K64F probe USB $frdm_usb is not attached"
fi
if key_gate f746-proof && need f746-proof "$KFSW_REPO_DIR/tools/f746-proof.sh" python3; then
	run_case f746-proof 'offline builds and signed-image checks; no hardware IO' \
		"$KFSW_REPO_DIR/tools/f746-proof.sh" build "${KFSW_OUTPUT_ROOT:-$KFSW_ROOT/build}/f746-proof"
fi
skip pico-shell 'needs a person to copy the UF2 and reconnect the console'
skip button-acceptance 'needs a person to press the button and judge its behaviour'

if [[ ${#failed[@]} -gt 0 ]]; then
	echo "BOARD RESULT: FAIL ${failed[*]}; ran ${passed[*]:-none}; not run ${not_run[*]:-none}"
	exit 1
elif [[ ${#passed[@]} -eq 0 ]]; then
	echo "BOARD RESULT: NOT RUN; nothing on this bench: ${not_run[*]}"
else
	echo "BOARD RESULT: PASS ${passed[*]}; not run ${not_run[*]:-none}"
fi
