#!/usr/bin/env bash
set -Eeuo pipefail
source "$(dirname "$0")/support/csp-debug-checks.sh"
work_dir="$(mktemp -d)"
trap 'rm -rf -- "$work_dir"' EXIT
log="$work_dir/transcript"

# Old valid output must not satisfy a missing or reset readback. Echoed writes
# must not count as success; an actual success reply must fail the refusal test.
send_fixture()
{
	if [[ "$1" == write ]]; then
		printf '%s\n' 'kfsw:~$ param set 2 log_level 5' >>"$log"
		[[ "$scenario" == no-refusal ]] || printf '\033[33m[WARNING] param set: node 2 did not answer\033[0m\r\n' >>"$log"
		[[ "$scenario" != false-success ]] || printf '%s\n' '2:log_level = 5' >>"$log"
	else
		case "$scenario" in
			reset) printf '%s\n' '2:log_level = 1' >>"$log" ;;
			stale) : ;;
			prefix) printf '%s\n' '2:log_level = 30' >>"$log" ;;
			*) printf '\033[0m2:log_level = 3\r\n' >>"$log" ;;
		esac
	fi
	printf '%s\n' 'K-FSW status' >>"$log"
}
for scenario in preserved reset stale prefix false-success no-refusal; do
	printf '%s\n' '2:log_level = 3' 'K-FSW status' >"$log"
	result=0
	csp_refusal_check "$log" "$$" send_fixture write read \
		'param set: node 2 did not answer' '2:log_level = 5' \
		'2:log_level = 3' >/dev/null || result=$?
	if [[ "$scenario" == preserved ]]; then
		[[ "$result" == 0 ]]
	else
		[[ "$result" != 0 ]]
	fi
	echo "CSP transcript $scenario: PASS"
done

# A terminated reader cannot close a new window with an old status response.
printf '%s\n' 'K-FSW status' >"$log"
send_nothing() { :; }
if csp_exchange "$log" 99999999 send_nothing read; then
	echo 'stale status accepted'
	exit 1
fi
echo 'CSP transcript stale barrier: PASS'
