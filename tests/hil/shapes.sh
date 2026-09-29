#!/usr/bin/env bash
# The bench shapes, defined once, used by run.sh and preflight.sh.
#
# A shape answers one question: with what is on the desk right now, which
# scenarios can run? Tags alone do not answer it, because a tag like `nucleo`
# sits on tests that also need a CAN transceiver or a second serial adapter.

# shellcheck disable=SC2034

KFSW_SHAPES=(software terminal board board-uart board-can radio)

kfsw_shape_needs() {
	case "$1" in
	software) echo "nothing; this is what software CI runs" ;;
	terminal) echo "tmux and the robot-terminal-runner submodule" ;;
	board) echo "a NUCLEO-L496ZG on its ST-LINK debug UART" ;;
	board-uart) echo "the board, plus a second serial adapter on the CSP UART" ;;
	board-can) echo "the board with a CAN transceiver, plus a host CAN adapter" ;;
	radio) echo "a pair of Holybro radios, one on the board and one on the host" ;;
	*) return 1 ;;
	esac
}

kfsw_shape_covers() {
	case "$1" in
	software) echo "every scenario that needs no hardware" ;;
	terminal) echo "operator-style shell sessions against the Linux image" ;;
	board) echo "boot and readiness, the clock across a reset, parameter tables" ;;
	board-uart) echo "CSP over the physical UART, telemetry capture, the echo benchmark, command retries" ;;
	board-can) echo "CSP over CAN, and firmware update over CAN with both slots read back" ;;
	radio) echo "the raw and CSP links, firmware update over the radio, beacons" ;;
	*) return 1 ;;
	esac
}

# Robot selection for a shape. Several --include options are OR'd by Robot, so
# an AND is written inside one option.
kfsw_shape_selection() {
	case "$1" in
	software)
		echo "--exclude physical"
		;;
	terminal)
		echo "--include terminal"
		;;
	board)
		# Every physical NUCLEO test that needs no second device.
		echo "--include physicalANDnucleo --exclude can --exclude uart --exclude fwu --exclude holybro"
		;;
	board-uart)
		echo "--include physicalANDuart --include physicalANDdiagnostics --include physicalANDfirmware-batches --exclude holybro --exclude can"
		;;
	board-can)
		echo "--include physicalANDcan"
		;;
	radio)
		echo "--include physicalANDholybro"
		;;
	*)
		return 1
		;;
	esac
}
