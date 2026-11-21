#!/usr/bin/env python3
"""Reject incomplete or contradictory resolved reference configurations."""

import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

REPO = Path(__file__).resolve().parents[1]
TOOL = REPO / 'tools/ci/profile-config.py'


class ProfileConfig(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='kfsw-profile-config-')
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.config = self.root / '.config'
        self.requirements = self.root / 'profiles.json'
        self.config.write_text(
            '# Automatically generated configuration\n\n'
            'CONFIG_STORAGE=y\n'
            'CONFIG_MBEDTLS_VERSION_4_x=y\n'
            '# CONFIG_EXTRA_SERVICE is not set\n'
            'CONFIG_NODE=2\n'
            'CONFIG_BOARD_TARGET="native_sim/native/64"\n'
            'CONFIG_OFFSET=0x10000\n'
            'CONFIG_ROLE="reference \\"image\\""\n')
        self.spec = {'version': 1, 'common': {'CONFIG_STORAGE': 'y'},
                     'profiles': {'minimal-hosted': {
                         'CONFIG_EXTRA_SERVICE': 'n', 'CONFIG_NODE': '2',
                         'CONFIG_BOARD_TARGET': '"native_sim/native/64"',
                         'CONFIG_OFFSET': '0x10000',
                         'CONFIG_ROLE': '"reference \\"image\\""'}}}

    def run_check(self, *extra, raw_spec=None):
        self.requirements.write_text(json.dumps(self.spec) if raw_spec is None else raw_spec)
        return subprocess.run(
            [sys.executable, str(TOOL), str(self.config), '--profile', 'minimal-hosted',
             '--requirements', str(self.requirements), *extra],
            capture_output=True, text=True, check=False)

    def assert_failure(self, expected, **kwargs):
        result = self.run_check(**kwargs)
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn(expected, result.stderr)
        self.assertNotIn('PASS', result.stdout)

    def test_resolved_configuration(self):
        result = self.run_check()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn('PROFILE CONFIG minimal-hosted: PASS (6 checks)', result.stdout)

    def test_absent_forbidden_symbol_is_disabled(self):
        self.config.write_text(self.config.read_text().replace(
            '# CONFIG_EXTRA_SERVICE is not set\n', ''))
        self.assertEqual(self.run_check().returncode, 0)

    def test_absent_required_symbol_fails(self):
        self.config.write_text(self.config.read_text().replace('CONFIG_STORAGE=y\n', ''))
        self.assert_failure('CONFIG_STORAGE: expected y, got n')

    def test_wrong_board_target_fails(self):
        self.config.write_text(self.config.read_text().replace(
            '"native_sim/native/64"', '"nucleo_f746zg/stm32f746xx"'))
        self.assert_failure('CONFIG_BOARD_TARGET: expected "native_sim/native/64", '
                            'got "nucleo_f746zg/stm32f746xx"')

    def test_forbidden_enabled_and_wrong_values_all_reported(self):
        self.config.write_text('CONFIG_STORAGE=y\nCONFIG_EXTRA_SERVICE=y\n'
                               'CONFIG_NODE=3\nCONFIG_OFFSET=0x20000\nCONFIG_ROLE="other"\n')
        result = self.run_check()
        self.assertEqual(result.returncode, 1)
        for name in ('EXTRA_SERVICE', 'NODE', 'OFFSET', 'ROLE'):
            self.assertIn(f'CONFIG_{name}: expected', result.stderr)
        self.assertNotIn('PASS', result.stdout)

    def test_duplicate_assignment_fails(self):
        with self.config.open('a') as stream:
            stream.write('CONFIG_STORAGE=y\n')
        self.assert_failure('duplicate configuration symbol: CONFIG_STORAGE')

    def test_enabled_and_disabled_duplicate_fails(self):
        with self.config.open('a') as stream:
            stream.write('CONFIG_EXTRA_SERVICE=n\n')
        self.assert_failure('duplicate configuration symbol: CONFIG_EXTRA_SERVICE')

    def test_malformed_configuration_fails(self):
        for line in ('CONFIG_BROKEN', 'CONFIG_BAD=maybe', 'CONFIG_X="unfinished',
                     '# CONFIG_X is not sett', 'unrecognized line'):
            with self.subTest(line=line):
                self.config.write_text(line + '\n')
                self.assert_failure('invalid configuration line 1')

    def test_invalid_specification_shape_fails(self):
        for spec in ({}, {'version': True, 'common': {}, 'profiles': {}},
                     {'version': 1, 'common': {}, 'profiles': {}},
                     {'version': 1, 'common': [], 'profiles': {'minimal-hosted': {}}}):
            with self.subTest(spec=spec):
                self.spec = spec
                self.assert_failure('requirements')

    def test_invalid_symbol_and_value_fail(self):
        for settings in ({'STORAGE': 'y'}, {'CONFIG_STORAGE': True},
                         {'CONFIG_STORAGE': 'maybe'}, {'CONFIG_X': '\n'},
                         {'CONFIG_X': '"unterminated'}):
            with self.subTest(settings=settings):
                self.spec['common'] = settings
                self.assert_failure('invalid requirement')

    def test_duplicate_json_key_fails(self):
        self.assert_failure('duplicate JSON key: version',
                            raw_spec='{"version": 1, "version": 1}')

    def test_profile_cannot_override_common_requirement(self):
        self.spec['profiles']['minimal-hosted']['CONFIG_STORAGE'] = 'n'
        self.assert_failure('repeated requirement: CONFIG_STORAGE')

    def test_unknown_profile_fails(self):
        self.spec['profiles'] = {'other-hosted': {'CONFIG_OTHER': 'n'}}
        self.assert_failure('unknown profile: minimal-hosted')

    def test_unknown_specification_field_fails(self):
        self.spec['ignored'] = {'CONFIG_HIDDEN': 'y'}
        self.assert_failure('requirements')

    def test_missing_config_fails(self):
        self.config.unlink()
        self.assert_failure('No such file')

    def test_missing_requirements_fails(self):
        result = self.run_check('--requirements', str(self.root / 'missing'))
        self.assertEqual(result.returncode, 1)
        self.assertIn('No such file', result.stderr)

    def test_invalid_unselected_profile_also_fails(self):
        self.spec['profiles']['other-hosted'] = {'CONFIG_UNCHECKED': 'maybe'}
        self.assert_failure('invalid requirement')

    def test_invalid_json_fails(self):
        self.assert_failure('PROFILE CONFIG: FAIL', raw_spec='{')


if __name__ == '__main__':
    unittest.main()
