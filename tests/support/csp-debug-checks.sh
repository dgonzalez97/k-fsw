#!/usr/bin/env bash

# The caller supplies a sender function. A status response closes each window;
# a separate exchange for readback excludes both the write ACK and old output.
csp_exchange()
{
	local file="$1" process_pid="$2" sender="$3" command="$4"
	local offset
	offset="$(stat -c %s "$file")"
	"$sender" "$command" 'status' || return 1
	for _ in {1..300}; do
		CSP_REPLY="$(tail -c +"$((offset + 1))" "$file" | \
			sed -E $'s/\033\\[[0-9;]*[[:alpha:]]//g' | sed 's/^\[WARNING\] //' | tr -d '\r')"
		if grep -Fxq 'K-FSW status' <<<"$CSP_REPLY"; then
			printf '%s\n' "$CSP_REPLY"
			return 0
		fi
		kill -0 "$process_pid" 2>/dev/null || return 1
		sleep 0.05
	done
	return 1
}

csp_reply_has()
{
	grep -Fxq -- "$1" <<<"$CSP_REPLY"
}

csp_refusal_check()
{
	local file="$1" process_pid="$2" sender="$3" write="$4" read="$5"
	local refusal="$6" success="$7" preserved="$8"
	csp_exchange "$file" "$process_pid" "$sender" "$write" || return 1
	csp_reply_has "$refusal" || return 1
	if csp_reply_has "$success"; then
		return 1
	fi
	csp_exchange "$file" "$process_pid" "$sender" "$read" || return 1
	csp_reply_has "$preserved"
}
