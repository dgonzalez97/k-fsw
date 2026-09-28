#!/usr/bin/env python3
"""Pull housekeeping from a node and send it to Yamcs.

A CSP client over a KISS serial link. It requests samples and forwards each one
to a Yamcs UDP telemetry link. With --listen it only records beacons and never
sends anything.

Frames are forwarded unchanged; Yamcs decodes them with the mission database
that hk-report.py generates.

For a hosted node, use its pseudo-terminal:

    ./tools/kfsw-linux run              # note the uart_1 pseudotty it prints
    tools/ground/hk-bridge.py --device /dev/pts/7 --node 1 --report 0

or the radio on the bench:

    tools/ground/hk-bridge.py --device "$KGROUND_HOLYBRO_DEVICE" \\
        --baud 57600 --node 2 --report 0
"""

import argparse
import collections
import contextlib
import json
import math
import socket
import struct
import sys
import time


# KISS, as libcsp frames it: a start, the escaped frame, and an end.
FEND = 0xC0
FESC = 0xDB
TFEND = 0xDC
TFESC = 0xDD
TNC_DATA = 0x00

# CSP 2.0: 48 bits, big endian. 2 priority, 14 destination, 14 source,
# 6 destination port, 6 source port, 6 flags.
CSP_HEADER_BYTES = 6
CSP_PRIO_NORM = 2
CSP_FCRC32 = 0x01

HK_PORT = 14
HK_PROTOCOL_VERSION = 1
HK_HEADER_BYTES = 10
HK_FLAG_INCOMPLETE = 0x01
HK_SAMPLE_MAX = 240
CAPTURE_LINE_MAX = 1024

# Envelope added before the node's frame: 8 bytes of Unix milliseconds and 4 of
# sequence, the sizes Yamcs reads. Keep in step with ENVELOPE_BYTES in
# hk-report.py.
ENVELOPE = struct.Struct(">QI")


def kiss_encode(frame):
    out = bytearray([FEND, TNC_DATA])
    for byte in frame:
        if byte == FEND:
            out += bytes([FESC, TFEND])
        elif byte == FESC:
            out += bytes([FESC, TFESC])
        else:
            out.append(byte)
    out.append(FEND)
    return bytes(out)


class KissReader:
    """Reassemble bounded KISS data frames; discard damaged frames to FEND."""

    def __init__(self):
        self._frame = bytearray()
        self._started = False
        self._escaped = False
        self._skip_type_byte = False

    def feed(self, data):
        frames = []
        for byte in data:
            if byte == FEND:
                if self._started and self._frame and not self._escaped:
                    frames.append(bytes(self._frame))
                self._frame.clear()
                self._started = True
                self._escaped = False
                self._skip_type_byte = True
                continue
            if not self._started:
                continue
            if self._skip_type_byte:
                self._skip_type_byte = False
                self._started = byte == TNC_DATA
                continue
            if self._escaped:
                if byte not in (TFEND, TFESC):
                    self._started = False
                    self._frame.clear()
                    continue
                self._frame.append(FEND if byte == TFEND else FESC)
                self._escaped = False
            elif byte == FESC:
                self._escaped = True
            else:
                self._frame.append(byte)
            if len(self._frame) > CSP_HEADER_BYTES + HK_SAMPLE_MAX + 8:
                self._started = False
                self._frame.clear()
        return frames


def csp_header(destination, source, dport, sport, flags):
    packed = (
        (CSP_PRIO_NORM << 46)
        | (destination << 32)
        | (source << 18)
        | (dport << 12)
        | (sport << 6)
        | flags
    )
    return packed.to_bytes(8, "big")[2:]


def parse_csp_header(frame):
    packed = int.from_bytes(b"\x00\x00" + frame[:CSP_HEADER_BYTES], "big")
    return {
        "destination": (packed >> 32) & 0x3FFF,
        "source": (packed >> 18) & 0x3FFF,
        "dport": (packed >> 12) & 0x3F,
        "sport": (packed >> 6) & 0x3F,
        "flags": packed & 0x3F,
    }


# libcsp uses CRC-32C (Castagnoli), not the CRC-32 in zlib. A frame with the
# wrong CRC is dropped without an error.
CRC32C_POLYNOMIAL = 0x82F63B78
CRC32C_TABLE = []
for _index in range(256):
    _value = _index
    for _ in range(8):
        _value = (_value >> 1) ^ (CRC32C_POLYNOMIAL if _value & 1 else 0)
    CRC32C_TABLE.append(_value)


def crc32(data):
    """CRC-32C over the bytes, big endian, as csp_crc32_append writes it."""
    value = 0xFFFFFFFF
    for byte in data:
        value = CRC32C_TABLE[(value ^ byte) & 0xFF] ^ (value >> 8)
    return struct.pack(">I", value ^ 0xFFFFFFFF)


def build_request(source, destination, sport, report, count, first_age):
    """One request packet, framed exactly as libcsp would put it on the wire.

    Two CRC32s, which is not a mistake. The housekeeping socket asks for
    CSP_O_CRC32, so csp_send appends one; then the KISS interface appends
    another over what it was handed. Both cover the payload only, because the
    header is prepended after they are computed. Both are stripped on the way
    back up, so a reply that survives has been checked twice.
    """
    payload = struct.pack(">BBBH", HK_PROTOCOL_VERSION, report, count, first_age)
    payload += crc32(payload)
    body = payload + crc32(payload)
    return kiss_encode(csp_header(destination, source, HK_PORT, sport, CSP_FCRC32) + body)


def decode_hk_frame(frame, node):
    """Return the housekeeping sample in a CSP frame, whoever it was sent to.

    Frames are matched by their source port, so replies to other requests and
    beacons are recorded too.
    """
    if len(frame) < CSP_HEADER_BYTES + 4:
        return None, "short frame"

    header = parse_csp_header(frame)
    if header["sport"] != HK_PORT or header["source"] != node:
        # Not housekeeping, or not the node being watched. Not a fault.
        return None, None

    if header["flags"] not in (0, CSP_FCRC32):
        return None, "unsupported CSP flags"

    body = frame[CSP_HEADER_BYTES:]
    if body[-4:] != crc32(body[:-4]):
        return None, "KISS CRC32 mismatch"
    body = body[:-4]

    if header["flags"] & CSP_FCRC32:
        if len(body) < 4 or body[-4:] != crc32(body[:-4]):
            return None, "CSP CRC32 mismatch"
        body = body[:-4]

    try:
        validate_sample(body)
    except ValueError as error:
        return None, str(error)
    return body, None


def describe(sample):
    version, report, sequence, seconds, count, flags = struct.unpack_from(">BBHIBB", sample)
    when = time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime(seconds)) if seconds else "clock unset"
    marks = " incomplete" if flags & HK_FLAG_INCOMPLETE else ""
    return (f"report {report} seq {sequence} at {when} v{version} "
            f"{count} values{marks} {len(sample)} bytes")


def envelope(sample, host_time_ms):
    version, _report, sequence, seconds, _count, _flags = struct.unpack_from(">BBHIBB", sample)
    if version != HK_PROTOCOL_VERSION:
        raise ValueError(f"housekeeping protocol version {version}, expected {HK_PROTOCOL_VERSION}")
    # A node whose clock was never set reports zero; use the host time instead.
    when = seconds * 1000 if seconds else host_time_ms
    return ENVELOPE.pack(when, sequence) + sample


def validate_sample(sample):
    if not HK_HEADER_BYTES <= len(sample) <= HK_SAMPLE_MAX:
        raise ValueError("invalid housekeeping sample length")
    version, report, _sequence, seconds, count, flags = struct.unpack_from(">BBHIBB", sample)
    if version != HK_PROTOCOL_VERSION or report >= 16:
        raise ValueError("unsupported housekeeping version or report")
    if not 1 <= count <= 64 or flags & ~3:
        raise ValueError("invalid housekeeping count or flags")
    if len(sample) < HK_HEADER_BYTES + count:
        raise ValueError("truncated housekeeping values")
    if bool(flags & 2) != (seconds == 0):
        raise ValueError("inconsistent housekeeping clock flag")


def collect(link, reader, args, sport, count, ask):
    """Yield received samples; only this request's distinct replies count."""
    if ask:
        link.write(build_request(args.source, args.node, sport, args.report, count, 0))
        link.flush()
    matched = set()
    deadline = time.monotonic() + args.timeout
    while time.monotonic() < deadline:
        if ask and len(matched) >= count:
            break
        data = link.read(min(link.in_waiting or 1, 4096))
        for frame in reader.feed(data):
            sample, problem = decode_hk_frame(frame, args.node)
            if problem:
                print(f"dropped a frame: {problem}", file=sys.stderr)
            elif sample:
                header = parse_csp_header(frame)
                matching = (header["destination"] == args.source
                            and header["dport"] == sport and sample[1] == args.report)
                if ask and matching:
                    matched.add(sample)
                yield sample, int(time.time() * 1000), matching
    if ask and len(matched) < count:
        raise TimeoutError(f"received {len(matched)} of {count} requested samples")


class RecentSamples:
    """Keep exact duplicates for at most 60 seconds and 1024 records."""

    def __init__(self):
        self._seen = collections.OrderedDict()

    def accept(self, node, sample, now):
        while self._seen and now - next(iter(self._seen.values())) >= 60:
            self._seen.popitem(last=False)
        key = (node, sample)
        if key in self._seen:
            return False
        self._seen[key] = now
        if len(self._seen) > 1024:
            self._seen.popitem(last=False)
        return True


def capture_record(node, sample, received_ms):
    return {"version": 1, "node": node, "received_ms": received_ms, "sample": sample.hex()}


def read_capture(stream):
    line_number = 0
    while line := stream.readline(CAPTURE_LINE_MAX + 1):
        line_number += 1
        try:
            if len(line) > CAPTURE_LINE_MAX or not line.endswith("\n"):
                raise ValueError("oversized or truncated record")
            record = json.loads(line)
            if not isinstance(record, dict) or set(record) != {
                    "version", "node", "received_ms", "sample"}:
                raise ValueError("invalid capture fields")
            if type(record["version"]) is not int or record["version"] != 1:
                raise ValueError("unsupported capture version")
            if type(record["node"]) is not int or not 0 <= record["node"] <= 16383:
                raise ValueError("invalid source node")
            if (type(record["received_ms"]) is not int
                    or not 0 <= record["received_ms"] < 2**64):
                raise ValueError("invalid receipt time")
            if not isinstance(record["sample"], str):
                raise ValueError("sample must be hexadecimal text")
            sample = bytes.fromhex(record["sample"])
            validate_sample(sample)
        except (ValueError, TypeError) as error:
            raise ValueError(f"capture line {line_number}: {error}") from error
        yield record["node"], sample, record["received_ms"]


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--device", help="serial device or native_sim pseudo-terminal")
    source.add_argument("--replay", help="replay a capture without opening a device")
    parser.add_argument("--capture", help="write accepted samples to a new JSONL file")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--node", type=int, help="CSP address to watch or poll")
    parser.add_argument("--source", type=int, default=16)
    parser.add_argument("--sport", type=int, default=40)
    parser.add_argument("--report", type=int, default=0)
    parser.add_argument("--count", type=int, default=1)
    parser.add_argument("--interval", type=float, default=5.0)
    parser.add_argument("--timeout", type=float, default=2.0)
    parser.add_argument("--once", action="store_true")
    parser.add_argument("--listen", action="store_true", help="receive without transmitting")
    parser.add_argument("--yamcs", default="127.0.0.1:10015",
                        help="host:port of the UDP link, or 'none'")
    args = parser.parse_args()
    if args.replay and (args.capture or args.listen or args.node is not None):
        parser.error("--replay cannot be combined with --capture, --listen or --node")
    if args.device and (args.node is None or not 0 <= args.node <= 16383):
        parser.error("--device requires --node in 0..16383")
    if not (0 <= args.source <= 16383 and 16 <= args.sport <= 63
            and 0 <= args.report < 16 and 1 <= args.count <= 255 and args.baud > 0):
        parser.error("invalid address, port, report, count or baud rate")
    if (not math.isfinite(args.timeout) or not 0 < args.timeout <= 3600
            or not math.isfinite(args.interval) or not 0 <= args.interval <= 3600):
        parser.error("timeout must be in (0,3600], interval in [0,3600]")

    with contextlib.ExitStack() as stack:
        sink = None
        if args.yamcs != "none":
            host, separator, port = args.yamcs.rpartition(":")
            if not separator or not host or not port.isdigit() or not 1 <= int(port) <= 65535:
                parser.error("--yamcs must be host:port or none")
            sink = (stack.enter_context(socket.socket(socket.AF_INET, socket.SOCK_DGRAM)),
                    (host, int(port)))
        capture = stack.enter_context(open(args.capture, "x", encoding="utf-8")) if args.capture else None

        def forward(sample, received_ms):
            print(describe(sample), flush=True)
            if sink:
                sink[0].sendto(envelope(sample, received_ms), sink[1])
            else:
                print(f"  {sample.hex()}", flush=True)

        if args.replay:
            stream = stack.enter_context(open(args.replay, encoding="utf-8"))
            for _node, sample, received_ms in read_capture(stream):
                forward(sample, received_ms)
            return 0

        import serial
        link = stack.enter_context(serial.Serial(args.device, args.baud, timeout=min(0.1, args.timeout),
                                                  write_timeout=args.timeout, exclusive=True))
        reader = KissReader()
        recent = RecentSamples()
        sport = args.sport
        while True:
            failed = False
            try:
                for sample, received_ms, _matching in collect(
                        link, reader, args, sport, args.count, not args.listen):
                    if not recent.accept(args.node, sample, time.monotonic()):
                        continue
                    if capture:
                        capture.write(json.dumps(capture_record(args.node, sample, received_ms)) + "\n")
                        capture.flush()
                    forward(sample, received_ms)
            except TimeoutError as error:
                print(f"no reply: {error}", file=sys.stderr)
                failed = True
            if args.once:
                return 1 if failed else 0
            if not args.listen:
                sport = 16 + (sport - 15) % 48
                time.sleep(args.interval)


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        sys.exit(130)
    except (OSError, ValueError) as error:
        print(f"error: {error}", file=sys.stderr)
        sys.exit(1)
