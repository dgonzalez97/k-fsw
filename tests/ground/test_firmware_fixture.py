import importlib.util
from pathlib import Path
import unittest
from unittest.mock import Mock, patch


spec = importlib.util.spec_from_file_location(
    'firmware_fixture', Path(__file__).resolve().parents[1] / 'firmware_fixture.py')
fixture = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fixture)


class PacketTests(unittest.TestCase):
    def setUp(self):
        self.version = fixture.wire.csp_version
        self.payload = b'\x01\x00\xc0\xdb'
        self.header = {'destination': 16, 'source': 1, 'dport': 40,
                       'sport': 14, 'flags': 1}

    def tearDown(self):
        fixture.wire.csp_version = self.version

    def frame(self, inner_bad=False, outer_bad=False):
        wire = fixture.wire
        inner = bytearray(wire.crc32(self.payload))
        inner[-1] ^= int(inner_bad)
        body = self.payload + inner
        outer = bytearray(wire.crc32(body))
        outer[-1] ^= int(outer_bad)
        return wire.csp_header(16, 1, 40, 14, 1) + body + outer

    def decode(self, *frames):
        data = b''.join(fixture.wire.kiss_encode(frame) for frame in frames)
        port = Mock(in_waiting=len(data))
        port.read.return_value = data
        with patch.object(fixture.time, 'monotonic', side_effect=[0, 0, 1]):
            return list(fixture.packets(port, .5))

    def test_escaped_frames_in_both_versions(self):
        for version in (1, 2):
            with self.subTest(version=version):
                fixture.wire.csp_version = version
                self.assertEqual(self.decode(self.frame()), [(self.header, self.payload)])

    def test_short_frames_are_rejected_before_header_parsing(self):
        for version in (1, 2):
            with self.subTest(version=version):
                fixture.wire.csp_version = version
                short = [bytes(length) for length in range(fixture.wire.csp_header_bytes() + 8)]
                with patch.object(fixture.wire, 'parse_csp_header',
                                  wraps=fixture.wire.parse_csp_header) as parse:
                    self.assertEqual(self.decode(*short, self.frame()),
                                     [(self.header, self.payload)])
                    parse.assert_called_once()

    def test_bad_inner_crc_is_rejected_with_a_valid_outer_crc(self):
        for version in (1, 2):
            with self.subTest(version=version):
                fixture.wire.csp_version = version
                self.assertEqual(self.decode(self.frame(inner_bad=True), self.frame()),
                                 [(self.header, self.payload)])

    def test_bad_outer_crc_is_rejected_with_a_valid_inner_crc(self):
        for version in (1, 2):
            with self.subTest(version=version):
                fixture.wire.csp_version = version
                self.assertEqual(self.decode(self.frame(outer_bad=True), self.frame()),
                                 [(self.header, self.payload)])


if __name__ == '__main__':
    unittest.main()
