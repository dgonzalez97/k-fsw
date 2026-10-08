"""Hosted flight/ground pair over PTY KISS; no UART wiring or RF evidence."""

from contextlib import ExitStack
import os
from pathlib import Path
import re
import subprocess
import sys
import time

REPO = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO / 'tests'))
from firmware_fixture import NativeNode


class Ground:
    ROBOT_LIBRARY_SCOPE = 'TEST'

    def open_ground_pair(self, output, procedure=''):
        self.stack = ExitStack()
        self.output = Path(output)
        self.output.mkdir(parents=True, exist_ok=False)
        root = Path(os.environ.get('KFSW_OUTPUT_ROOT', REPO.parent / 'build'))
        self.flight_image = root / 'linux/zephyr/zephyr.exe'
        ground_root = Path(os.environ.get('KGROUND_BUILD_ROOT', root / 'k-ground'))
        self.ground_image = ground_root / 'kfsw-ops-node-19/zephyr/zephyr.exe'
        subprocess.run([str(REPO / 'tools/k-ground'), 'build', 'kfsw-ops'], check=True)
        flash = self.output / 'ground.bin'
        if procedure:
            subprocess.run([sys.executable, str(REPO / 'tools/ground/stage-file.py'),
                            '--flash', str(flash), '--offset', '0xfc000', '--size', '0x40000',
                            str(REPO / 'tests/procedures' / procedure),
                            '/ftp/procedures/' + Path(procedure).name], check=True)
        try:
            self.flight = self.stack.enter_context(NativeNode(self.flight_image, self.output / 'flight'))
            self.ground = self.stack.enter_context(NativeNode(self.ground_image, self.output / 'ground', flash))
            self.bridge_log = self.stack.enter_context((self.output / 'bridge.log').open('w'))
            self.bridge = subprocess.Popen(['socat', self.flight.device + ',raw,echo=0',
                                            self.ground.device + ',raw,echo=0'],
                                           stdout=self.bridge_log, stderr=subprocess.STDOUT)
            self.stack.callback(self.stop_bridge)
            time.sleep(.2)
            self.ground_command('csp routes', '0/0 -> KISS direct')
            self.ground_command('csp ping 1', 'CSP ping 1: success')
        except BaseException:
            self.close_ground_pair()
            raise

    def stop_bridge(self):
        self.bridge.terminate()
        try:
            self.bridge.wait(timeout=3)
        except subprocess.TimeoutExpired:
            self.bridge.kill()
            self.bridge.wait()

    def close_ground_pair(self):
        if hasattr(self, 'stack'):
            self.stack.close()

    def ground_command(self, command, expected, timeout=20):
        offset = self.ground.send(command)
        self.ground.wait(re.escape(expected), offset, float(timeout))
        # Wait for the next prompt so the returned block includes the full reply.
        self.ground.wait(r'kfsw-ops# ', offset, float(timeout))
        text = self.ground.log.read_text()[offset:]
        print(text)
        return text

    def flight_command(self, command, expected, timeout=20):
        offset = self.flight.send(command)
        self.flight.wait(re.escape(expected), offset, float(timeout))
        self.flight.wait(r'kfsw:~\$ ', offset, float(timeout))
        text = self.flight.log.read_text()[offset:]
        print(text)
        return text

    def decode_ground_capture(self, capture, wrong_image=False):
        path = self.output / 'capture.log'
        path.write_text(capture)
        image = self.ground_image if wrong_image else self.flight_image
        result = subprocess.run([sys.executable, str(REPO / 'tools/ground/log-decode.py'),
                                 '--elf', str(image), str(path)], capture_output=True, text=True)
        print(result.stdout)
        return result.returncode, result.stdout
