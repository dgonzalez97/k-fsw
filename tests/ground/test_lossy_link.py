import contextlib
import importlib.util
import io
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch


TESTS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TESTS))
from firmware_fixture import wire

spec = importlib.util.spec_from_file_location('lossy_link', TESTS / 'support/lossy-link.py')
bridge = importlib.util.module_from_spec(spec)
spec.loader.exec_module(bridge)


class LossyLinkTests(unittest.TestCase):
    def setUp(self):
        self.version = wire.csp_version

    def tearDown(self):
        wire.csp_version = self.version

    def frame(self, opcode, offset=0):
        source, destination, sport, dport = (16, 19, 9, 20)
        if opcode == 10:
            source, destination, sport, dport = (19, 16, 20, 9)
        payload = bytes((1, opcode, 0, 0)) + (1).to_bytes(4, 'big')
        payload += offset.to_bytes(4, 'big') + b'\xc0\xdb'
        return wire.kiss_encode(wire.csp_header(destination, source, dport, sport, 0) + payload)

    def run_bridge(self, chunks, *options, write_size=None):
        forwarded = bytearray()

        def write(fd, data):
            size = min(len(data), write_size) if write_size else len(data)
            forwarded.extend(data[:size])
            return size

        event = [(SimpleNamespace(fileobj=10, data=11), 1)]
        events = [event for _ in chunks] + [KeyboardInterrupt()]
        with tempfile.TemporaryDirectory() as directory:
            argv = ['lossy-link', '--left', 'left', '--right', 'right',
                    '--csp-version', str(wire.csp_version), '--drop-bytes', '0',
                    '--ftp-stats', str(Path(directory) / 'stats.json'), *options]
            with (patch.object(sys, 'argv', argv),
                  patch.object(bridge.os, 'open', side_effect=[10, 11]),
                  patch.object(bridge.os, 'read', side_effect=chunks),
                  patch.object(bridge.os, 'write', side_effect=write),
                  patch.object(bridge.selectors, 'DefaultSelector') as selector,
                  patch.object(bridge.time, 'monotonic', return_value=1),
                  contextlib.redirect_stderr(io.StringIO())):
                selector.return_value.select.side_effect = events
                self.assertEqual(bridge.main(), 0)
            return bytes(forwarded)

    def test_cut_forwards_four_chunks_even_when_reads_are_coalesced_or_split(self):
        for version in (1, 2):
            wire.csp_version = version
            info = self.frame(13)
            data = [self.frame(14, offset) for offset in range(6)]
            stream = info + b''.join(data)
            for chunks in ([stream], [bytes((byte,)) for byte in stream], [info, *data]):
                with self.subTest(version=version, reads=len(chunks)):
                    self.assertEqual(self.run_bridge(chunks, '--cut-after-get-data', '4'),
                                     info + b''.join(data[:4]))

    def test_loss_drops_one_data_frame_and_forwards_its_retry(self):
        for version in (1, 2):
            wire.csp_version = version
            info = self.frame(9)
            data = [self.frame(10, offset) for offset in range(6)]
            stream = info + b''.join(data) + data[3]
            for chunks in ([stream], [bytes((byte,)) for byte in stream]):
                with self.subTest(version=version, reads=len(chunks)):
                    self.assertEqual(self.run_bridge(chunks, '--drop-put-data', '4'),
                                     info + b''.join(data[:3] + data[4:]) + data[3])

    def test_forwarding_resumes_without_a_second_cut(self):
        for version in (1, 2):
            wire.csp_version = version
            data = [self.frame(14, offset) for offset in range(6)]
            with self.subTest(version=version):
                self.assertEqual(self.run_bridge([b''.join(data), b''.join(data[4:])],
                                                 '--cut-after-get-data', '4', '--cut-for', '0'),
                                 b''.join(data))

    def test_short_writes_preserve_complete_frames(self):
        for version in (1, 2):
            wire.csp_version = version
            stream = self.frame(13) + self.frame(14)
            with self.subTest(version=version):
                self.assertEqual(self.run_bridge([stream], write_size=3), stream)


if __name__ == '__main__':
    unittest.main()
