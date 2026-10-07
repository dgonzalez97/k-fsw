#!/usr/bin/env bash
# GCC's static analyzer (-fanalyzer) over the NUCLEO image: null dereferences,
# leaks, use after free, double fclose and the like, found by following paths
# through the code. A finding in a K-FSW repository fails the stage; Zephyr
# and its modules are not ours to fix, so they are left out.
set -Eeuo pipefail

KFSW_SCA_TOOL="$(readlink -f "${BASH_SOURCE[0]}")"
KFSW_CI_DIR="$(dirname "$KFSW_SCA_TOOL")"
KFSW_TOOLS_DIR="$(dirname "$KFSW_CI_DIR")"

source "$KFSW_TOOLS_DIR/_common.sh" nucleo_l496zg

out_dir="$KFSW_ROOT/build/sca"
log_file="$out_dir/build.log"
findings="$out_dir/findings.txt"
rm -rf "$out_dir"
mkdir -p "$out_dir"

echo "SCA: building nucleo_l496zg with -fanalyzer in $out_dir/build"
set +e
KFSW_BUILD_DIR="$out_dir/build" KFSW_PRISTINE=always \
	KFSW_CMAKE_ARGS="-DZEPHYR_SCA_VARIANT=gcc" \
	"$KFSW_TOOLS_DIR/build.sh" nucleo_l496zg >"$log_file" 2>&1
build_result=$?

# The services only the Linux composition carries, with the board's compiler.
# Only K-FSW's libraries are built: the image would not fit, and is not needed.
echo "SCA: the services the NUCLEO leaves out, in $out_dir/services"
if [[ $build_result -eq 0 ]]; then
	west build -p always -b "$ZEPHYR_BOARD" "$KFSW_ROOT/k-fsw/app" -d "$out_dir/services" \
		--cmake-only \
		--extra-conf "$KFSW_ROOT/k-fsw/tests/config/sca-services.conf" \
		--extra-dtc-overlay "$KFSW_ROOT/k-fsw/config/profiles/nucleo-mcuboot-flash.overlay" \
		--extra-dtc-overlay "$KFSW_ROOT/k-fsw/config/profiles/nucleo-mcuboot-fwu.overlay" \
		-- -DZEPHYR_SCA_VARIANT=gcc >>"$log_file" 2>&1 &&
		cmake --build "$out_dir/services" --target app kfsw_services kfsw_comms \
			kfsw_platform >>"$log_file" 2>&1
	build_result=$?
fi
set -e

# One line per finding: file:line:column: warning: text [-Wanalyzer-...].
grep -aE "^$KFSW_ROOT/(k-fsw|kfsw-[a-z]+)/[^:]+:[0-9]+:[0-9]+: warning: .*\[-Wanalyzer-" \
	"$log_file" | grep -v "/third_party/" | sed "s#^$KFSW_ROOT/##" | sort -u >"$findings" || true

if [[ $build_result -ne 0 ]]; then
	tail -n 40 "$log_file"
	echo "SCA RESULT: FAIL (the analysis build failed, see $log_file)"
	exit 1
fi

if [[ -s "$findings" ]]; then
	echo "SCA: $(wc -l <"$findings") finding(s) in K-FSW code"
	cat "$findings"
	echo "SCA RESULT: FAIL"
	exit 1
fi

echo "SCA RESULT: PASS"
