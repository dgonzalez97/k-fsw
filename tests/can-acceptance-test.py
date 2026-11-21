#!/usr/bin/env python3
"""Check CAN load selection and failure evidence without touching hardware."""

import argparse
import contextlib
import io
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import MagicMock, patch

REPO = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('can_acceptance', REPO / 'tests/hil/fwu/can-acceptance.py')
load = importlib.util.module_from_spec(spec)
spec.loader.exec_module(load)


class Clock:
    def __init__(self):
        self.value = 0

    def __call__(self):
        self.value += .1
        return self.value


class Console:
    def __init__(self, fail=None, node=2, progress=True, remote_node=None):
        self.commands = []
        self.fail = fail or set()
        self.node = node
        self.remote_node = node if remote_node is None else remote_node
        self.progress = progress
        self.hk_reads = 0
        self.beacon_progress = True

    def run(self, command, expected=None, timeout=30):
        self.commands.append(command)
        if command in self.fail:
            raise RuntimeError('fixture failure: ' + command)
        if command == 'hk show':
            self.hk_reads += 1
            count = 10 + (self.hk_reads if self.progress else 0)
            beacons = count if self.beacon_progress else 10
            text = f'collections: {count}\nfailed collections: 0\nabsent values: 0\nbeacons sent: {beacons}\n'
        elif command == 'hil report':
            text = 'hk_collect count=1 max_us=10\nuntracked=0'
        elif command == 'hil start':
            text = 'Timing started'
        elif command == 'kernel thread stacks':
            text = 'unused'
        elif command == 'mcuboot':
            text = 'confirmed: 1'
        elif command == 'csp routes':
            text = '17/14 -> CAN direct' if self.node == 2 else '0/0 -> CAN direct'
        elif command == 'csp interfaces':
            text = f'CAN addr={self.node}/14'
        elif command.startswith('csp ping '):
            text = f'CSP ping {command.split()[-1]}: success'
        elif command.startswith('csp ident '):
            text = 'revision: fixture-one'
        elif command.startswith('param listall '):
            text = 'node_id\nparam_requests_dropped'
        elif command.startswith('param get '):
            text = f'2:node_id = {self.remote_node}\n'
        elif command.startswith('status '):
            text = 'uptime_ms: 10\nstorage: ready\nrevisions:\n  k-fsw: abc123\n'
        elif command.startswith('hk define '):
            parts = command.split()
            text = f'report {parts[2]} defines {len(parts) - 3} values'
        elif command.startswith('hk period '):
            parts = command.split()
            text = f'report {parts[2]} every {parts[3]} ms'
        elif command.startswith('hk beacon '):
            parts = command.split()
            text = (f'report {parts[2]} beacons to node {parts[3]} every {parts[4]} ms'
                    if parts[4] != '0' else f'report {parts[2]} stops beaconing')
        else:
            text = ': PASS'
        if expected is not None:
            load.bench.require(text, expected)
        return text

    def ready(self):
        return ''


class Soak(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='kfsw-can-load-')
        self.addCleanup(self.temporary.cleanup)
        self.output = Path(self.temporary.name)

    def run_soak(self, board, ground, profile='minimal', probes=False):
        with patch.object(load.time, 'monotonic', side_effect=Clock()):
            return load.soak(board, ground, 1, self.output, profile, probes)

    def result(self):
        return json.loads((self.output / 'soak.json').read_text())

    def test_minimal_uses_builtin_data_without_hk_or_probes(self):
        board, ground = Console(), Console()
        self.run_soak(board, ground)
        self.assertEqual(board.commands, [])
        self.assertIn('param get 2 node_id', ground.commands)
        self.assertIn('status 2', ground.commands)
        self.assertIn('ftp verify /soak-source.bin /soak-copy.bin', ground.commands)
        self.assertFalse(any('temp_' in text or '51:' in text for text in ground.commands))
        result = self.result()
        self.assertEqual(result['result'], 'PASS')
        self.assertGreater(result['transfer_cycles'], 0)
        self.assertEqual(result['reads'], result['control_reads'])
        self.assertIn('interleaved', result['latency_method'])
        operations = [json.loads(line) for line in (self.output / 'operations.jsonl').read_text().splitlines()]
        self.assertEqual(len(operations), 6)
        self.assertTrue(all(operation['result'] == 'PASS' for operation in operations))

    def test_obc_uses_ground17_and_requires_hk_progress(self):
        board, ground = Console(), Console()
        self.run_soak(board, ground, 'obc')
        self.assertIn('hk define 1 17:1:0 17:5:0', board.commands)
        self.assertIn('hk beacon 0 17 5000', board.commands)
        self.assertIn('hk beacon 0 17 0', board.commands)
        self.assertFalse(any('16:' in text or '51:' in text or text.startswith('hil ')
                             for text in board.commands))
        self.assertGreater(self.result()['hk_collections_delta'], 0)

    def test_wrong_node_value_fails_with_partial_summary(self):
        with self.assertRaisesRegex(RuntimeError, 'node_id'):
            self.run_soak(Console(), Console(node=20))
        result = self.result()
        self.assertEqual(result['result'], 'FAIL')
        self.assertEqual(result['operations']['param']['failed'], 1)

    def test_primary_and_cleanup_failures_both_survive(self):
        board = Console(fail={'hk period 0 0'})
        ground = Console(fail={'ftp put 2 /soak-source.bin /soak.bin'})
        with self.assertRaises(RuntimeError):
            self.run_soak(board, ground, 'obc')
        result = self.result()
        self.assertIn('ftp put', result['errors'][0])
        self.assertTrue(any('hk period 0 0' in error for error in result['errors']))
        self.assertIn('hk period 1 0', board.commands)
        self.assertIn('hk beacon 0 17 0', board.commands)
        self.assertEqual(result['transfer_cycles'], 0)

    def test_missing_hk_progress_fails(self):
        with self.assertRaisesRegex(RuntimeError, 'housekeeping'):
            self.run_soak(Console(progress=False), Console(), 'obc')
        self.assertEqual(self.result()['result'], 'FAIL')


    def test_probe_failure_does_not_skip_remaining_cleanup(self):
        board = Console(fail={'hil report'})
        with self.assertRaisesRegex(RuntimeError, 'hil report'):
            self.run_soak(board, Console(), 'obc', True)
        self.assertIn('kernel thread stacks', board.commands)
        self.assertIn('hk beacon 0 17 0', board.commands)
        self.assertEqual(self.result()['result'], 'FAIL')

    def test_setup_failure_still_writes_summary(self):
        ground = Console(fail={'ftp generate /soak-source.bin 4096'})
        with self.assertRaisesRegex(RuntimeError, 'ftp generate'):
            self.run_soak(Console(), ground)
        self.assertEqual(self.result()['transfer_cycles'], 0)
        self.assertEqual(self.result()['duration_active_s'], 0)

    def test_no_active_work_and_invalid_minimal_probes_fail(self):
        board, ground = Console(), Console()
        with patch.object(load.time, 'monotonic', side_effect=[0, 1, 3, 4, 5]):
            with self.assertRaisesRegex(RuntimeError, 'No complete'):
                load.soak(board, ground, 1, self.output, 'minimal')
        self.assertEqual(self.result()['result'], 'FAIL')
        with self.assertRaises(ValueError):
            load.soak(board, ground, 1, self.output, 'minimal', True)

    def test_keyboard_interrupt_preserves_failed_summary(self):
        ground = Console()
        original = ground.run
        def interrupt(command, *args, **kwargs):
            if command.startswith('status '):
                raise KeyboardInterrupt()
            return original(command, *args, **kwargs)
        ground.run = interrupt
        with self.assertRaises(KeyboardInterrupt):
            self.run_soak(Console(), ground)
        self.assertEqual(self.result()['result'], 'FAIL')


    def test_cleanup_interrupt_finishes_remaining_cleanup_and_summary(self):
        board = Console()
        original = board.run
        def interrupt(command, *args, **kwargs):
            if command == 'hk period 0 0':
                board.commands.append(command)
                raise KeyboardInterrupt()
            return original(command, *args, **kwargs)
        board.run = interrupt
        with self.assertRaises(KeyboardInterrupt):
            self.run_soak(board, Console(), 'obc')
        self.assertIn('hk period 1 0', board.commands)
        self.assertIn('hk beacon 0 17 0', board.commands)
        result = self.result()
        self.assertEqual(result['result'], 'FAIL')
        self.assertIn('duration_measured_s', result)
        self.assertTrue(any('KeyboardInterrupt' in error for error in result['errors']))


    def test_long_inflight_transfer_requires_beacon_progress(self):
        board, ground, clock = Console(), Console(), Clock()
        board.beacon_progress = False
        original = ground.run
        def slow(command, *args, **kwargs):
            if '/boot/firmware_1.bin' in command:
                clock.value += 6
            return original(command, *args, **kwargs)
        ground.run = slow
        with patch.object(load.time, 'monotonic', side_effect=clock):
            with self.assertRaisesRegex(RuntimeError, 'beacon progress'):
                load.soak(board, ground, 1, self.output, 'obc')
        self.assertGreater(self.result()['duration_active_s'], 5)
        self.assertEqual(self.result()['result'], 'FAIL')

    def test_short_run_does_not_require_beacon_before_its_period(self):
        board = Console()
        board.beacon_progress = False
        self.run_soak(board, Console(), 'obc')
        self.assertLess(self.result()['duration_active_s'], 5)
        self.assertEqual(self.result()['result'], 'PASS')
        self.assertEqual(self.result()['hk_beacons_delta'], 0)


class Inputs(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='kfsw-can-inputs-')
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.ground = self.root / 'ground'
        self.ground.mkdir()
        self.executable = self.ground / 'zephyr.exe'
        self.executable.write_bytes(b'ground fixture')
        self.executable.chmod(0o700)
        (self.ground / '.config').write_text(
            'CONFIG_KFSW_CSP_ADDRESS=17\nCONFIG_KFSW_CSP_CAN=y\n'
            'CONFIG_KFSW_CSP_VERSION_2=y\nCONFIG_KFSW_CSP_CAN_BITRATE=500000\n'
            'CONFIG_KFSW_CSP_ROUTE_TABLE="0/0 CAN"\n')
        self.flash = self.root / 'flash.bin'
        self.flash.write_bytes(b'flash fixture')
        self.flight_image = self.root / 'flight.bin'
        self.flight_image.write_bytes(b'baseline fixture')
        self.flight_config = self.root / 'flight.config'
        self.context = self.root / 'context.json'
        spec = json.loads((REPO / 'tests/requirements/profiles/reference.json').read_text())
        settings = spec['common'] | spec['profiles']['minimal-l496']
        self.flight_config.write_text(''.join(f'{key}={value}\n' for key, value in settings.items()))
        self.metadata = {'version': 1, 'sources': {'k-fsw': 'a' * 40},
                         'profile': 'minimal-l496', 'evidence_kind': 'physical',
                         'config_sha256': hashlib.sha256(self.flight_config.read_bytes()).hexdigest()}
        self.context.write_text(json.dumps(self.metadata))
        self.args = argparse.Namespace(ground=self.executable, flash=self.flash,
            image=None, flight_image=self.flight_image, flight_config=self.flight_config,
            context=self.context, profile='minimal', probes=False, interruptions=False,
            seconds=60, serial='/dev/serial/by-id/fixture')

    def test_actual_artifacts_hashed_without_candidate_confusion(self):
        data, record = load.prepare_inputs(self.args)
        self.assertEqual(record['selection'], 'minimal-l496')
        self.assertEqual(data['flight-image.bin'], b'baseline fixture')
        self.assertNotIn('candidate.bin', data)
        self.assertEqual(record['inputs']['ground.exe']['sha256'],
                         hashlib.sha256(b'ground fixture').hexdigest())
        self.assertIn('do not attest', record['identity_limit'])
        self.assertIn('reference-requirements.json', data)
        self.assertEqual(record['expected_config']['CONFIG_KFSW_HK'], 'n')

    def test_context_hash_profile_and_kind_must_match(self):
        for name, value in [('config_sha256', 'f' * 64), ('profile', 'obc-l496'),
                            ('evidence_kind', 'hosted')]:
            with self.subTest(name=name):
                self.context.write_text(json.dumps(self.metadata | {name: value}))
                with self.assertRaises(ValueError):
                    load.prepare_inputs(self.args)
        self.context.write_text('{"version": 1, "version": 1}')
        with self.assertRaisesRegex(ValueError, 'duplicate'):
            load.prepare_inputs(self.args)

    def test_resolved_configuration_mismatch_fails(self):
        self.flight_config.write_text(self.flight_config.read_text().replace(
            'CONFIG_KFSW_HK=n', 'CONFIG_KFSW_HK=y'))
        with self.assertRaisesRegex(ValueError, 'CONFIG_KFSW_HK'):
            load.prepare_inputs(self.args)

    def test_wrong_ground_address_fails_before_runtime(self):
        path = self.ground / '.config'
        path.write_text(path.read_text().replace('ADDRESS=17', 'ADDRESS=16'))
        with self.assertRaisesRegex(ValueError, 'Ground CONFIG_KFSW_CSP_ADDRESS'):
            load.prepare_inputs(self.args)

    def test_missing_baseline_artifact_is_not_accepted(self):
        self.flight_image.unlink()
        with self.assertRaises(OSError):
            load.prepare_inputs(self.args)
        self.args.flight_image = None
        with self.assertRaisesRegex(ValueError, 'flight-image'):
            load.prepare_inputs(self.args)

    def test_instrumented_selection_has_distinct_context(self):
        spec = json.loads((REPO / 'tests/requirements/profiles/reference.json').read_text())
        settings = spec['common'] | spec['profiles']['obc-l496']
        settings['CONFIG_KFSW_HIL_PROBES'] = 'y'
        self.flight_config.write_text(''.join(f'{key}={value}\n' for key, value in settings.items()))
        self.args.profile, self.args.probes = 'obc', True
        context = self.metadata | {'profile': 'obc-l496-probes',
            'config_sha256': hashlib.sha256(self.flight_config.read_bytes()).hexdigest()}
        self.context.write_text(json.dumps(context))
        _, record = load.prepare_inputs(self.args)
        self.assertTrue(record['derived_instrumented_selection'])
        self.assertEqual(record['selection'], 'obc-l496-probes')
        self.context.write_text(json.dumps(context | {'profile': 'obc-l496'}))
        with self.assertRaises(ValueError):
            load.prepare_inputs(self.args)

    def test_legacy_interruption_only_keeps_candidate_separate(self):
        self.args.seconds = 0
        self.args.profile = None
        self.args.interruptions = True
        self.args.image = self.flight_image
        data, record = load.prepare_inputs(self.args)
        self.assertIn('candidate.bin', data)
        self.assertNotIn('flight-image.bin', data)
        self.assertIsNone(record['context'])

    def test_cli_negative_paths_do_not_open_devices_or_create_output(self):
        output = self.root / 'must-not-exist'
        base = ['can-acceptance', '--ground', str(self.executable), '--flash', str(self.flash),
                '--serial', self.args.serial, '--output', str(output)]
        for extra in (['--seconds', '0'], ['--seconds', '-1'], ['--seconds', '1'],
                      ['--profile', 'minimal', '--probes'],
                      ['--profile', 'minimal', '--seconds', '1']):
            with self.subTest(extra=extra), patch.object(load.sys, 'argv', base + extra), \
                    patch.object(load.serial, 'Serial') as serial_open, \
                    patch.object(load.subprocess, 'Popen') as launch, \
                    patch.object(load.bench, 'can_state') as controller, \
                    contextlib.redirect_stderr(io.StringIO()):
                with self.assertRaises(SystemExit) as error:
                    load.main()
                self.assertEqual(error.exception.code, 2)
                serial_open.assert_not_called()
                launch.assert_not_called()
                controller.assert_not_called()
                self.assertFalse(output.exists())

    def test_can_state_and_bitrate_rejected(self):
        for state, bitrate in [('BUS-OFF', 500000), ('ERROR-ACTIVE', 1000000), ('STOPPED', 500000)]:
            with self.subTest(state=state, bitrate=bitrate):
                with self.assertRaises(RuntimeError):
                    load.check_can({'linkinfo': {'info_data': {
                        'state': state, 'bittiming': {'bitrate': bitrate}}}})
        load.check_can({'linkinfo': {'info_data': {
            'state': 'ERROR-ACTIVE', 'bittiming': {'bitrate': 500000}}}})


    def run_main(self, board=None, after=None):
        output = self.root / 'run'
        argv = ['can-acceptance', '--ground', str(self.executable), '--flash', str(self.flash),
                '--serial', self.args.serial, '--output', str(output), '--profile', 'minimal',
                '--seconds', '1', '--flight-image', str(self.flight_image),
                '--flight-config', str(self.flight_config), '--context', str(self.context)]
        state = {'linkinfo': {'info_data': {'state': 'ERROR-ACTIVE', 'bittiming': {'bitrate': 500000}}}}
        with patch.object(load.sys, 'argv', argv), \
                patch.object(load.serial, 'Serial'), \
                patch.object(load.bench, 'Console', side_effect=[board or Console(), Console(node=17, remote_node=2)]), \
                patch.object(load.bench, 'stop_process'), \
                patch.object(load.subprocess, 'Popen', return_value=MagicMock()) as launch, \
                patch.object(load.bench, 'can_state', side_effect=[state, after or state]), \
                patch.object(load.time, 'monotonic', side_effect=Clock()), \
                contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            result = load.main()
        return result, output, launch

    def test_main_launches_copied_ground_as17(self):
        code, output, launch = self.run_main()
        self.assertEqual(code, 0)
        self.assertEqual(launch.call_args.args[0][0], str(output / 'ground.exe'))
        self.assertIn('--device_id=17', launch.call_args.args[0])
        self.assertEqual(json.loads((output / 'run.json').read_text())['result'], 'PASS')
        self.assertTrue((output / 'can-after.json').exists())

    def test_bad_after_state_cannot_report_success(self):
        code, output, _ = self.run_main(after={'linkinfo': {'info_data': {
            'state': 'BUS-OFF', 'bittiming': {'bitrate': 500000}}}})
        self.assertEqual(code, 1)
        result = json.loads((output / 'run.json').read_text())
        self.assertEqual(result['result'], 'FAIL')
        self.assertIn('CAN after run', result['errors'][0])

    def test_main_records_interrupt_before_reraising(self):
        board = Console()
        original = board.run
        def interrupt(command, *args, **kwargs):
            if command == 'mcuboot':
                raise KeyboardInterrupt()
            return original(command, *args, **kwargs)
        board.run = interrupt
        with self.assertRaises(KeyboardInterrupt):
            self.run_main(board=board)
        result = json.loads((self.root / 'run/run.json').read_text())
        self.assertEqual(result['result'], 'FAIL')
        self.assertIn('KeyboardInterrupt', result['errors'])
        self.assertTrue((self.root / 'run/can-after.json').exists())


if __name__ == '__main__':
    unittest.main()
