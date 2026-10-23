#!/usr/bin/env python3
"""Exercise CI selection and board accounting without hardware."""

import re
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

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
        stages = ('build', 'quality', 'unit', 'integration', 'csp-v1',
                  'valgrind', 'robot', 'docs', 'board')
        for stage in stages:
            self.script(self.ci / f'{stage}.sh', f'echo {stage} >> "$TRACE"')
        self.script(self.ci / 'golden.py', 'exit 0')
        (self.ci / 'golden.py').write_text('pass\n')
        for name in ('tools/zephyr-patch.sh', 'tests/zephyr-patch.sh', 'tools/docs/icd.py'):
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


if __name__ == '__main__':
    unittest.main(verbosity=2)
