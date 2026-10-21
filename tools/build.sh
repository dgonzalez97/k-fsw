#!/usr/bin/env bash
set -Eeuo pipefail

usage() {
	cat <<'EOF'
Usage: tools/build.sh [target] [--semihost]
       tools/build.sh --help

Target defaults to nucleo_l496zg.
  --semihost  Append the debug-only full-text log mirror profile (default off).
              Requires a supported Cortex-M composition and OpenOCD semihosting
              enabled before execution. Supported targets: nucleo_l496zg,
              nucleo_f746zg, frdm_k64f.
EOF
}

KFSW_TARGET=""
semihost=false
for arg in "$@"; do
	case "$arg" in
		--help)
			if [[ $# != 1 ]]; then
				echo "ERROR: --help must be used alone" >&2
				exit 2
			fi
			usage
			exit 0
			;;
		--semihost)
			if $semihost; then
				echo "ERROR: duplicate --semihost" >&2
				exit 2
			fi
			semihost=true
			;;
		-*)
			echo "ERROR: unknown option: $arg" >&2
			usage >&2
			exit 2
			;;
		*)
			if [[ -n "$KFSW_TARGET" || ! "$arg" =~ ^[a-zA-Z0-9_]+$ ]]; then
				echo "ERROR: expected one target name: $arg" >&2
				exit 2
			fi
			KFSW_TARGET="$arg"
			;;
	esac
done
KFSW_TARGET="${KFSW_TARGET:-nucleo_l496zg}"
if $semihost; then
	case "$KFSW_TARGET" in
		nucleo_l496zg|nucleo_f746zg|frdm_k64f) ;;
		*)
			echo "ERROR: --semihost is unsupported for target: $KFSW_TARGET" >&2
			exit 2
			;;
	esac
fi
KFSW_PRISTINE="${KFSW_PRISTINE:-auto}"

case "$KFSW_PRISTINE" in
	auto|always|never)
		;;
	*)
		echo "ERROR: KFSW_PRISTINE must be auto, always, or never"
		exit 1
		;;
esac

source "$(dirname "$0")/_common.sh" "$KFSW_TARGET"

if $semihost; then
	KFSW_EXTRA_CONF_FILE="${KFSW_EXTRA_CONF_FILE:+$KFSW_EXTRA_CONF_FILE;}$KFSW_REPO_DIR/config/profiles/log-semihost.conf"
fi

echo
echo "============================================================"
echo " K-FSW BUILD"
echo "============================================================"
echo "Target  : $KFSW_TARGET"
echo "Board   : $ZEPHYR_BOARD"
echo "Build   : $KFSW_BUILD_DIR"
echo "Pristine: $KFSW_PRISTINE"
echo

west_args=()
cmake_args=()
if [[ -n "${KFSW_OUTPUT_ROOT:-}" ]]; then
	cmake_args+=("-DUSER_CACHE_DIR=$KFSW_OUTPUT_ROOT/cache")
fi

# Sysbuild is off by default; only the MCUboot composition turns it on.
if [[ -n "${KFSW_SYSBUILD:-}" ]]; then
	west_args+=(--sysbuild)
fi

if [[ -n "${KFSW_EXTRA_CONF_FILE:-}" ]]; then
	west_args+=(--extra-conf "$KFSW_EXTRA_CONF_FILE")
fi

if [[ -n "${KFSW_EXTRA_DTC_OVERLAY_FILE:-}" ]]; then
	west_args+=(--extra-dtc-overlay "$KFSW_EXTRA_DTC_OVERLAY_FILE")

fi

# The bootloader image only gets the shared flash map overlay.
if [[ -n "${KFSW_MCUBOOT_DTC_OVERLAY_FILE:-}" ]]; then
	cmake_args+=("-Dmcuboot_EXTRA_DTC_OVERLAY_FILE=$KFSW_MCUBOOT_DTC_OVERLAY_FILE")
fi

if [[ -n "${KFSW_CONF_FILE:-}" ]]; then
	cmake_args+=("-DCONF_FILE=$KFSW_CONF_FILE")
fi

# Extra CMake definitions, separated by spaces, for analysis builds.
if [[ -n "${KFSW_CMAKE_ARGS:-}" ]]; then
	read -r -a extra_cmake_args <<<"$KFSW_CMAKE_ARGS"
	cmake_args+=("${extra_cmake_args[@]}")
fi

# The bootloader is configured in app/sysbuild.conf. Only the signing key is
# passed here; without it MCUboot uses its public development key.
if [[ -n "${KFSW_MCUBOOT_KEY:-}" ]]; then
	if [[ ! -r "$KFSW_MCUBOOT_KEY" ]]; then
		echo "ERROR: KFSW_MCUBOOT_KEY is set but not readable: $KFSW_MCUBOOT_KEY"
		exit 1
	fi
	cmake_args+=("-DSB_CONFIG_BOOT_SIGNATURE_KEY_FILE=\"$KFSW_MCUBOOT_KEY\"")
fi

if [[ ${#cmake_args[@]} -gt 0 ]]; then
	west_args+=(-- "${cmake_args[@]}")
fi

west build \
    -p "$KFSW_PRISTINE" \
    -b "$ZEPHYR_BOARD" \
    "$KFSW_REPO_DIR/app" \
    -d "$KFSW_BUILD_DIR" \
	"${west_args[@]}"
