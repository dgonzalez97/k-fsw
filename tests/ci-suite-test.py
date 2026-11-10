#!/usr/bin/env python3
"""Exercise CI selection and board accounting without hardware."""

import hashlib
import json
import os
import re
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
import xml.etree.ElementTree as ET

REPO = Path(__file__).resolve().parents[1]


class Suites(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='kfsw-ci-suite-')
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.repo = self.root / 'k-fsw'
        self.ci = self.repo / 'tools/ci'
        self.ci.mkdir(parents=True)
        self.env = {'LC_ALL': 'C'}
        self.env['TRACE'] = str(self.root / 'trace')
        self.env['HOME'] = str(self.root / 'home')
        Path(self.env['HOME']).mkdir()
        self.binary = self.root / 'bin'
        self.binary.mkdir()
        for command in ('bash', 'env', 'python3', 'readlink', 'dirname', 'mktemp',
                        'rm', 'tr', 'cat', 'grep', 'sed', 'mkdir', 'date', 'find'):
            (self.binary / command).symlink_to(shutil.which(command))
        self.env['PATH'] = str(self.binary)
        self.stub_paths = set()
        for command in ('west', 'openocd', 'ip', 'pyocd', 'STM32_Programmer_CLI',
                        'JLinkExe', 'candump', 'cansend', 'stty', 'picocom'):
            self.script(self.binary / command,
                        f'echo "FORBIDDEN {command} $*" >> "$TRACE"; exit 99')
        for name in ('all.sh', 'board.sh', 'unit.sh', 'robot-counts.py'):
            shutil.copy2(REPO / 'tools/ci' / name, self.ci / name)

    def script(self, path, body):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text('#!/usr/bin/env bash\nset -eu\n' + body + '\n')
        path.chmod(0o755)

    def stub(self, relative, body=''):
        self.stub_paths.add(relative)
        path = self.repo / relative
        if path.suffix == '.py':
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text('import os, sys\n'
                            'with open(os.environ["TRACE"], "a") as trace:\n'
                            f'    trace.write({relative!r} + " " + " ".join(sys.argv[1:]) + "\\n")\n')
        else:
            self.script(path, f'echo "{relative} $*" >> "$TRACE"\n' + body)

    def trace(self):
        path = Path(self.env['TRACE'])
        return path.read_text().splitlines() if path.exists() else []

    def run_script(self, name, *args):
        Path(self.env['TRACE']).unlink(missing_ok=True)
        result = subprocess.run([str(self.ci / name), *args], env=self.env,
                                text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        if name == 'board.sh':
            for line in self.trace():
                self.assertIn(line.split()[0], self.stub_paths | {'lsusb'}, line)
        return result

    def test_selection(self):
        stages = ('build', 'quality', 'unit', 'coverage', 'integration', 'csp-v1',
                  'valgrind', 'robot', 'docs', 'board')
        for stage in stages:
            self.script(self.ci / f'{stage}.sh', f'echo {stage} >> "$TRACE"')
        self.script(self.ci / 'golden.py', 'exit 0')
        (self.ci / 'golden.py').write_text('pass\n')
        for name in ('tools/zephyr-patch.sh', 'tests/zephyr-patch.sh', 'tools/docs/icd-source.py'):
            self.script(self.repo / name, 'exit 0')
        binary = self.root / 'bin'
        self.script(binary / 'west', 'exit 0')
        for args in (('--suite', 'unknown'), ('--suite',), ('extra',),
                     ('--suite', 'board', 'extra')):
            result = self.run_script('all.sh', *args)
            self.assertEqual(result.returncode, 2, result.stdout)
            self.assertFalse(Path(self.env['TRACE']).exists())
        for args, expected in (((), stages[:-1]), (('--suite', 'software'), stages[:-1]),
                               (('--suite', 'board'), stages)):
            result = self.run_script('all.sh', *args)
            self.assertEqual(result.returncode, 0, result.stdout)
            self.assertEqual(Path(self.env['TRACE']).read_text().splitlines(), list(expected))
            Path(self.env['TRACE']).unlink()

    def board_fixture(self):
        self.script(self.repo / 'tools/_common.sh',
                    'KFSW_ROOT="$(dirname "$KFSW_REPO_DIR")"\nexport KFSW_BUILD_DIR=wrong-target')
        board = (self.ci / 'board.sh').read_text()
        paths = {'tests/hil/' + name for name in re.findall(r'\$hil/([\w./-]+)', board)}
        paths.update('tools/' + name for name in
                     re.findall(r'\$KFSW_REPO_DIR/tools/([\w./-]+\.sh)', board))
        self.assertTrue({'tests/hil/preflight.sh', 'tests/hil/run.sh',
                         'tests/hil/mcuboot/rollback.sh', 'tools/f746-proof.sh'} <= paths)
        for relative in paths - {'tools/_common.sh'}:
            self.stub(relative)
        self.stub('tests/hil/preflight.sh', '''
case "${1:-}" in
  ''|--export) exit 0 ;;
  board) [[ "${ATTACHED:-0}" == 1 ]] && exit 0 ;;
  *) [[ "${ALL_ATTACHED:-0}" == 1 ]] && exit 0 ;;
esac
echo "needs absent $1 device" >&2
exit 1''')
        self.stub('tests/hil/run.sh', '''
[[ -z "${KFSW_BUILD_DIR:-}" ]]
mkdir -p "$KFSW_ROBOT_OUT_DIR"
printf '<robot><suite><test name="case"><status status="%s">fixture reason</status></test><test name="absent F746"><status status="SKIP">F746 not attached</status></test></suite></robot>' "${ROBOT_STATUS:-PASS}" > "$KFSW_ROBOT_OUT_DIR/output.xml"
[[ "${ROBOT_STATUS:-PASS}" != FAIL ]]''')
        for name in ('watchdog/watchdog-reset.sh', 'health/health-reset.sh'):
            self.stub('tests/hil/' + name, 'echo "NOT RUN: fixture baseline unavailable"')
        binary = self.root / 'bin'
        self.script(binary / 'lsusb', 'echo "lsusb $*" >> "$TRACE"')
        self.script(binary / 'ip',
                    'echo "ip $*" >> "$TRACE"; echo "can state ERROR-ACTIVE"')
        self.stub_paths.add('ip')
        target = self.repo / 'config/targets/frdm_k64f.env'
        target.parent.mkdir(parents=True)
        target.write_text('KFSW_USB_FLASH_ID=0d28:0204\n')

    def test_absent_boards(self):
        self.board_fixture()
        result = self.run_script('board.sh')
        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertIn('BOARD RESULT: NOT RUN', result.stdout)
        self.assertTrue(all(line.split()[0] in {'tests/hil/preflight.sh', 'lsusb'}
                            for line in self.trace()))
        self.assertNotIn('PASS', result.stdout)
        for name in ('board', 'board-uart', 'board-can', 'radio', 'watchdog', 'health',
                     'mcuboot', 'golden-recovery', 'limits-can', 'can-acceptance',
                     'radio-crypto', 'frdm-shell', 'f746-proof', 'pico-shell', 'button-acceptance'):
            self.assertIn(f'BOARD: {name} NOT RUN:', result.stdout)

    def test_robot_results(self):
        self.board_fixture()
        self.env['ATTACHED'] = '1'
        for status, code, verdict in (('PASS', 0, 'PASS'), ('SKIP', 0, 'NOT RUN'),
                                      ('FAIL', 1, 'FAIL')):
            self.env['ROBOT_STATUS'] = status
            result = self.run_script('board.sh')
            self.assertEqual(result.returncode, code, result.stdout)
            self.assertIn(f'BOARD RESULT: {verdict}', result.stdout)
            self.assertIn('BOARD: board-uart NOT RUN: needs absent board-uart device', result.stdout)
            self.assertIn('absent F746: F746 not attached', result.stdout)
            if status == 'SKIP':
                self.assertIn('case: fixture reason', result.stdout)
                self.assertNotIn('BOARD RESULT: PASS', result.stdout)

    def key_fixture(self):
        self.board_fixture()
        key = Path(self.env['HOME']) / '.config/kfsw/mcuboot-signing-key.pem'
        key.parent.mkdir(parents=True)
        key.write_text('fixture')
        self.env['ATTACHED'] = '1'

    def test_key_cases(self):
        self.key_fixture()
        self.env['ALL_ATTACHED'] = '1'
        baseline = self.root / 'baseline'
        self.script(baseline, 'exit 99')
        for name in ('KFSW_FWU_CAN_ACCEPTANCE_READY', 'KFSW_RADIO_CRYPTO_READY'):
            self.env[name] = '1'
        for name in ('KFSW_FWU_CAN_GROUND', 'KFSW_FWU_CAN_IMAGE', 'KFSW_FWU_CAN_FLASH',
                     'KFSW_RADIO_CRYPTO_GROUND', 'KFSW_RADIO_CRYPTO_KEY_FILE'):
            self.env[name] = str(baseline)
        self.env.update(KFSW_DEBUG_SERIAL='/absent/debug', KFSW_RADIO_SERIAL='/absent/radio',
                        KFSW_CAN_INTERFACE='absent-can')
        result = self.run_script('board.sh')
        self.assertEqual(result.returncode, 0, result.stdout)
        for name in ('tests/hil/mcuboot/rollback.sh', 'tools/f746-proof.sh',
                     'tests/hil/golden-recovery-smoke.sh', 'tests/hil/limits/released-over-can.sh',
                     'tests/hil/fwu/can-acceptance.py', 'tests/hil/radio-uhf/holybro/crypto-smoke.py'):
            self.assertTrue(any(line.startswith(name + ' ') for line in self.trace()))

    def test_missing_case_scripts(self):
        self.key_fixture()
        for name in ('tests/hil/mcuboot/rollback.sh', 'tools/f746-proof.sh'):
            (self.repo / name).unlink()
        result = self.run_script('board.sh')
        self.assertEqual(result.returncode, 0, result.stdout)
        for name in ('tests/hil/mcuboot/rollback.sh', 'tools/f746-proof.sh'):
            self.assertIn('missing ' + name, result.stdout)
        self.assertNotIn('exit 127', result.stdout)

    def test_missing_tool(self):
        self.key_fixture()
        (self.binary / 'openocd').unlink()
        result = self.run_script('board.sh')
        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertIn('BOARD: mcuboot NOT RUN: openocd is not installed', result.stdout)
        self.assertFalse(any(line.startswith('tests/hil/mcuboot/rollback.sh ')
                             for line in self.trace()))
        self.env['ALL_ATTACHED'] = '1'
        (self.binary / 'ip').unlink()
        result = self.run_script('board.sh')
        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertIn('BOARD: board-can NOT RUN: ip is not installed', result.stdout)

    def test_focused_unit_selection(self):
        self.board_fixture()
        self.script(self.root / 'bin/west', 'exit 0')
        output = self.root / 'twister'
        output.mkdir()
        self.env['KFSW_TWISTER_OUT_DIR'] = str(output)
        result = self.run_script('unit.sh', '-s', 'kfsw.services.fwu')
        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertIn('NOT RUN: selection has no recovery build', result.stdout)
        result = self.run_script('unit.sh')
        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn('did not produce the FWU recovery build', result.stdout)

    def test_software_acceptance_counts(self):
        result_file = self.root / 'output.xml'
        result_file.write_text('<robot><suite><test name="optional">'
                               '<status status="SKIP">missing tool</status>'
                               '</test></suite></robot>')
        for acceptance, expected in (('0', 0), ('1', 1)):
            self.env['KFSW_ROBOT_ACCEPTANCE'] = acceptance
            result = subprocess.run([sys.executable, str(self.ci / 'robot-counts.py'),
                                     str(result_file)], env=self.env, capture_output=True, text=True)
            self.assertEqual(result.returncode, expected, result.stdout)
            self.assertIn('executed=0 skipped=1 failed=0', result.stdout)

    def test_proof_failure(self):
        self.board_fixture()
        key = self.root / 'not-a-key'
        key.write_text('fixture')
        self.env['KFSW_MCUBOOT_KEY'] = str(key)
        self.stub('tools/f746-proof.sh', 'exit 7')
        result = self.run_script('board.sh')
        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn('f746-proof (exit 7)', result.stdout)


class RequiredEvidence(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='kfsw-required-')
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.report = self.root / 'output.xml'
        self.manifest = self.root / 'required.json'
        self.case = {'id': 'startup', 'suite': ['Hil', 'Boot'], 'test': 'Ready',
                     'source': 'tests/hil/boot.robot'}
        self.selection = {'version': 1, 'required': [self.case]}
        self.manifest.write_text(json.dumps(self.selection))

    def write_report(self, cases):
        root = ET.Element('robot')
        suites = {}
        for identity, status in cases:
            parent = root
            for depth, name in enumerate(identity[:-1], 1):
                path = tuple(identity[:depth])
                if path not in suites:
                    suite = ET.SubElement(parent, 'suite', name=name)
                    ET.SubElement(suite, 'status', status='PASS')
                    suites[path] = suite
                parent = suites[path]
            test = ET.SubElement(parent, 'test', name=identity[-1])
            ET.SubElement(test, 'status', status=status)
        ET.ElementTree(root).write(self.report)
        return root

    def check(self, *options, required=True):
        arguments = [sys.executable, str(REPO / 'tools/ci/robot-counts.py'), str(self.report)]
        if required:
            arguments += ['--required', str(self.manifest)]
        return subprocess.run(arguments + list(options), text=True, capture_output=True,
                              env=dict(os.environ, KFSW_ROBOT_ACCEPTANCE='0'), timeout=5)

    def test_complete_selection(self):
        self.write_report([(('Hil', 'Boot', 'Ready'), 'PASS'),
                           (('Hil', 'Optional', 'Other'), 'SKIP')])
        result = self.check()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn('REQUIRED CASES: PASS 1', result.stdout)

    def test_required_case_must_be_present_and_pass_once(self):
        for states in ([], ['SKIP'], ['FAIL'], ['PASS', 'PASS'], ['PASS', 'SKIP']):
            with self.subTest(states=states):
                self.write_report([(('Hil', 'Optional', 'Other'), 'PASS')] +
                                  [(('Hil', 'Boot', 'Ready'), state) for state in states])
                result = self.check('--board')
                self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
                self.assertIn('startup', result.stdout)

    def test_equal_test_names_in_different_suites_are_distinct(self):
        self.write_report([(('Hil', 'Boot', 'Ready'), 'PASS'),
                           (('Hil', 'Other', 'Ready'), 'SKIP')])
        self.assertEqual(self.check().returncode, 0)
        self.write_report([(('Hil', 'Other', 'Ready'), 'PASS')])
        self.assertEqual(self.check().returncode, 1)
        self.write_report([(('Hil.Boot', 'Ready'), 'PASS')])
        self.assertEqual(self.check().returncode, 1)

    def test_explicit_empty_manifest_does_not_disable_the_gate(self):
        self.write_report([(('Hil', 'Optional', 'Other'), 'PASS')])
        result = self.check('--required', '')
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn('ROBOT COUNTS ERROR', result.stderr)

    def test_duplicate_suite_paths_are_ambiguous(self):
        root = self.write_report([(('Hil', 'Boot', 'Ready'), 'PASS')])
        suite = root.find('suite/suite')
        root.find('suite').append(ET.fromstring(ET.tostring(suite)))
        ET.ElementTree(root).write(self.report)
        self.assertEqual(self.check().returncode, 1)

    def test_malformed_reports_fail(self):
        reports = ['<robot>', '<results/>', '<robot/>',
                   '<robot><suite><test name="Ready"><status status="PASS"/>'
                   '</test></suite></robot>']
        for xml in reports:
            with self.subTest(xml=xml):
                self.report.write_text(xml)
                self.assertEqual(self.check().returncode, 1)
        for state in ('', 'NOT RUN', 'UNKNOWN'):
            self.write_report([(('Hil', 'Boot', 'Ready'), state)])
            self.assertEqual(self.check().returncode, 1)
        for bad_status in ('missing', 'duplicate'):
            root = self.write_report([(('Hil', 'Boot', 'Ready'), 'PASS')])
            test = root.find('.//test')
            if bad_status == 'missing':
                test.remove(test.find('status'))
            else:
                ET.SubElement(test, 'status', status='PASS')
            ET.ElementTree(root).write(self.report)
            self.assertEqual(self.check().returncode, 1)
        self.report.unlink()
        self.assertEqual(self.check().returncode, 1)

    def test_failed_suite_cannot_be_accepted(self):
        for status in ('FAIL', 'SKIP'):
            with self.subTest(status=status):
                root = self.write_report([(('Hil', 'Boot', 'Ready'), 'PASS')])
                root.find('suite/status').set('status', status)
                ET.ElementTree(root).write(self.report)
                self.assertEqual(self.check().returncode, 1)

    def test_malformed_or_empty_manifests_fail(self):
        self.write_report([(('Hil', 'Boot', 'Ready'), 'PASS')])
        for manifest in ([], {}, {'version': True, 'required': [self.case]},
                         {'version': 2, 'required': [self.case]},
                         {'version': 1, 'required': []},
                         {'version': 1, 'required': [self.case, self.case]},
                         {'version': 1, 'required': [self.case, dict(self.case, id='other')]},
                         {'version': 1, 'required': [self.case, dict(self.case, test='Other')]},
                         {'version': 1, 'required': [dict(self.case, suite='Hil.Boot')]},
                         {'version': 1, 'required': [dict(self.case, test='')]},
                         {'version': 1, 'required': [dict(self.case, test=None)]}):
            with self.subTest(manifest=manifest):
                self.manifest.write_text(json.dumps(manifest))
                self.assertEqual(self.check().returncode, 1)
        for text in ('{', '{"version": 1, "version": 1, "required": []}'):
            self.manifest.write_text(text)
            self.assertEqual(self.check().returncode, 1)
        self.manifest.unlink()
        self.assertEqual(self.check().returncode, 1)

    def test_defaults_keep_optional_and_empty_report_behavior(self):
        self.write_report([(('Hil', 'Optional', 'Other'), 'SKIP')])
        self.assertEqual(self.check(required=False).returncode, 0)
        self.assertEqual(self.check('--board', required=False).returncode, 3)
        self.report.write_text('<robot><suite/></robot>')
        self.assertEqual(self.check(required=False).returncode, 0)
        self.assertEqual(self.check('--board', required=False).returncode, 3)

    def test_seed_manifest_references_exist(self):
        identifiers = set()
        for path in sorted((REPO / 'tests/requirements').glob('*.json')):
            manifest = json.loads(path.read_text())
            for case in manifest['required']:
                self.assertNotIn(case['id'], identifiers)
                identifiers.add(case['id'])
                source = REPO / case['source']
                self.assertTrue(source.is_file(), source)
                self.assertEqual(source.read_text().splitlines().count(case['test']), 1)
                self.assertEqual(case['suite'], ['Hil', source.stem.replace('_', ' ').title()])
            self.manifest.write_text(json.dumps(manifest))
            self.write_report([(tuple(case['suite']) + (case['test'],), 'PASS')
                               for case in manifest['required']])
            self.assertEqual(self.check().returncode, 0)
        self.assertEqual(len(identifiers), 4)
        mapping = REPO / 'tests/requirements/README.md'
        for target in re.findall(r'\]\(([^)]+)\)', mapping.read_text()):
            self.assertTrue((mapping.parent / target).is_file(), target)
        health_case = 'test_a_component_is_late_before_it_is_overdue'
        self.assertIn(health_case, mapping.read_text())
        self.assertIn(f'ZTEST(services_health, {health_case})',
                      (REPO / 'tests/unit/services_health/src/main.c').read_text())

    def binding_fixture(self):
        self.write_report([(('Hil', 'Boot', 'Ready'), 'PASS')])
        self.selection['evidence_kind'] = 'hosted'
        self.manifest.write_text(json.dumps(self.selection))
        self.image = self.root / 'zephyr.exe'
        self.image.write_bytes(b'fixture executable image')
        self.config = self.root / '.config'
        self.config.write_bytes(b'CONFIG_KFSW_PARAM_TEST_DEFINITIONS=y\n')
        self.context = self.root / 'expected-context.json'
        self.expected = {
            'version': 1, 'sources': {'k-fsw': 'a' * 40, 'kfsw-services': 'b' * 40},
            'snapshot_sha256': 'c' * 64, 'profile': 'linux-robot',
            'config_sha256': hashlib.sha256(self.config.read_bytes()).hexdigest(),
            'evidence_kind': 'hosted',
        }
        self.context.write_text(json.dumps(self.expected))
        self.binding = self.root / 'binding.json'
        self.record = {
            'version': 1, 'context': self.expected,
            'report_sha256': hashlib.sha256(self.report.read_bytes()).hexdigest(),
            'image_sha256': hashlib.sha256(self.image.read_bytes()).hexdigest(),
        }
        self.binding.write_text(json.dumps(self.record))

    def check_binding(self, *options, required=True):
        return self.check('--binding', str(self.binding), '--image', str(self.image),
                          '--context', str(self.context), '--config', str(self.config),
                          *options, required=required)

    def test_matching_image_binding(self):
        self.binding_fixture()
        result = self.check_binding()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn('IMAGE BINDING: PASS', result.stdout)
        # Clean contexts may omit a snapshot; omission does not prove cleanliness.
        del self.expected['snapshot_sha256']
        self.context.write_text(json.dumps(self.expected))
        self.binding.write_text(json.dumps(self.record))
        self.assertEqual(self.check_binding().returncode, 0)

    def test_image_binding_checks_actual_files(self):
        for artifact in ('report', 'image', 'config'):
            with self.subTest(artifact=artifact):
                self.binding_fixture()
                path = getattr(self, artifact)
                changed = b'\n<!--changed-->\n' if artifact == 'report' else b'\nchanged'
                path.write_bytes(path.read_bytes() + changed)
                result = self.check_binding()
                self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
                self.assertIn('sha256', result.stderr)
                path.unlink()
                self.assertEqual(self.check_binding().returncode, 1)

    def test_image_binding_matches_independent_context(self):
        replacements = {
            'sources': {'k-fsw': 'd' * 40, 'kfsw-services': 'b' * 40},
            'snapshot_sha256': 'd' * 64, 'profile': 'different-profile',
            'config_sha256': 'e' * 64, 'evidence_kind': 'physical',
        }
        for key, value in replacements.items():
            with self.subTest(key=key):
                self.binding_fixture()
                record = json.loads(self.binding.read_text())
                record['context'][key] = value
                self.binding.write_text(json.dumps(record))
                result = self.check_binding()
                self.assertEqual(result.returncode, 1, result.stdout + result.stderr)

    def test_hosted_binding_cannot_satisfy_physical_selection(self):
        self.binding_fixture()
        self.selection['evidence_kind'] = 'physical'
        self.manifest.write_text(json.dumps(self.selection))
        self.assertEqual(self.check_binding().returncode, 1)
        self.selection['evidence_kind'] = 'hosted'
        self.manifest.write_text(json.dumps(self.selection))
        self.assertEqual(self.check_binding('--board').returncode, 1)
        del self.selection['evidence_kind']
        self.manifest.write_text(json.dumps(self.selection))
        self.assertEqual(self.check_binding().returncode, 1)

    def test_malformed_image_bindings_fail(self):
        self.binding_fixture()
        for key in self.record:
            record = dict(self.record)
            del record[key]
            self.binding.write_text(json.dumps(record))
            self.assertEqual(self.check_binding().returncode, 1)
        for key, value in (('version', True), ('version', 2), ('extra', 1),
                           ('report_sha256', ''), ('image_sha256', 'A' * 64),
                           ('report_sha256', True), ('context', [])):
            with self.subTest(key=key, value=value):
                self.binding.write_text(json.dumps(dict(self.record, **{key: value})))
                self.assertEqual(self.check_binding().returncode, 1)
        for content in ('{', '[]', '{"version":1,"version":1}'):
            self.binding.write_text(content)
            self.assertEqual(self.check_binding().returncode, 1)
        self.binding.unlink()
        self.assertEqual(self.check_binding().returncode, 1)

    def test_malformed_expected_context_fails(self):
        self.binding_fixture()
        for key, value in (('version', True), ('sources', {}),
                           ('sources', {'k-fsw': 'short'}),
                           ('sources', {'other': 'a' * 40}),
                           ('snapshot_sha256', None), ('profile', ''),
                           ('config_sha256', 'wrong'), ('evidence_kind', 'unknown'),
                           ('extra', True)):
            with self.subTest(key=key, value=value):
                self.context.write_text(json.dumps(dict(self.expected, **{key: value})))
                self.assertEqual(self.check_binding().returncode, 1)
        self.context.unlink()
        self.assertEqual(self.check_binding().returncode, 1)

    def test_binding_options_require_a_complete_selection(self):
        self.binding_fixture()
        self.assertEqual(self.check('--binding', str(self.binding)).returncode, 2)
        self.assertEqual(self.check('--image', str(self.image)).returncode, 2)
        self.assertEqual(self.check_binding(required=False).returncode, 2)

    def test_binding_does_not_replace_required_case_checks(self):
        self.binding_fixture()
        self.write_report([(('Hil', 'Boot', 'Ready'), 'SKIP')])
        self.record['report_sha256'] = hashlib.sha256(self.report.read_bytes()).hexdigest()
        self.binding.write_text(json.dumps(self.record))
        self.assertEqual(self.check_binding().returncode, 1)


if __name__ == '__main__':
    unittest.main(verbosity=2)
