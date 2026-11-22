#!/usr/bin/env bash
set -Eeuo pipefail

# Share the hosted watchdog and thread-suspension procedure with polling KISS.
exec "$(dirname "$0")/kiss-rx-stall-smoke.sh" --can "$@"
