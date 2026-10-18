#!/usr/bin/env bash
# Two hosted nodes over a PTY KISS bridge, and what a shared key decides.
#
# The point is the failing half. A node that signs with the wrong key is not
# refused with a message: libcsp drops the packet inside the router, so the
# caller sees a timeout and nothing else. The evidence that it was
# authentication and not a broken link is the per-interface counter, which is
# why this checks it rather than trusting the silence.
set -Eeuo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "$here/.." && pwd)"
workspace="$(cd "$root/.." && pwd)"
work="$(mktemp -d /tmp/kfsw-csp-hmac.XXXXXX)"
keep=0
pids=()

cleanup() {
	local pid
	for pid in "${pids[@]:-}"; do
		[[ -n "$pid" ]] && kill "$pid" 2>/dev/null || true
	done
	if [[ "$keep" == 1 ]]; then
		echo "CSP HMAC: logs in $work"
	else
		rm -rf "$work"
	fi
}
trap cleanup EXIT

fail() { keep=1; echo "CSP HMAC RESULT: FAIL - $*"; exit 1; }

command -v socat >/dev/null || { echo "CSP HMAC RESULT: NOT RUN - socat is missing"; exit 0; }

cd "$workspace"
# shellcheck source=/dev/null
[[ -f .venv/bin/activate ]] && . .venv/bin/activate

printf 'kfsw-hmac-key-one' | head -c 16 >"$work/key-a"
printf 'kfsw-hmac-key-two' | head -c 16 >"$work/key-b"

build() # key file, address, build directory
{
	local conf="$work/conf-$2.conf"
	cat >"$conf" <<EOF
CONFIG_KFSW_CSP_HMAC=y
CONFIG_KFSW_CSP_HMAC_KEY_FILE="$1"
CONFIG_KFSW_CSP_ADDRESS=$2
EOF
	KFSW_EXTRA_CONF_FILE="$conf" KFSW_BUILD_DIR="$3" KFSW_PRISTINE=always \
		"$root/tools/build.sh" linux >"$work/build-$2.log" 2>&1 ||
		fail "the node at address $2 did not build; see $work/build-$2.log"
}

build "$work/key-a" 1 "$work/node-a"
build "$work/key-b" 2 "$work/node-b"

# The input fifos are opened here and not in a helper: a descriptor opened
# inside a command substitution belongs to that subshell and is gone by the
# time anything writes to it.
mkfifo "$work/a.in" "$work/b.in"
exec 3<>"$work/a.in"
exec 4<>"$work/b.in"

start() # build directory, name, input descriptor
{
	"$1/zephyr/zephyr.exe" --uart_stdinout --no-color \
		-flash="$work/$2-flash.bin" <&"$3" >"$work/$2.log" 2>&1 &
	pids+=("$!")
}

wait_for() # file, text, seconds
{
	local waited=0
	while ((waited < $3)); do
		grep -q "$2" "$1" 2>/dev/null && return 0
		sleep 0.5
		waited=$((waited + 1))
	done
	return 1
}

start "$work/node-a" a 3
start "$work/node-b" b 4
wait_for "$work/a.log" "@READY " 40 || fail "node a did not come up"
wait_for "$work/b.log" "@READY " 40 || fail "node b did not come up"

pty_of() { sed -n 's/^uart_1 connected to pseudotty: //p' "$1" | head -1; }
pty_a="$(pty_of "$work/a.log")"
pty_b="$(pty_of "$work/b.log")"
[[ -n "$pty_a" && -n "$pty_b" ]] || fail "a node did not expose its KISS pseudo terminal"

socat -d -d "$pty_a,raw,echo=0,b115200" "$pty_b,raw,echo=0,b115200" \
	>"$work/socat.log" 2>&1 &
pids+=("$!")
wait_for "$work/socat.log" "starting data transfer loop" 20 || fail "the bridge did not start"

printf 'csp ping 2\n' >&3
sleep 6
if grep -q "CSP ping 2: success" "$work/a.log"; then
	fail "two nodes with different keys reached each other"
fi
echo "ok   a ping signed with the wrong key did not arrive"

# Both sides, because "nothing arrived" and "nothing was sent" look the same
# from the caller and have different causes.
printf 'csp interfaces\n' >&3
printf 'csp interfaces\n' >&4
sleep 3
sender="$(grep -o 'KISS[^$]*autherr=[0-9]*' "$work/a.log" | tail -1 || true)"
echo "info sender:   ${sender:-no counters}"
authline="$(grep -o 'KISS[^$]*autherr=[0-9]*' "$work/b.log" | tail -1 || true)"
[[ -n "$authline" ]] || fail "node b did not report its interface counters"
autherr="${authline##*autherr=}"
if [[ "$autherr" -lt 1 ]]; then
	fail "node b counted no authentication error: $authline"
fi
echo "ok   the receiving node counted it as authentication, not as a lost packet ($authline)"

echo "CSP HMAC RESULT: PASS different keys refuse each other, autherr=$autherr"
