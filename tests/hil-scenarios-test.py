#!/usr/bin/env python3
"""Software-only checks for HIL image selection and argument validation."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

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
    printf 'CONFIG_KFSW_CSP_ADDRESS=3\\nCONFIG_BOARD_TARGET="nucleo_f746zg"\\n# CONFIG_KFSW_STORAGE is not set\\n' >"$KFSW_BUILD_DIR/zephyr/.config"
else
    printf 'CONFIG_KFSW_CSP_ADDRESS=2\\nCONFIG_BOARD_TARGET="nucleo_l496zg/stm32l496xx"\\nCONFIG_KFSW_STORAGE=y\\n' >"$KFSW_BUILD_DIR/zephyr/.config"
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
