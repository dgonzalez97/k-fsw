#!/usr/bin/env bash
set -Eeuo pipefail

KFSW_QUALITY_TOOL="$(readlink -f "${BASH_SOURCE[0]}")"
KFSW_CI_DIR="$(dirname "$KFSW_QUALITY_TOOL")"
KFSW_REPO_DIR="$(dirname "$(dirname "$KFSW_CI_DIR")")"
KFSW_WORKSPACE_ROOT="$(dirname "$KFSW_REPO_DIR")"

clang_format=""

bash "$KFSW_REPO_DIR/tests/build-options-test.sh"
python3 "$KFSW_REPO_DIR/tests/ci-suite-test.py"

quality_python="$KFSW_WORKSPACE_ROOT/.venv/bin/python"
[[ -x "$quality_python" ]] || quality_python=python3
echo "QUALITY: F746 offline proof tests"
started=$SECONDS
"$quality_python" "$KFSW_REPO_DIR/tests/f746-proof-test.py"
echo "QUALITY: F746 offline proof tests added $((SECONDS - started))s"

# The Python under tools/ and tests/ drives the bench, the release and the
# ground station, and nothing was checking it. F and E9 are real errors, B is
# bugbear: the rules that catch a mistake rather than a preference.
ruff=""
if [[ -x "$KFSW_WORKSPACE_ROOT/.venv/bin/ruff" ]]; then
	ruff="$KFSW_WORKSPACE_ROOT/.venv/bin/ruff"
elif command -v ruff >/dev/null 2>&1; then
	ruff="$(command -v ruff)"
else
	echo "ERROR: ruff is required"
	echo "Install it in the workspace venv: .venv/bin/pip install ruff"
	exit 1
fi

mapfile -d '' python_sources < <(
	find "$KFSW_REPO_DIR/tools" "$KFSW_REPO_DIR/tests" \
		-type f -name '*.py' \
		! -path '*/third_party/*' \
		-print0 | sort -z
)

echo "QUALITY: ruff (${#python_sources[@]} files)"
"$ruff" check --no-cache --select F,E9,B "${python_sources[@]}"

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
format_roots+=("$KFSW_REPO_DIR/tests/hil/fwu")
[[ -d "$KFSW_REPO_DIR/tests/hil/radio-uhf/holybro/raw-peer/src" ]] && \
	format_roots+=("$KFSW_REPO_DIR/tests/hil/radio-uhf/holybro/raw-peer/src")
[[ -d "$KFSW_WORKSPACE_ROOT/kfsw-modules" ]] && \
	format_roots+=("$KFSW_WORKSPACE_ROOT/kfsw-modules")
for library in kfsw-platform kfsw-services kfsw-comms; do
	format_roots+=("$KFSW_WORKSPACE_ROOT/$library/include" "$KFSW_WORKSPACE_ROOT/$library/src")
done

mapfile -d '' format_sources < <(
	find "${format_roots[@]}" \
		-type f \( -name '*.c' -o -name '*.h' \) \
		! -path '*/third_party/*' \
		-print0 | sort -z
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

python3 "$KFSW_CI_DIR/descriptions.py"
python3 "$KFSW_CI_DIR/health-slots.py"

# Checked after the formatter has run, so a missing analyser doesn't hide
# formatting errors.
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
	-DCONFIG_KFSW_LOG_HISTORY=1 \
	-DCONFIG_KFSW_LOG_HISTORY_DEPTH=32 \
	-DCONFIG_KFSW_LOG_REMOTE=1 \
	-DCONFIG_KFSW_LOG_REMOTE_PORT=16 \
	-DCONFIG_KFSW_LOG_REMOTE_STACK_SIZE=2048 \
	-DCONFIG_KFSW_LOG_REMOTE_TIMEOUT_MS=3000 \
	-DCONFIG_KFSW_STORAGE=1 \
	-DCONFIG_KFSW_FTP=1 \
	-DCONFIG_KFSW_COMMAND=1 \
	-DCONFIG_KFSW_EVENT=1 \
	-DCONFIG_KFSW_EVENT_RING_DEPTH=32 \
	-DCONFIG_KFSW_JOURNAL=1 \
	-DCONFIG_KFSW_JOURNAL_CAPACITY=128 \
	-DCONFIG_KFSW_JOURNAL_QUEUE_DEPTH=16 \
	-DCONFIG_KFSW_JOURNAL_MIN_SEVERITY=1 \
	-DCONFIG_KFSW_JOURNAL_FLUSH_MS=1000 \
	-DCONFIG_KFSW_JOURNAL_STACK_SIZE=3072 \
	-DCONFIG_KFSW_COMMAND_CSP=1 \
	-DCONFIG_KFSW_COMMAND_RETRY=1 \
	-DCONFIG_KFSW_COMMAND_RETRY_SLOTS=8 \
	-DCONFIG_KFSW_COMMAND_RETRY_WINDOW_MS=60000 \
	-DCONFIG_KFSW_COMMAND_MAX_COMMANDS=16 \
	-DCONFIG_KFSW_COMMAND_TIMEOUT_MS=3000 \
	-DCONFIG_KFSW_PARAM=1 \
	-DCONFIG_KFSW_PARAM_CSP=1 \
	-DCONFIG_KFSW_PARAM_PERSISTENCE=1 \
	-DCONFIG_KFSW_BUTTON_TEST=1 \
	-DCONFIG_KFSW_BUTTON_TEST_GPIO=1 \
	-DCONFIG_KFSW_BUTTON_TEST_DEBOUNCE_MS=30 \
	-DCONFIG_KFSW_BUTTON_TEST_SHELL=1 \
	-DCONFIG_KFSW_GPREDICT=1 \
	-DCONFIG_KFSW_GPREDICT_GRACE_MS=5000 \
	-DCONFIG_KFSW_GPREDICT_PARK_AZIMUTH_DEG=0 \
	-DCONFIG_KFSW_GPREDICT_PARK_ELEVATION_DEG=0 \
	-DCONFIG_KFSW_GPREDICT_AZIMUTH_MIN_DEG=0 \
	-DCONFIG_KFSW_GPREDICT_AZIMUTH_MAX_DEG=360 \
	-DCONFIG_KFSW_GPREDICT_ELEVATION_MIN_DEG=0 \
	-DCONFIG_KFSW_GPREDICT_ELEVATION_MAX_DEG=90 \
	-DCONFIG_KFSW_GPREDICT_PROFILES=3 \
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
