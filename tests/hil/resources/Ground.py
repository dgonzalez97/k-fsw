"""Hosted flight/ground pair over PTY KISS; no UART wiring or RF evidence."""

from contextlib import ExitStack
import os
import shutil
import struct
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

    def response_should_contain(self, text, expected):
        """Compare numeric fields as integers; otherwise match bounded numbers."""
        numeric = re.fullmatch(r'(.*?[:=] )(-?\d+)', expected)
        if numeric:
            field, value = numeric.groups()
            found = re.findall(r'(?<![\w])' + re.escape(field) + r'(-?\d+)(?=\s|$)',
                               text, re.MULTILINE)
            assert found and all(int(item) == int(value) for item in found), (expected, text)
        else:
            pattern = re.escape(expected)
            if re.search(r'\d', expected):
                pattern = r'(?<![0-9])' + pattern + r'(?![0-9])'
            assert re.search(pattern, text), (expected, text)

    def node_command(self, node, prompt, command, expected, timeout):
        offset = node.send(command)
        node.wait(re.escape(expected.split('=')[0]) if '= ' in expected else
                  re.escape(expected), offset, float(timeout))
        node.wait(prompt, offset, float(timeout))
        text = node.log.read_text()[offset:]
        self.response_should_contain(text, expected)
        print(text)
        return text

    def ground_command(self, command, expected, timeout=20):
        return self.node_command(self.ground, r'kfsw-ops# ', command, expected, timeout)

    def flight_command(self, command, expected, timeout=20):
        return self.node_command(self.flight, r'kfsw:~\$ ', command, expected, timeout)

    def decode_ground_capture(self, capture, wrong_image=False):
        path = self.output / 'capture.log'
        path.write_text(capture)
        image = self.flight_image
        if wrong_image:
            # Keep the original addresses and cbprintf arguments, but change a
            # mapped format string so rendering succeeds with different text.
            from elftools.elf.elffile import ELFFile
            package = bytes.fromhex(re.search(r'pkg=00000000:([0-9a-f]+)', capture)[1])
            pointer = struct.unpack_from('<Q', package, 8)[0]
            image = self.output / 'mismatched.elf'
            shutil.copyfile(self.flight_image, image)
            with image.open('rb') as handle:
                elf = ELFFile(handle)
                for section in elf.iter_sections():
                    if section['sh_addr'] <= pointer < section['sh_addr'] + section['sh_size']:
                        offset = pointer - section['sh_addr']
                        data = section.data()
                        end = data.index(b'\0', offset)
                        letter = re.search(rb'[A-Za-z]', data[offset:end])
                        assert letter, 'format needs a literal to change'
                        position = section['sh_offset'] + offset + letter.start()
                        break
                else:
                    raise AssertionError('format pointer is not mapped')
            with image.open('r+b') as handle:
                handle.seek(position)
                old = handle.read(1)
                handle.seek(position)
                handle.write(b'X' if old != b'X' else b'Y')
        result = subprocess.run([sys.executable, str(REPO / 'tools/ground/log-decode.py'),
                                 '--elf', str(image), str(path)], capture_output=True, text=True)
        print(result.stdout)
        return result.returncode, result.stdout
