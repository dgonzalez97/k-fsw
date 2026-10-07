import importlib.util
import io
import json
from pathlib import Path
import struct
from types import SimpleNamespace
import unittest

spec = importlib.util.spec_from_file_location(
    "hk_bridge", Path(__file__).resolve().parents[2] / "tools/ground/hk-bridge.py")
hk = importlib.util.module_from_spec(spec)
spec.loader.exec_module(hk)


def sample(report=0, sequence=7, seconds=1234567890, value=b'\xc0\xdb'):
    return struct.pack('>BBHIBB', 1, report, sequence, seconds, 1, 0 if seconds else 2) + value


def frame(body, destination=16, port=40):
    body += hk.crc32(body)
    return hk.csp_header(destination, 1, port, hk.HK_PORT, 1) + body + hk.crc32(body)


class Link:
    def __init__(self, data):
        self.data = data
        self.sent = b''

    @property
    def in_waiting(self):
        return len(self.data)

    def read(self, count):
        data, self.data = self.data[:count], self.data[count:]
        return data

    def write(self, data):
        self.sent += data

    def flush(self):
        pass


class BridgeTests(unittest.TestCase):
    def test_split_escapes(self):
        reader = hk.KissReader()
        packet = frame(sample())
        result = []
        for byte in hk.kiss_encode(packet):
            result.extend(reader.feed(bytes([byte])))
        self.assertEqual(result, [packet])
        self.assertEqual(hk.decode_hk_frame(result[0], 1), (sample(), None))

    def test_bad_frames_resynchronize(self):
        good = hk.kiss_encode(frame(sample()))
        for bad in (b'\xc0\x01abc', b'\xc0\0abc\xdb\x12', b'\xc0\0abc\xdb',
                    b'\xc0\0' + b'a' * 2000):
            reader = hk.KissReader()
            self.assertEqual(reader.feed(bad + good), [frame(sample())])

    def test_invalid_samples_and_crcs(self):
        for bad in (b'', sample()[:9], b'\x02' + sample()[1:], sample(report=16),
                    sample()[:10], sample() + b'x' * 240,
                    sample(seconds=0)[:9] + b'\0' + sample()[10:]):
            decoded, problem = hk.decode_hk_frame(frame(bad), 1)
            self.assertIsNone(decoded)
            self.assertIsNotNone(problem)
        damaged = bytearray(frame(sample()))
        damaged[-1] ^= 1
        self.assertIn('KISS CRC32', hk.decode_hk_frame(damaged, 1)[1])
        damaged = frame(sample())[:-4]
        damaged = damaged[:-1] + bytes([damaged[-1] ^ 1])
        self.assertIn('CSP CRC32', hk.decode_hk_frame(damaged + hk.crc32(damaged[6:]), 1)[1])

    def test_report_sequence_wrap_and_reset(self):
        recent = hk.RecentSamples()
        self.assertTrue(recent.accept(1, sample(0), 0))
        self.assertTrue(recent.accept(1, sample(1), 0))
        self.assertFalse(recent.accept(1, sample(0), 1))
        self.assertTrue(recent.accept(2, sample(0), 1))
        self.assertTrue(recent.accept(1, sample(sequence=65535), 2))
        self.assertTrue(recent.accept(1, sample(sequence=0), 3))
        self.assertTrue(recent.accept(1, sample(seconds=1234567891), 4))
        self.assertTrue(recent.accept(1, sample(0), 60))
        for n in range(2000):
            recent.accept(1, sample(sequence=n), 61)
        self.assertLessEqual(len(recent._seen), 1024)

    def test_poll_matches_report_destination_and_port(self):
        bodies = [sample(1), sample(sequence=8), sample(sequence=9), sample()]
        frames = [frame(bodies[0]), frame(bodies[1], destination=17),
                  frame(bodies[2], port=41), frame(bodies[3])]
        link = Link(b''.join(hk.kiss_encode(f) for f in frames))
        args = SimpleNamespace(source=16, node=1, report=0, timeout=.01)
        received = list(hk.collect(link, hk.KissReader(), args, 40, 1, True))
        self.assertEqual([row[0] for row in received], bodies)
        self.assertEqual([row[2] for row in received], [False, False, False, True])
        self.assertTrue(link.sent)
        with self.assertRaises(TimeoutError):
            list(hk.collect(Link(hk.kiss_encode(frames[0])), hk.KissReader(), args, 40, 1, True))

    def test_duplicate_reply_does_not_complete_batch(self):
        link = Link(hk.kiss_encode(frame(sample())) * 2)
        args = SimpleNamespace(source=16, node=1, report=0, timeout=.01)
        with self.assertRaises(TimeoutError):
            list(hk.collect(link, hk.KissReader(), args, 40, 2, True))

    def test_listen_sends_nothing(self):
        link = Link(hk.kiss_encode(frame(sample())))
        args = SimpleNamespace(source=16, node=1, report=0, timeout=.001)
        received = list(hk.collect(link, hk.KissReader(), args, 40, 1, False))
        self.assertEqual(len(received), 1)
        self.assertFalse(link.sent)

    def test_capture_preserves_envelope_with_unset_clock(self):
        for body in (sample(), sample(seconds=0)):
            record = hk.capture_record(1, body, 1234567890123)
            decoded = list(hk.read_capture(io.StringIO(json.dumps(record) + '\n')))
            self.assertEqual(decoded, [(1, body, 1234567890123)])
            self.assertEqual(hk.envelope(decoded[0][1], decoded[0][2]),
                             hk.envelope(body, record['received_ms']))

    def test_bad_capture(self):
        record = hk.capture_record(1, sample(), 123)
        for data in ('{', json.dumps(record), 'x' * 1025 + '\n', '[]\n',
                     json.dumps(dict(record, version=2)) + '\n',
                     json.dumps(dict(record, node=True)) + '\n',
                     json.dumps(dict(record, received_ms=-1)) + '\n',
                     json.dumps(dict(record, sample='ff')) + '\n'):
            with self.assertRaisesRegex(ValueError, 'capture line 1'):
                list(hk.read_capture(io.StringIO(data)))



class CspVersionTests(unittest.TestCase):
    def tearDown(self):
        hk.csp_version = 2

    def test_csp1_header_is_four_bytes_with_the_source_first(self):
        hk.csp_version = 1
        header = hk.csp_header(16, 1, 40, hk.HK_PORT, 1)
        self.assertEqual(len(header), 4)
        expected = (2 << 30) | (1 << 25) | (16 << 20) | (40 << 14) | (hk.HK_PORT << 8) | 1
        self.assertEqual(int.from_bytes(header, 'big'), expected)
        self.assertEqual(hk.parse_csp_header(header), {
            'destination': 16, 'source': 1, 'dport': 40, 'sport': hk.HK_PORT, 'flags': 1})

    def test_csp1_frame_carries_a_sample(self):
        hk.csp_version = 1
        self.assertEqual(hk.decode_hk_frame(frame(sample()), 1), (sample(), None))

    def test_a_csp2_bridge_does_not_read_csp1_frames(self):
        hk.csp_version = 1
        csp1 = frame(sample())
        hk.csp_version = 2
        # Read as CSP 2 the fields land elsewhere, so it is not this node's HK.
        self.assertEqual(hk.decode_hk_frame(csp1, 1), (None, None))

if __name__ == '__main__':
    unittest.main()
