"""Numeric reply fragments must not accept prefixes of different values."""

from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'hil/resources'))
from Ground import Ground


class Responses(unittest.TestCase):
    def test_complete_tokens_in_compound_replies(self):
        ground = Ground()
        cases = [
            ('contacts: 1 last_node: 19', 'contacts: 10 last_node: 19', 'contacts: 1'),
            ('1:boot_trial_valid = 0', '1:boot_trial_valid = 01', '1:boot_trial_valid = 0'),
            ('report 0 every 1000 ms', 'report 0 every 10000 ms', 'report 0 every 1000 ms'),
            ('run absent.txt: -2', 'run absent.txt: -20', 'run absent.txt: -2'),
            ('CSP node: 16\nCSP node: 19', 'CSP node: 160', 'CSP node: 16'),
            ('19/14 -> KISS direct', '119/14 -> KISS direct', '19/14 -> KISS direct'),
            ('bytes=256', 'bytes=2560', 'bytes=256'),
            ('selected: 2', 'selected: -2', 'selected: 2'),
            ('selected: 2', 'selected: 2.5', 'selected: 2'),
            ('node: 1', 'last_node: 1', 'node: 1'),
            ('board 0x00', 'board 0x00a', 'board 0x00'),
            ('\x1b[0mnode 19 UHF module: absent',
             '\x1b[0mnode 190 UHF module: absent', 'node 19 UHF module: absent'),
        ]
        for good, bad, expected in cases:
            with self.subTest(expected=expected):
                ground.response_should_contain(good, expected)
                with self.assertRaises(AssertionError):
                    ground.response_should_contain(bad, expected)


if __name__ == '__main__':
    unittest.main()
