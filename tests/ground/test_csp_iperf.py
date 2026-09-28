"""Bounded CSP benchmark regressions using private PTYs and loopback ZMQ."""

import importlib.util
import json
import os
from pathlib import Path
import pty
import select
import subprocess
import threading
import tty
import unittest

from test_csp_tools import wire

TOOL = os.environ.get('KFSW_CSP_IPERF_TEST_BINARY')
HAVE_ZMQ = importlib.util.find_spec('zmq') is not None


def invoke(arguments):
    return subprocess.run([TOOL, '--src-addr', '30', '--dest-addr', '1',
                           '--packet-size', '64', '--tx-rate', '640',
                           '--duration', '.55', '--reply-timeout', '.2', '--json',
                           *arguments], text=True, capture_output=True, timeout=5)


def kiss_reply(header, payload, source=None, port=None, corrupt=False):
    data = payload + wire.crc32(payload)
    if corrupt:
        data = data[:-1] + bytes([data[-1] ^ 1])
    packet = wire.csp_header(header['source'], header['destination'] if source is None else source,
                             header['sport'] if port is None else port, header['dport'], 1)
    return wire.kiss_encode(packet + data + wire.crc32(data))


@unittest.skipUnless(TOOL, 'set KFSW_CSP_IPERF_TEST_BINARY for benchmark tests')
class IperfTest(unittest.TestCase):
    def exchange(self, respond):
        master, slave = pty.openpty()
        tty.setraw(slave)
        stop = threading.Event()
        errors = []

        def server():
            reader = wire.KissReader()
            try:
                while not stop.is_set():
                    if not select.select([master], [], [], .02)[0]:
                        continue
                    for frame in reader.feed(os.read(master, 4096)):
                        header = wire.parse_csp_header(frame)
                        payload = frame[6:-8]
                        self.assertEqual(frame[-8:-4], wire.crc32(payload))
                        self.assertEqual(frame[-4:], wire.crc32(frame[6:-4]))
                        for response in respond(header, payload):
                            os.write(master, response)
            except Exception as error:
                errors.append(error)

        worker = threading.Thread(target=server)
        worker.start()
        try:
            result = invoke(['--device', os.ttyname(slave)])
        finally:
            stop.set()
            worker.join(timeout=1)
            os.close(master)
            os.close(slave)
        if errors:
            raise errors[0]
        self.assertNotEqual(result.stdout, '', result.stderr)
        row = json.loads(result.stdout)
        self.assertGreaterEqual(row['sent'], 4)
        return result, row

    def test_echo_and_duplicate(self):
        result, row = self.exchange(lambda header, data: [kiss_reply(header, data)] * 2)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(row['received'], row['sent'])
        self.assertEqual(row['duplicates'], row['sent'])
        self.assertEqual(row['lost'], 0)

    def test_foreign_port_nonce_payload_and_crc_are_rejected(self):
        def respond(header, data):
            stale = bytes([data[0] ^ 1]) + data[1:]
            corrupt = data[:-1] + bytes([data[-1] ^ 1])
            return [kiss_reply(header, data, source=3), kiss_reply(header, data, port=2),
                    kiss_reply(header, stale), kiss_reply(header, corrupt),
                    kiss_reply(header, data, corrupt=True), kiss_reply(header, data)]
        result, row = self.exchange(respond)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(row['received'], row['sent'])
        self.assertEqual(row['ignored'], 4 * row['sent'])

    def test_silence_and_tail_loss(self):
        for silent in [True, False]:
            with self.subTest(silent=silent):
                def respond(header, data):
                    return [kiss_reply(header, data)] if not silent and int.from_bytes(data[8:16], 'big') == 0 else []
                result, row = self.exchange(respond)
                self.assertEqual(result.returncode, 2, result.stderr)
                self.assertEqual(row['received'], 0 if silent else 1)
                self.assertEqual(row['lost'], row['sent'] - row['received'])
                self.assertLess(row['elapsed_seconds'], 1.5)

    def test_reordering_recovers_gap_without_double_counting(self):
        held = []
        def respond(header, data):
            if int.from_bytes(data[8:16], 'big') == 0:
                held.append(kiss_reply(header, data))
                return []
            return [kiss_reply(header, data)] + ([held.pop()] if held else [])
        result, row = self.exchange(respond)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(row['lost'], 0)
        self.assertEqual(row['reordered'], 1)

    def test_zero_rate_exits_without_opening_device(self):
        result = subprocess.run([TOOL, '--device', '/does-not-exist', '--dest-addr', '1',
                                 '--tx-rate', '0'], text=True, capture_output=True, timeout=2)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('rate must be finite and positive', result.stderr)
        self.assertNotIn('panicked', result.stderr)

    @unittest.skipUnless(HAVE_ZMQ, 'install pyzmq for the legacy transport regression')
    def test_original_csp1_ping_server_and_asymmetric_replies(self):
        import zmq
        for size in [None, '48', '96']:
            with self.subTest(reply_size=size):
                context = zmq.Context()
                upstream = context.socket(zmq.XSUB)
                downstream = context.socket(zmq.XPUB)
                for socket in [upstream, downstream]:
                    socket.setsockopt(zmq.LINGER, 0)
                tx_port = upstream.bind_to_random_port('tcp://127.0.0.1')
                rx_port = downstream.bind_to_random_port('tcp://127.0.0.1')
                endpoints = ['--zmq-tx-socket', f'tcp://127.0.0.1:{tx_port}',
                             '--zmq-rx-socket', f'tcp://127.0.0.1:{rx_port}']
                stop = threading.Event()
                def relay():
                    poller = zmq.Poller()
                    poller.register(upstream, zmq.POLLIN)
                    poller.register(downstream, zmq.POLLIN)
                    while not stop.is_set():
                        events = dict(poller.poll(20))
                        if downstream in events:
                            upstream.send(downstream.recv())
                        if upstream in events:
                            downstream.send(upstream.recv())
                worker = threading.Thread(target=relay)
                worker.start()
                extra = ['--reply-size', size] if size else []
                server = subprocess.Popen([str(Path(TOOL).with_name('csp-ping-server')),
                                           *endpoints, '--addr', '1', *extra],
                                          stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
                try:
                    result = invoke(endpoints + extra)
                finally:
                    server.terminate()
                    server.communicate(timeout=2)
                    stop.set()
                    worker.join(timeout=1)
                    upstream.close()
                    downstream.close()
                    context.term()
                self.assertEqual(result.returncode, 0, result.stderr)
                row = json.loads(result.stdout)
                self.assertGreater(row['received'], 0)
                self.assertEqual(row['lost'], 0)
