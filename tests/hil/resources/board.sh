#!/usr/bin/env bash

hil_board() {
	local board="$1" composition="$2" serial probe profiles
	local repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
	hil_board_reason=""
	case "$board" in
	nucleo_l496zg)
		serial="${KFSW_DEBUG_SERIAL:-${KFSW_SERIAL:-}}"
		probe="${KFSW_L496_STLINK:-${KFSW_STLINK_SERIAL:-}}"
		;;
	nucleo_f746zg)
		serial="${KFSW_F746_DEBUG_SERIAL:-}"
		probe="${KFSW_F746_STLINK:-}"
		;;
	*) hil_board_reason="$board is not on this bench"; return 1 ;;
	esac

	source "$repo/tools/_common.sh" "$board"
	# _common.sh fills KFSW_SERIAL from the bench environment, which names the
	# L4; every other board has to say which device is its own.
	case "$board" in
	nucleo_l496zg) hil_board_debug="${serial:-${KFSW_SERIAL:-}}" ;;
	*) hil_board_debug="$serial" ;;
	esac
	KFSW_STLINK_SERIAL="${probe:-$KFSW_STLINK_SERIAL}"
	profiles="$KFSW_REPO_DIR/config/profiles"
	hil_board_conf=""
	hil_board_overlay=""
	hil_board_boot_overlay=""
	hil_board_openocd=""
	case "$board:$composition" in
	nucleo_l496zg:limits-can | nucleo_l496zg:limits-links | nucleo_l496zg:fbo-can)
		hil_board_conf="$profiles/nucleo-can.conf;$profiles/nucleo-$composition.conf"
		hil_board_overlay="$profiles/nucleo-can.overlay"
		;;
	nucleo_l496zg:scrub)
		hil_board_conf="$profiles/nucleo-mcuboot.conf;$profiles/nucleo-mcuboot-fwu.conf"
		hil_board_boot_overlay="$profiles/nucleo-mcuboot-flash.overlay"
		hil_board_overlay="$hil_board_boot_overlay;$profiles/nucleo-mcuboot.overlay;$profiles/nucleo-mcuboot-fwu.overlay"
		hil_board_openocd="$KFSW_ROOT/zephyr/boards/st/$board/support/openocd.cfg"
		FLASH_BASE=0x08000000
		SLOT0_ADDR=0x08010000
		SLOT_SIZE=360448
		HEADER_SIZE=0x200
		ALIGN=8
		;;
	nucleo_f746zg:*)
		hil_board_reason="$board has no filesystem composition (limits, FBO and FTP are not built); $composition is unsupported"
		[[ "$composition" != scrub ]] || hil_board_reason="$board has no scrub MCUboot layout or persistent reference composition"
		return 1
		;;
	*) hil_board_reason="no $composition composition for $board"; return 1 ;;
	esac
	if [[ "$hil_board_debug" != /dev/serial/by-id/* || ! -e "$hil_board_debug" ]]; then
		hil_board_reason="$board is not on this bench: debug serial must name a present /dev/serial/by-id device"
		return 1
	fi
	if [[ -z "$KFSW_STLINK_SERIAL" ]]; then
		hil_board_reason="$board has no flash probe serial"
		return 1
	fi
}
