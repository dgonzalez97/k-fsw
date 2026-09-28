"""Native node lifecycle and CSP framing shared by firmware smoke tests."""

import importlib.util
from pathlib import Path
import re
import subprocess
import time

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('hk_wire', ROOT / 'tools/ground/hk-bridge.py')
wire = importlib.util.module_from_spec(spec)
spec.loader.exec_module(wire)


class NativeNode:
    def __init__(self, executable, output, flash=None):
        self.executable = Path(executable).resolve()
        self.output = Path(output).resolve()
        self.flash = Path(flash).resolve() if flash else self.output / 'flash.bin'

    def __enter__(self):
        self.output.mkdir(parents=True, exist_ok=False)
        self.log = self.output / 'node.log'
        self.stream = self.log.open('w')
        self.process = subprocess.Popen([str(self.executable), '--uart_stdinout', '--no-color',
                                         f'-flash={self.flash}'], stdin=subprocess.PIPE,
                                        stdout=self.stream, stderr=subprocess.STDOUT, text=True)
        try:
            self.wait(r'@READY ')
            self.wait(r'@SERVICES ok failures=0')
            self.device = self.wait(r'uart_1 connected to pseudotty: (\S+)')[1]
        except BaseException:
            self.__exit__(None, None, None)
            raise
        return self

    def wait(self, pattern, offset=0, timeout=15):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            text = self.log.read_text()[offset:]
            if match := re.search(pattern, text):
                return match
            if self.process.poll() is not None:
                break
            time.sleep(.02)
        raise AssertionError(f'missing {pattern}: {self.log.read_text()[offset:]}')

    def send(self, command):
        offset = len(self.log.read_text())
        self.process.stdin.write(command + '\n')
        self.process.stdin.flush()
        return offset

    def command(self, command, pattern, timeout=15):
        return self.wait(pattern, self.send(command), timeout)

    def parameter(self, name):
        return int(self.command(f'param get {name}', rf'{name} = (\d+)')[1])

    def __exit__(self, *unused):
        if self.process.poll() is None:
            self.process.terminate()
            try:
                self.process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait()
        self.process.stdin.close()
        self.stream.close()


def send_packet(port, source, destination, sport, dport, payload):
    data = payload + wire.crc32(payload)
    header = wire.csp_header(destination, source, dport, sport, 1)
    port.write(wire.kiss_encode(header + data + wire.crc32(data)))


def packets(port, seconds=2):
    reader = wire.KissReader()
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        for frame in reader.feed(port.read(max(1, port.in_waiting))):
            header = wire.parse_csp_header(frame)
            data = frame[6:-8]
            if frame[-4:] != wire.crc32(frame[6:-4]) or frame[-8:-4] != wire.crc32(data):
                continue
            yield header, data


class BenchNode(NativeNode):
    """Explicit shell and KISS ports; never resets or discovers a board."""

    def __init__(self, shell, device, output):
        self.shell = shell
        self.device = device
        self.output = Path(output).resolve()

    def __enter__(self):
        import serial
        self.output.mkdir(parents=True, exist_ok=False)
        self.log = self.output / 'node.log'
        self.log.write_text('')
        self.console = serial.Serial(self.shell, 115200, timeout=.05, write_timeout=1)
        return self

    def send(self, command):
        offset = len(self.log.read_text())
        self.console.write((command + '\n').encode())
        return offset

    def wait(self, pattern, offset=0, timeout=15):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            data = self.console.read(max(1, self.console.in_waiting))
            if data:
                with self.log.open('a') as stream:
                    stream.write(data.decode(errors='replace'))
            if match := re.search(pattern, self.log.read_text()[offset:]):
                return match
        raise AssertionError(f'missing {pattern}: {self.log.read_text()[offset:]}')

    def __exit__(self, *unused):
        self.console.close()
