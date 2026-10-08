#!/usr/bin/env bash
# CSP 1: the targets build with config/profiles/csp-v1.conf, an address CSP 1
# cannot carry stops the build, the CSP and multi-KISS smokes pass on a CSP 1
# network, and a CSP 2 node and a CSP 1 node do not understand each other.
set -Eeuo pipefail

KFSW_CSP_V1_TOOL="$(readlink -f "${BASH_SOURCE[0]}")"
KFSW_CI_DIR="$(dirname "$KFSW_CSP_V1_TOOL")"
KFSW_TOOLS_DIR="$(dirname "$KFSW_CI_DIR")"
KFSW_REPO_DIR="$(dirname "$KFSW_TOOLS_DIR")"

source "$KFSW_TOOLS_DIR/_common.sh" linux

profile="$KFSW_REPO_DIR/config/profiles/csp-v1.conf"
out_dir="${KFSW_OUTPUT_ROOT:-$KFSW_ROOT/build}/csp-v1"
work_dir="$(mktemp -d /tmp/kfsw-csp-v1.XXXXXX)"
pids=()

cleanup()
{
	for pid in "${pids[@]}"; do
		kill "$pid" 2>/dev/null || true
	done
	wait 2>/dev/null || true
	rm -rf -- "$work_dir"
}
trap cleanup EXIT

fail()
{
	echo "CSP V1 RESULT: FAIL ($1)"
	exit 1
}

mkdir -p "$out_dir"

echo "CSP V1: NUCLEO, and NUCLEO with CAN"
KFSW_BUILD_DIR="$out_dir/nucleo" KFSW_EXTRA_CONF_FILE="$profile" KFSW_PRISTINE=always \
	"$KFSW_TOOLS_DIR/build.sh" nucleo_l496zg >"$out_dir/nucleo.log" 2>&1 ||
	fail "the NUCLEO did not build, see $out_dir/nucleo.log"
KFSW_BUILD_DIR="$out_dir/nucleo-can" \
	KFSW_EXTRA_CONF_FILE="$KFSW_REPO_DIR/config/profiles/nucleo-can.conf;$profile" \
	KFSW_EXTRA_DTC_OVERLAY_FILE="$KFSW_REPO_DIR/config/profiles/nucleo-can.overlay" \
	KFSW_PRISTINE=always "$KFSW_TOOLS_DIR/build.sh" nucleo_l496zg >"$out_dir/nucleo-can.log" 2>&1 ||
	fail "the NUCLEO with CAN did not build, see $out_dir/nucleo-can.log"

echo "CSP V1: an address past the CSP 1 range stops the build"
printf 'CONFIG_KFSW_CSP_ADDRESS=40\n' >"$work_dir/address-40.conf"
if KFSW_BUILD_DIR="$out_dir/address-40" KFSW_EXTRA_CONF_FILE="$profile;$work_dir/address-40.conf" \
	KFSW_PRISTINE=always "$KFSW_TOOLS_DIR/build.sh" linux >"$out_dir/address-40.log" 2>&1; then
	fail "node address 40 was accepted under CSP 1"
fi
grep -q "outside the active range (\[1, 30\])" "$out_dir/address-40.log" ||
	fail "address 40 failed the build for another reason, see $out_dir/address-40.log"

echo "CSP V1: the Linux nodes"
KFSW_BUILD_DIR="${KFSW_OUTPUT_ROOT:-$KFSW_ROOT/build}/linux-csp1" \
	KFSW_EXTRA_CONF_FILE="$KFSW_REPO_DIR/tests/config/param-fixtures.conf;$KFSW_REPO_DIR/config/profiles/linux-temperature.conf;$profile" \
	KFSW_PRISTINE=always "$KFSW_TOOLS_DIR/build.sh" linux >"$out_dir/linux.log" 2>&1 ||
	fail "node 1 did not build, see $out_dir/linux.log"
# _common.sh exports KFSW_BUILD_DIR, so name node 2's directory explicitly.
KFSW_BUILD_DIR="${KFSW_OUTPUT_ROOT:-$KFSW_ROOT/build}/tests/linux-node2-csp1" KFSW_CSP_VERSION=1 KFSW_PRISTINE=always \
	"$KFSW_REPO_DIR/tests/build-linux-node2.sh" \
	>"$out_dir/linux-node2.log" 2>&1 || fail "node 2 did not build, see $out_dir/linux-node2.log"

echo "CSP V1: CSP smoke"
KFSW_CSP_VERSION=1 "$KFSW_REPO_DIR/tests/csp-smoke.sh" >"$out_dir/csp-smoke.log" 2>&1 ||
	fail "the CSP smoke failed, see $out_dir/csp-smoke.log"
grep -q "protocol: CSP v1" "$out_dir/csp-smoke.log" || fail "csp info did not report CSP v1"

echo "CSP V1: multi-KISS smoke"
KFSW_CSP_VERSION=1 "$KFSW_REPO_DIR/tests/build-multi-kiss.sh" >"$out_dir/multi-kiss-build.log" 2>&1 ||
	fail "the multi-KISS nodes did not build, see $out_dir/multi-kiss-build.log"
KFSW_CSP_VERSION=1 "$KFSW_REPO_DIR/tests/multi-kiss-smoke.sh" >"$out_dir/multi-kiss.log" 2>&1 ||
	fail "the multi-KISS smoke failed, see $out_dir/multi-kiss.log"

# A CSP 2 node 1 and a CSP 1 node 2 on one link: the ping goes unanswered.
echo "CSP V1: a CSP 2 node and a CSP 1 node do not understand each other"
KFSW_BUILD_DIR="$out_dir/linux-v2" "$KFSW_TOOLS_DIR/build.sh" linux >"$out_dir/linux-v2.log" 2>&1 ||
	fail "the CSP 2 node did not build, see $out_dir/linux-v2.log"
v2_node="$out_dir/linux-v2/zephyr/zephyr.exe"
mkfifo "$work_dir/v2.in" "$work_dir/v1.in"
exec 3<>"$work_dir/v2.in" 4<>"$work_dir/v1.in"
"$v2_node" --uart_stdinout --no-color -flash="$work_dir/v2.bin" <&3 >"$work_dir/v2.log" 2>&1 &
pids+=($!)
"${KFSW_OUTPUT_ROOT:-$KFSW_ROOT/build}/tests/linux-node2-csp1/zephyr/zephyr.exe" --uart_stdinout --no-color \
	-flash="$work_dir/v1.bin" <&4 >"$work_dir/v1.log" 2>&1 &
pids+=($!)
for log in v2 v1; do
	for _ in $(seq 100); do
		grep -q "uart_1 connected to pseudotty: " "$work_dir/$log.log" && break
		sleep 0.1
	done
done
v2_pty="$(sed -n 's/^uart_1 connected to pseudotty: //p' "$work_dir/v2.log" | head -1)"
v1_pty="$(sed -n 's/^uart_1 connected to pseudotty: //p' "$work_dir/v1.log" | head -1)"
[[ -n "$v2_pty" && -n "$v1_pty" ]] || fail "the mixed pair did not start"
socat "$v2_pty,raw,echo=0" "$v1_pty,raw,echo=0" >"$work_dir/socat.log" 2>&1 &
pids+=($!)
sleep 2
printf '%s\n' 'csp ping 2' >&3
for _ in $(seq 100); do
	grep -q "csp ping: node 2 did not answer\|CSP ping 2: success" "$work_dir/v2.log" && break
	sleep 0.1
done
grep -q "CSP ping 2: success" "$work_dir/v2.log" && fail "a CSP 2 node reached a CSP 1 node"
grep -q "csp ping: node 2 did not answer" "$work_dir/v2.log" ||
	fail "the mixed ping gave no answer either way"
grep -q "protocol: CSP v2" <(printf '%s\n' 'csp info' >&3; sleep 1; cat "$work_dir/v2.log") ||
	fail "the CSP 2 side of the mixed pair was not CSP 2"

echo "CSP V1 RESULT: PASS"
