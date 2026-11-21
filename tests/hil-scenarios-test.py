#!/usr/bin/env python3
"""Software-only checks for HIL image selection and argument validation."""
import argparse
import importlib.util
import os
import pty
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

REPO = Path(__file__).resolve().parents[1]


class BootSelection(unittest.TestCase):
    def setUp(self):
        self.work = tempfile.TemporaryDirectory(prefix="kfsw-hil-test-")
        self.addCleanup(self.work.cleanup)
        self.root = Path(self.work.name)
        self.repo = self.root / "k-fsw"
        (self.repo / "tests").mkdir(parents=True)
        (self.repo / "tools").mkdir()
        shutil.copy(REPO / "tests/hil-smoke.sh", self.repo / "tests")
        self.bin = self.root / "bin"
        self.bin.mkdir()
        self.serial = self.root / "console"
        self.serial.write_text("@BOOT board=nucleo_l496zg\n@READY uptime_ms=100\n")
        self.calls = self.root / "calls"
        self.env = dict(os.environ, PATH=f"{self.bin}:{os.environ['PATH']}",
                        FIXTURE_ROOT=str(self.root), BOOT_KIND="plain")
        self.env.pop("KFSW_MCUBOOT_KEY", None)
        (self.repo / "tools/_common.sh").write_text('''
KFSW_ROOT="$FIXTURE_ROOT"
KFSW_REPO_DIR="$KFSW_ROOT/k-fsw"
KFSW_BUILD_DIR="$KFSW_ROOT/build"
ZEPHYR_BASE="$KFSW_ROOT/zephyr"
ZEPHYR_BOARD=nucleo_l496zg
KFSW_SERIAL="$KFSW_ROOT/console"
KFSW_SERIAL_BAUD=115200
KFSW_STLINK_USB_ID=0483:374b
KFSW_STLINK_SERIAL=fixture-probe
kfsw_west_flash() { echo "flash $*" >>"$FIXTURE_ROOT/calls"; }
''')
        self.tool(self.repo / "tools/build.sh", '''
printf 'build %s sysbuild=%s conf=%s overlays=%s key=%s\n' "$*" \\
    "${KFSW_SYSBUILD:-}" "${KFSW_EXTRA_CONF_FILE:-}" \\
    "${KFSW_EXTRA_DTC_OVERLAY_FILE:-}" "${KFSW_MCUBOOT_KEY:+set}" >>"$FIXTURE_ROOT/calls"
''')
        self.tool(self.bin / "lsusb", "echo 0483:374b")
        self.tool(self.bin / "stty", "exit 0")
        self.tool(self.bin / "sleep", "exec /bin/sleep 0.02")
        self.tool(self.bin / "openocd", '''
echo "probe $*" >>"$FIXTURE_ROOT/calls"
[[ "$BOOT_KIND" == error ]] && exit 1
for arg in "$@"; do
    if [[ "$arg" == dump_image* ]]; then
        read -r _ destination _ _ <<<"$arg"
        [[ "$BOOT_KIND" == short ]] && size=8 || size=65536
        head -c "$size" /dev/zero >"$destination"
        if [[ "$BOOT_KIND" == mcuboot ]]; then
            printf '*** Booting MCUboot v2 ***' | dd of="$destination" conv=notrunc status=none
        fi
    fi
done
exit 0
''')

    @staticmethod
    def tool(path, body):
        path.write_text("#!/usr/bin/env bash\nset -eu\n" + body + "\n")
        path.chmod(0o755)

    def run_smoke(self):
        result = subprocess.run([str(self.repo / "tests/hil-smoke.sh")],
                                env=self.env, text=True, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, timeout=10)
        calls = self.calls.read_text() if self.calls.exists() else ""
        return result, calls

    def test_plain(self):
        result, calls = self.run_smoke()
        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertIn("Boot image: plain", result.stdout)
        self.assertNotIn("--domain", calls)
        self.assertIn("adapter serial fixture-probe", calls)

    def test_signed_application_only(self):
        self.env["BOOT_KIND"] = "mcuboot"
        key = self.root / "fixture-key"
        key.touch()
        self.env["KFSW_MCUBOOT_KEY"] = str(key)
        result, calls = self.run_smoke()
        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertIn("Boot image: signed", result.stdout)
        self.assertIn("--domain app", calls)
        self.assertIn("sysbuild=1", calls)
        self.assertIn("nucleo-mcuboot-fwu.conf", calls)
        self.assertIn("nucleo-mcuboot-flash.overlay", calls)

    def test_missing_key_does_not_build_or_flash(self):
        self.env["BOOT_KIND"] = "mcuboot"
        result, calls = self.run_smoke()
        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn("set KFSW_MCUBOOT_KEY", result.stdout)
        self.assertNotIn("build ", calls)
        self.assertNotIn("flash ", calls)

    def test_probe_failures_do_not_build_or_flash(self):
        for kind in ("error", "short"):
            with self.subTest(kind=kind):
                self.calls.unlink(missing_ok=True)
                self.env["BOOT_KIND"] = kind
                result, calls = self.run_smoke()
                self.assertEqual(result.returncode, 1, result.stdout)
                self.assertNotIn("build ", calls)
                self.assertNotIn("flash ", calls)

    def test_uart_target_build_and_storage_selection(self):
        shutil.copy(REPO / "tests/uart-csp-smoke.sh", self.repo / "tests")
        (self.repo / "tests/support").mkdir()
        shutil.copy(REPO / "tests/support/csp-debug-checks.sh", self.repo / "tests/support")
        with (self.repo / "tools/_common.sh").open("a") as common:
            common.write('export KFSW_ROOT KFSW_BUILD_DIR\n')
        ftdi = self.root / "ftdi"
        ftdi.touch()
        self.tool(self.bin / "socat", "exit 0")
        self.tool(self.bin / "stty", "exit 77")
        self.tool(self.bin / "udevadm", '''
if [[ "$*" == *ftdi* ]]; then
    printf 'ID_VENDOR_ID=0403\\nID_MODEL_ID=6001\\n'
else
    printf 'ID_VENDOR_ID=0483\\nID_MODEL=STM32_STLink\\n'
fi
''')
        self.tool(self.bin / "readlink", '''
case "$*" in
    *ftdi*) echo /dev/ttyUSB-fixture ;;
    *console*) echo /dev/ttyACM-fixture ;;
    *) exec /usr/bin/readlink "$@" ;;
esac
''')
        self.tool(self.repo / "tools/build.sh", '''
echo "build $* conf=${KFSW_EXTRA_CONF_FILE:-} overlays=${KFSW_EXTRA_DTC_OVERLAY_FILE:-}" >>"$FIXTURE_ROOT/calls"
mkdir -p "$KFSW_BUILD_DIR/zephyr"
if [[ "$1" == linux ]]; then
    printf '#!/bin/sh\\nexit 0\\n' >"$KFSW_BUILD_DIR/zephyr/zephyr.exe"
    chmod +x "$KFSW_BUILD_DIR/zephyr/zephyr.exe"
elif [[ "$1" == nucleo_f746zg ]]; then
    printf 'CONFIG_KFSW_DEBUG_SHELL=y\\nCONFIG_KFSW_CSP_ADDRESS=3\\nCONFIG_BOARD_TARGET="nucleo_f746zg"\\n# CONFIG_KFSW_STORAGE is not set\\n' >"$KFSW_BUILD_DIR/zephyr/.config"
else
    printf 'CONFIG_KFSW_DEBUG_SHELL=y\\nCONFIG_KFSW_CSP_ADDRESS=2\\nCONFIG_BOARD_TARGET="nucleo_l496zg/stm32l496xx"\\nCONFIG_KFSW_STORAGE=y\\n' >"$KFSW_BUILD_DIR/zephyr/.config"
fi
''')
        for target in (None, "nucleo_f746zg"):
            with self.subTest(target=target):
                self.calls.unlink(missing_ok=True)
                # The cable is declared, not detected: say it is on whichever
                # board this case is about.
                self.env["KFSW_FTDI_BOARD"] = target or "nucleo_l496zg"
                args = [str(self.repo / "tests/uart-csp-smoke.sh"), "--ftdi", str(ftdi)]
                if target:
                    args += ["--target", target]
                result = subprocess.run(args, env=self.env, text=True,
                                        stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                        timeout=10)
                self.assertEqual(result.returncode, 1, result.stdout)
                self.assertIn("cannot read ST-LINK UART settings", result.stdout)
                calls = self.calls.read_text()
                if target:
                    self.assertIn("f746-uart.conf", calls)
                    self.assertIn("f746-uart.overlay", calls)
                    self.assertIn("SKIP: storage info", result.stdout)
                else:
                    self.assertIn("build nucleo_l496zg", calls)
                    self.assertNotIn("SKIP: storage info", result.stdout)


class Preflight(unittest.TestCase):
    def setUp(self):
        self.work = tempfile.TemporaryDirectory(prefix="kfsw-preflight-")
        self.addCleanup(self.work.cleanup)
        self.root = Path(self.work.name)
        self.repo = self.root / "k-fsw"
        (self.repo / "tests/hil").mkdir(parents=True)
        (self.repo / "config/targets").mkdir(parents=True)
        for name in ("preflight.sh", "shapes.sh"):
            shutil.copy(REPO / "tests/hil" / name, self.repo / "tests/hil")
        for board in ("nucleo_l496zg", "nucleo_f746zg"):
            (self.repo / f"config/targets/{board}.env").write_text(
                "KFSW_STLINK_USB_ID=0483:374b\nKFSW_STLINK_SERIAL=fixture-probe\n")
        self.bin = self.root / "bin"
        self.bin.mkdir()
        BootSelection.tool(self.bin / "robot", "exit 0")
        BootSelection.tool(self.bin / "ip", "exit 1")
        BootSelection.tool(self.bin / "udevadm", "exit 1")
        BootSelection.tool(self.bin / "lsusb", "echo '  iSerial 3 fixture-probe'")
        master, slave = pty.openpty()
        self.addCleanup(os.close, master)
        self.addCleanup(os.close, slave)
        self.device = os.ttyname(slave)
        self.env = {key: value for key, value in os.environ.items() if not key.startswith("KFSW_")}
        self.env.update(PATH=f"{self.bin}:{os.environ['PATH']}",
                        KFSW_DEBUG_SERIAL=self.device, KFSW_FTDI_DEVICE=self.device,
                        KFSW_RADIO_SERIAL=self.device)

    def run_preflight(self, shape):
        return subprocess.run([str(self.repo / "tests/hil/preflight.sh"), shape],
                              env=self.env, text=True, stdout=subprocess.PIPE,
                              stderr=subprocess.STDOUT, timeout=5)

    def test_valid_devices_and_probe(self):
        for shape in ("software", "board", "board-uart", "radio"):
            with self.subTest(shape=shape):
                result = self.run_preflight(shape)
                self.assertEqual(result.returncode, 0, result.stdout)

    def test_direct_ftdi_uart_is_not_a_radio(self):
        self.env.pop("KFSW_RADIO_SERIAL")
        self.assertEqual(self.run_preflight("board-uart").returncode, 0)
        result = self.run_preflight("radio")
        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn("KFSW_RADIO_SERIAL", result.stdout)
        exported = self.run_preflight("--export")
        self.assertNotIn("KFSW_RADIO_SERIAL=", exported.stdout)
        self.assertIn("KFSW_FTDI_DEVICE=", exported.stdout)

    def test_invalid_overrides(self):
        ordinary = self.root / "ordinary"
        ordinary.touch()
        missing = self.root / "missing"
        dangling = self.root / "dangling"
        dangling.symlink_to(missing)
        for variable, shape in (("KFSW_DEBUG_SERIAL", "board"),
                                ("KFSW_FTDI_DEVICE", "board-uart"),
                                ("KFSW_F746_DEBUG_SERIAL", "board-uart"),
                                ("KFSW_RADIO_SERIAL", "radio")):
            for path in (ordinary, missing, dangling):
                with self.subTest(variable=variable, path=path):
                    self.env[variable] = str(path)
                    result = self.run_preflight(shape)
                    self.assertEqual(result.returncode, 1, result.stdout)
                    self.assertIn(variable, result.stdout)
                    self.assertIn("live character device", result.stdout)
                    self.assertIn("USB", result.stdout)
            self.env[variable] = self.device

    def test_missing_or_wrong_probe(self):
        for output in ("", "  iSerial 3 another-board"):
            BootSelection.tool(self.bin / "lsusb", f"echo '{output}'")
            for shape in ("board", "board-uart", "board-can", "radio"):
                result = self.run_preflight(shape)
                self.assertEqual(result.returncode, 1, result.stdout)
                self.assertIn("ST-LINK USB 0483:374b serial fixture-probe", result.stdout)
            self.assertEqual(self.run_preflight("software").returncode, 0)

    def test_probe_override_and_export(self):
        self.env["KFSW_STLINK_SERIAL"] = "selected-probe"
        self.env["KFSW_STLINK_USB_ID"] = "0483:374e"
        BootSelection.tool(self.bin / "lsusb", """
[[ "$*" == '-v -d 0483:374e' ]] || exit 1
echo '  iSerial 3 selected-probe'
""")
        result = self.run_preflight("board")
        self.assertEqual(result.returncode, 0, result.stdout)
        result = self.run_preflight("--export")
        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertIn("KFSW_STLINK_SERIAL=selected-probe", result.stdout)
        self.env["KFSW_L496_STLINK"] = "selected-probe"
        self.env["KFSW_STLINK_SERIAL"] = "ignored-probe"
        self.assertEqual(self.run_preflight("board").returncode, 0)
        self.env.pop("KFSW_L496_STLINK")
        self.env["KFSW_STLINK_SERIAL"] = "absent-probe"
        self.assertEqual(self.run_preflight("board").returncode, 1)

    def test_unavailable_can(self):
        result = self.run_preflight("board-can")
        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn("a CAN interface with a board on it", result.stdout)

    def test_can_queue_length(self):
        for queue, expected in (("qlen 10", 1), ("qlen 1000", 0), ("qlen 2000", 0), ("", 1)):
            with self.subTest(queue=queue):
                BootSelection.tool(self.bin / "ip", f'''
case "$*" in
    '-brief link show type can') echo 'can0 UP' ;;
    'link show can0' | '-details link show can0') echo 'can0: state UP {queue}' ;;
    *) exit 1 ;;
esac
''')
                for override in (False, True):
                    if override:
                        self.env["KFSW_CAN_INTERFACE"] = "can0"
                    else:
                        self.env.pop("KFSW_CAN_INTERFACE", None)
                    result = self.run_preflight("board-can")
                    self.assertEqual(result.returncode, expected, result.stdout)
                    if expected:
                        self.assertIn("txqueuelen of at least 1000", result.stdout)
                        self.assertIn("can-up.sh 500000 normal", result.stdout)


class CanQueue(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        spec = importlib.util.spec_from_file_location("can_update", REPO / "tests/hil/fwu/can-update.py")
        cls.bench = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(cls.bench)

    def setUp(self):
        self.state = {"txqlen": 1000, "linkinfo": {"info_data": {
            "state": "ERROR-ACTIVE", "bittiming": {"bitrate": 500000}}}}

    def test_sufficient_queue(self):
        for queue in (1000, 2000):
            self.state["txqlen"] = queue
            self.bench.check_can(self.state, "can0")

    def test_short_or_missing_queue(self):
        for queue in (10, 0, None):
            with self.subTest(queue=queue):
                if queue is None:
                    self.state.pop("txqlen")
                else:
                    self.state["txqlen"] = queue
                with self.assertRaisesRegex(RuntimeError, "can0 txqueuelen is .*needs 1000.*can-up.sh"):
                    self.bench.check_can(self.state, "can0")

    def test_wrong_bitrate_or_state(self):
        for key, value in (("bittiming", {"bitrate": 250000}), ("state", "BUS-OFF")):
            with self.subTest(key=key):
                self.setUp()
                self.state["linkinfo"]["info_data"][key] = value
                with self.assertRaisesRegex(RuntimeError, "CAN must be ERROR-ACTIVE at 500000 bit/s"):
                    self.bench.check_can(self.state, "can0")

    def test_rejection_precedes_upload_and_serial(self):
        self.state["txqlen"] = 10
        with mock.patch.object(self.bench, "can_state", return_value=self.state), \
                mock.patch.object(self.bench.subprocess, "run") as run, \
                mock.patch.object(self.bench.serial, "Serial") as serial:
            with self.assertRaisesRegex(RuntimeError, "txqueuelen"):
                self.bench.run_test(argparse.Namespace(interface="can0"), None)
            run.assert_not_called()
            serial.assert_not_called()

    def test_acceptance_rejects_before_serial(self):
        spec = importlib.util.spec_from_file_location(
            "can_acceptance", REPO / "tests/hil/fwu/can-acceptance.py")
        acceptance = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(acceptance)
        self.state["txqlen"] = 10
        with tempfile.TemporaryDirectory(prefix="kfsw-can-acceptance-") as work:
            root = Path(work)
            flash = root / "flash.bin"
            flash.touch()
            arguments = ["can-acceptance.py", "--ground", "ground", "--image", "image",
                         "--flash", str(flash), "--serial", "/dev/serial/by-id/fixture",
                         "--output", str(root / "output")]
            with mock.patch.object(sys, "argv", arguments), \
                    mock.patch.object(acceptance.bench, "can_state", return_value=self.state), \
                    mock.patch.object(acceptance.serial, "Serial") as serial, \
                    mock.patch.object(acceptance.subprocess, "Popen") as process, \
                    mock.patch.object(sys, "stderr") as stderr:
                self.assertEqual(acceptance.main(), 1)
                self.assertIn("txqueuelen", "".join(call.args[0] for call in stderr.write.call_args_list))
                serial.assert_not_called()
                process.assert_not_called()


class CanBringup(unittest.TestCase):
    def setUp(self):
        self.work = tempfile.TemporaryDirectory(prefix="kfsw-can-up-")
        self.addCleanup(self.work.cleanup)
        self.root = Path(self.work.name)
        self.calls = self.root / "calls"
        BootSelection.tool(self.root / "ip", 'echo "$*" >>"$CAN_CALLS"')
        BootSelection.tool(self.root / "cat", '''
[[ "$*" == /sys/class/net/can0/tx_queue_len ]] || exit 1
echo "$CAN_QUEUE"
''')
        self.env = dict(os.environ, PATH=f"{self.root}:{os.environ['PATH']}",
                        CAN_CALLS=str(self.calls), CAN_QUEUE="1000")

    def bring_up(self, mode="normal"):
        return subprocess.run(["bash", str(REPO / "tests/hil/stm32/nucleo-l496zg/can-up.sh"),
                               "500000", mode], env=self.env, text=True,
                              stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=5)

    def test_queue_set_before_interface_up(self):
        result = self.bring_up()
        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertIn("can0 up at 500000 bps, normal, txqueuelen 1000", result.stdout)
        calls = self.calls.read_text().splitlines()
        self.assertLess(calls.index("link set can0 txqueuelen 1000"), calls.index("link set can0 up"))

    def test_readback_failure_leaves_interface_down(self):
        for queue in ("10", ""):
            with self.subTest(queue=queue):
                self.calls.write_text("")
                self.env["CAN_QUEUE"] = queue
                result = self.bring_up()
                self.assertEqual(result.returncode, 1, result.stdout)
                self.assertIn("expected 1000", result.stdout)
                self.assertNotIn("link set can0 up", self.calls.read_text())

    def test_down_does_not_set_queue(self):
        result = self.bring_up("down")
        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertEqual(self.calls.read_text(), "link set can0 down\n")


class UartArguments(unittest.TestCase):
    def test_invalid_arguments(self):
        for arguments in (["--target", "linux"], ["--target"], ["--unknown"]):
            with self.subTest(arguments=arguments):
                result = subprocess.run([str(REPO / "tests/uart-csp-smoke.sh"), *arguments],
                                        stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                        text=True, timeout=5)
                self.assertNotEqual(result.returncode, 0)
                self.assertTrue("ERROR:" in result.stdout or "requires a board" in result.stdout)


if __name__ == "__main__":
    unittest.main()
