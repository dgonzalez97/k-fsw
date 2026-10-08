"""Dictionary text must agree with the node, including when an ELF looks plausible."""

import binascii
import importlib.util
import io
from pathlib import Path
import struct
import sys
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location(
    'log_decode', Path(__file__).resolve().parents[2] / 'tools/ground/log-decode.py')
log = importlib.util.module_from_spec(spec)
spec.loader.exec_module(log)


class Strings(log.LogDatabase):
    def __init__(self, text, bits=64):
        super().__init__()
        self.set_tgt_bits(bits)
        self.set_tgt_endianness(self.LITTLE_ENDIAN)
        self.text = text

    def find_string(self, pointer):
        return self.text if pointer == 0x1000 else None

    def get_kconfigs(self):
        return {}


PACKAGE = struct.pack('<8BQ', 4, 0, 0, 0, 0, 0, 0, 0, 0x1000)


def capture(text, package=PACKAGE):
    return f'1 t=2ms core level=1 pkg=00000000:{package.hex()} crc32={binascii.crc32(text):08x}'


class DecodeTests(unittest.TestCase):
    def run_decoder(self, text, source, bits=64):
        with patch.object(log, 'ElfStrings', return_value=Strings(text, bits)), \
                patch.object(sys, 'argv', ['log-decode', '--elf', 'image.elf']), \
                patch.object(sys, 'stdin', io.StringIO(source)), \
                patch.object(sys, 'stdout', new_callable=io.StringIO) as output:
            status = log.main()
            return status, output.getvalue()

    def test_matching_text_and_output_shape(self):
        self.assertEqual(self.run_decoder('hello', capture(b'hello')),
                         (0, '1 t=2ms core level=1 hello\n'))

    def test_32_bit_package_and_integer_argument(self):
        package = struct.pack('<4BII', 3, 0, 0, 0, 0x1000, 42)
        self.assertEqual(self.run_decoder('value=%u', capture(b'value=42', package), 32),
                         (0, '1 t=2ms core level=1 value=42\n'))
        status, output = self.run_decoder('other=%u', capture(b'value=42', package), 32)
        self.assertEqual(status, 1)
        self.assertIn('CRC32 mismatch', output)

    def test_ieee_crc_check_vector(self):
        self.assertEqual(binascii.crc32(b'123456789'), 0xcbf43926)
        self.assertEqual(self.run_decoder('123456789', capture(b'123456789'))[0], 0)

    def test_plausible_wrong_dictionary_is_rejected(self):
        status, output = self.run_decoder('other', capture(b'hello'))
        self.assertEqual(status, 1)
        self.assertIn('pkg=', output)
        self.assertIn('not decoded: rendered-text CRC32 mismatch', output)
        self.assertNotIn('other', output)

    def test_missing_malformed_and_damaged_records(self):
        for source in ('pkg=', f'pkg={PACKAGE.hex()}', capture(b'hello').split(' crc32=')[0],
                       capture(b'hello')[:-1], capture(b'hello', b'\0')):
            with self.subTest(source=source):
                status, output = self.run_decoder('hello', source)
                self.assertEqual(status, 1)
                self.assertIn('not decoded', output)
                self.assertIn(source, output)

    def test_node_normalization_and_byte_limit(self):
        text = 'hi\r\n' + 'a' * 200
        self.assertEqual(self.run_decoder(text, capture(b'hi  ' + b'a' * 187)),
                         (0, '1 t=2ms core level=1 hi  ' + 'a' * 187 + '\n'))

    def test_text_passes_through_and_failure_sets_stream_status(self):
        status, output = self.run_decoder('hello', 'plain text\n' + capture(b'other'))
        self.assertEqual(status, 1)
        self.assertTrue(output.startswith('plain text\n'))

    def test_legacy_decoder_guard_is_a_short_package(self):
        # The old decoder's regex takes only 00000000, then decode raises struct.error.
        with self.assertRaises(struct.error):
            database = Strings('hello')
            log.decode(log.LogParserV3(database), database, b'\0' * 4)


if __name__ == '__main__':
    unittest.main()
