"""Protocol faults on a private PTY; opt in with KFSW_CSP_TOOLS_TEST_BINARY."""

import importlib.util
import json
import os
from pathlib import Path
import pty
import select
import struct
import subprocess
import tempfile
import threading
import tty
import unittest


REPO = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('hk_wire', REPO / 'tools/ground/hk-bridge.py')
wire = importlib.util.module_from_spec(spec)
spec.loader.exec_module(wire)
TOOL = os.environ.get('KFSW_CSP_TOOLS_TEST_BINARY')


@unittest.skipUnless(TOOL, 'set KFSW_CSP_TOOLS_TEST_BINARY for CSP tool protocol tests')
class CspToolsTest(unittest.TestCase):
    def exchange(self, arguments, responder):
        master, slave = pty.openpty()
        tty.setraw(slave)
        stop = threading.Event()
        errors = []

        def server():
            reader = wire.KissReader()
            try:
                while not stop.is_set():
                    if not select.select([master], [], [], .05)[0]:
                        continue
                    for frame in reader.feed(os.read(master, 4096)):
                        header = wire.parse_csp_header(frame)
                        payload = frame[6:-8]
                        self.assertEqual(frame[-4:], wire.crc32(frame[6:-4]))
                        self.assertEqual(frame[-8:-4], wire.crc32(payload))
                        for reply in responder(header, payload):
                            data = reply + wire.crc32(reply)
                            packet = wire.csp_header(header['source'], header['destination'],
                                                     header['sport'], header['dport'], 1)
                            os.write(master, wire.kiss_encode(packet + data + wire.crc32(data)))
            except Exception as error:
                errors.append(error)

        worker = threading.Thread(target=server)
        worker.start()
        try:
            result = subprocess.run([TOOL, '--device', os.ttyname(slave), '--timeout-ms', '150']
                                    + arguments, text=True, capture_output=True, timeout=5)
        finally:
            stop.set()
            worker.join(timeout=1)
            os.close(master)
            os.close(slave)
        if errors:
            raise errors[0]
        return result

    def test_discover_multiple_peers_and_missing_or_invalid_identity(self):
        def respond(header, payload):
            node = header['destination']
            if node == 4:
                return []
            if header['dport'] == 1:
                return [payload]
            if node == 2:
                return []
            identity = bytearray(93)
            identity[:2] = b'\xff\x01'
            identity[2:8] = b'node-1'
            if node == 3:
                identity[2:22] = b'x' * 20
            return [bytes(identity)]

        result = self.exchange(['discover', '--nodes', '1,2,3,4'], respond)
        self.assertEqual(result.returncode, 0, result.stderr)
        rows = [json.loads(line) for line in result.stdout.splitlines()]
        self.assertEqual([row['status'] for row in rows[:-1]],
                         ['identified', 'reachable', 'reachable', 'no_reply'])
        self.assertIn('unterminated', rows[2]['identity_error'])
        self.assertTrue(rows[-1]['complete'])

    @staticmethod
    def log_replies(payload):
        header = b'\x01' + payload[4:12]
        start = header[:1] + b'\x00' + header[1:] + struct.pack('>QQQ', 1, 2, 0)
        record = header[:1] + b'\x01' + header[1:] + struct.pack('>QQBBBB', 1, 55, 0, 2, 0, 1) + b'x'
        end = header[:1] + b'\x02' + header[1:] + b'\x00\x00\x01'
        return start, record, end

    def test_logs_ignore_old_transaction_and_preserve_original_bytes(self):
        def respond(_header, payload):
            start, record, end = self.log_replies(payload)
            old = bytearray(start)
            old[2] ^= 1
            return [bytes(old), start, record, end]

        result = self.exchange(['logs', '--node', '1'], respond)
        self.assertEqual(result.returncode, 0, result.stderr)
        rows = [json.loads(line) for line in result.stdout.splitlines()]
        self.assertEqual(rows[1]['text_hex'], '78')
        self.assertTrue(rows[-1]['complete'])

    def test_logs_missing_duplicate_and_truncated_replies_fail(self):
        for fault in ['missing', 'duplicate', 'truncated', 'no_end']:
            with self.subTest(fault=fault):
                def respond(_header, payload):
                    start, record, end = self.log_replies(payload)
                    return {'missing': [start, end], 'duplicate': [start, record, record, end],
                            'truncated': [start, record[:-1], end], 'no_end': [start, record]}[fault]
                with tempfile.TemporaryDirectory() as directory:
                    path = Path(directory) / 'partial.jsonl'
                    result = self.exchange(['logs', '--node', '1', '--output', str(path)], respond)
                    self.assertNotEqual(result.returncode, 0)
                    rows = [json.loads(line) for line in path.read_text().splitlines()]
                    self.assertEqual(rows[0]['kind'], 'start')
                    self.assertFalse(any(row.get('complete') for row in rows))

    def test_empty_filtered_capture_completes(self):
        def respond(_header, payload):
            start, _record, end = self.log_replies(payload)
            return [start, end[:-1] + b'\x00']

        result = self.exchange(['logs', '--node', '1', '--min-level', '3'], respond)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue(json.loads(result.stdout.splitlines()[-1])['complete'])
