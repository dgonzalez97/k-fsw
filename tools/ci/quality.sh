#!/usr/bin/env bash
set -Eeuo pipefail

KFSW_QUALITY_TOOL="$(readlink -f "${BASH_SOURCE[0]}")"
KFSW_CI_DIR="$(dirname "$KFSW_QUALITY_TOOL")"
KFSW_REPO_DIR="$(dirname "$(dirname "$KFSW_CI_DIR")")"
KFSW_WORKSPACE_ROOT="$(dirname "$KFSW_REPO_DIR")"

clang_format=""

if [[ -x "$KFSW_WORKSPACE_ROOT/.venv/bin/clang-format" ]]; then
	clang_format="$KFSW_WORKSPACE_ROOT/.venv/bin/clang-format"
elif command -v clang-format >/dev/null 2>&1; then
	clang_format="$(command -v clang-format)"
else
	echo "ERROR: clang-format is required"
	exit 1
fi

format_roots=("$KFSW_REPO_DIR/app/src")
[[ -d "$KFSW_REPO_DIR/tests/support" ]] && \
	format_roots+=("$KFSW_REPO_DIR/tests/support")
[[ -d "$KFSW_REPO_DIR/tests/unit" ]] && \
	format_roots+=("$KFSW_REPO_DIR/tests/unit")
[[ -d "$KFSW_REPO_DIR/tests/hil/radio-uhf/holybro/raw-peer/src" ]] && \
	format_roots+=("$KFSW_REPO_DIR/tests/hil/radio-uhf/holybro/raw-peer/src")
[[ -d "$KFSW_WORKSPACE_ROOT/kfsw-modules" ]] && \
	format_roots+=("$KFSW_WORKSPACE_ROOT/kfsw-modules")

mapfile -d '' format_sources < <(
	find "${format_roots[@]}" \
		-type f \( -name '*.c' -o -name '*.h' \) \
		! -path '*/third_party/*' \
		-print0 | sort -z
)

format_sources+=(
	"$KFSW_WORKSPACE_ROOT/kfsw-platform/include/kfsw/platform/storage.h"
	"$KFSW_WORKSPACE_ROOT/kfsw-platform/src/storage.c"
	"$KFSW_WORKSPACE_ROOT/kfsw-platform/include/kfsw/platform/watchdog.h"
	"$KFSW_WORKSPACE_ROOT/kfsw-platform/src/watchdog.c"
	"$KFSW_WORKSPACE_ROOT/kfsw-services/include/kfsw/services/health.h"
	"$KFSW_WORKSPACE_ROOT/kfsw-services/src/health/health.c"
	"$KFSW_WORKSPACE_ROOT/kfsw-services/include/kfsw/services/log.h"
	"$KFSW_WORKSPACE_ROOT/kfsw-services/include/kfsw/services/parameter.h"
	"$KFSW_WORKSPACE_ROOT/kfsw-services/include/kfsw/services/ftp.h"
	"$KFSW_WORKSPACE_ROOT/kfsw-services/src/command/command.c"
	"$KFSW_WORKSPACE_ROOT/kfsw-services/src/command/command_csp.c"
	"$KFSW_WORKSPACE_ROOT/kfsw-services/src/command/command_internal.h"
	"$KFSW_WORKSPACE_ROOT/kfsw-services/src/command/command_protocol.c"
	"$KFSW_WORKSPACE_ROOT/kfsw-services/include/kfsw/services/command.h"
	"$KFSW_WORKSPACE_ROOT/kfsw-services/include/kfsw/services/event.h"
	"$KFSW_WORKSPACE_ROOT/kfsw-services/src/event/event.c"
	"$KFSW_WORKSPACE_ROOT/kfsw-services/src/ftp/ftp_client.c"
	"$KFSW_WORKSPACE_ROOT/kfsw-services/src/ftp/ftp_internal.h"
	"$KFSW_WORKSPACE_ROOT/kfsw-services/src/ftp/ftp_link.h"
	"$KFSW_WORKSPACE_ROOT/kfsw-services/src/ftp/ftp_link_csp.c"
	"$KFSW_WORKSPACE_ROOT/kfsw-services/src/ftp/ftp_protocol.c"
	"$KFSW_WORKSPACE_ROOT/kfsw-services/src/ftp/ftp_server.c"
	"$KFSW_WORKSPACE_ROOT/kfsw-services/src/ftp/ftp_store.c"
	"$KFSW_WORKSPACE_ROOT/kfsw-services/src/ftp/ftp_transfer.c"
	"$KFSW_WORKSPACE_ROOT/kfsw-services/src/log.c"
	"$KFSW_WORKSPACE_ROOT/kfsw-services/src/parameter/parameter.c"
	"$KFSW_WORKSPACE_ROOT/kfsw-services/src/parameter/parameter_csp.c"
	"$KFSW_WORKSPACE_ROOT/kfsw-services/src/parameter/parameter_internal.h"
	"$KFSW_WORKSPACE_ROOT/kfsw-services/src/parameter/parameter_persistence.c"
)

echo "QUALITY: clang-format (${#format_sources[@]} files)"
"$clang_format" --style="file:$KFSW_REPO_DIR/.clang-format" \
	--dry-run --Werror "${format_sources[@]}"

analysis_roots=(
	"$KFSW_REPO_DIR/app/src"
	"$KFSW_WORKSPACE_ROOT/kfsw-platform/src"
	"$KFSW_WORKSPACE_ROOT/kfsw-services/src"
	"$KFSW_WORKSPACE_ROOT/kfsw-comms/src"
)
[[ -d "$KFSW_WORKSPACE_ROOT/kfsw-modules" ]] && \
	analysis_roots+=("$KFSW_WORKSPACE_ROOT/kfsw-modules")

mapfile -d '' analysis_sources < <(
	find "${analysis_roots[@]}" \
		-type f -name '*.c' \
		! -path "$KFSW_WORKSPACE_ROOT/kfsw-modules/tests/*" \
		! -path '*/third_party/*' \
		-print0 | sort -z
)

module_include_args=()
if [[ -d "$KFSW_WORKSPACE_ROOT/kfsw-modules" ]]; then
	while IFS= read -r -d '' include_dir; do
		module_include_args+=(-I "$include_dir")
	done < <(
		find "$KFSW_WORKSPACE_ROOT/kfsw-modules" \
			-type d -name include \
			! -path '*/third_party/*' \
			-print0 | sort -z
	)
fi

# Checked here rather than at the top so a missing analyser cannot abort the
# run before the formatter has reported. A local gate that exits early looks
# like a pass and hides exactly the failures CI then reports.
if ! command -v cppcheck >/dev/null 2>&1; then
	echo "ERROR: cppcheck is required"
	echo "Debian/Ubuntu: sudo apt-get install cppcheck"
	exit 1
fi

echo "QUALITY: cppcheck (${#analysis_sources[@]} files)"
cppcheck \
	--enable=warning,performance,portability \
	--error-exitcode=1 \
	--force \
	--inline-suppr \
	--language=c \
	--quiet \
	--std=c11 \
	--suppress=missingIncludeSystem \
	--suppress=unknownMacro \
	-DCONFIG_KFSW_CSP=1 \
	-DCONFIG_KFSW_CSP_KISS_UART=1 \
	-DCONFIG_KFSW_CSP_UART_INTERRUPT_DRIVEN=1 \
	-DCONFIG_KFSW_LOG_MIN_LEVEL=0 \
	-DCONFIG_KFSW_STORAGE=1 \
	-DCONFIG_KFSW_FTP=1 \
	-DCONFIG_KFSW_COMMAND=1 \
	-DCONFIG_KFSW_EVENT=1 \
	-DCONFIG_KFSW_EVENT_RING_DEPTH=32 \
	-DCONFIG_KFSW_COMMAND_CSP=1 \
	-DCONFIG_KFSW_COMMAND_MAX_COMMANDS=16 \
	-DCONFIG_KFSW_COMMAND_TIMEOUT_MS=10000 \
	-DCONFIG_KFSW_PARAM=1 \
	-DCONFIG_KFSW_PARAM_CSP=1 \
	-DCONFIG_KFSW_PARAM_PERSISTENCE=1 \
	-DCONFIG_KFSW_BOTON_TEST=1 \
	-DCONFIG_KFSW_BOTON_TEST_GPIO=1 \
	-DCONFIG_KFSW_BOTON_TEST_DEBOUNCE_MS=30 \
	-DCONFIG_KFSW_BOTON_TEST_SHELL=1 \
	-DCONFIG_KFSW_RADIO_UHF=1 \
	-DCONFIG_KFSW_RADIO_UHF_HOLYBRO=1 \
	-DCONFIG_KFSW_RADIO_UHF_SHELL=1 \
	-DCONFIG_KFSW_RADIO_UHF_EXPECTED_SERIAL_BAUD=57600 \
	-I "$KFSW_WORKSPACE_ROOT/kfsw-platform/include" \
	-I "$KFSW_WORKSPACE_ROOT/kfsw-services/include" \
	-I "$KFSW_WORKSPACE_ROOT/kfsw-comms/include" \
	"${module_include_args[@]}" \
	"${analysis_sources[@]}"

echo "QUALITY RESULT: PASS"
