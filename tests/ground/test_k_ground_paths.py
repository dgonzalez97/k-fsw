"""Ground profiles sharing a node must retain separate build configurations."""

import os
from pathlib import Path
import subprocess
import tempfile
import unittest


REPO = Path(__file__).resolve().parents[2]


class GroundBuildPathsTest(unittest.TestCase):
    def test_profile_node_and_csp_version_isolation(self):
        with tempfile.TemporaryDirectory(prefix='k-ground-paths.') as temporary:
            root = Path(temporary)
            nodes = root / 'station/nodes'
            nodes.mkdir(parents=True)
            west = root / 'west'
            west.write_text('#!/bin/bash\n'
                            'printf "%s\\n" "$KFSW_BUILD_DIR" "$KFSW_EXTRA_CONF_FILE"\n')
            west.chmod(0o755)
            environment = dict(os.environ, PATH=f'{root}:{os.environ["PATH"]}',
                               KFSW_VENV_DIR=str(root / 'no-venv'),
                               KGROUND_STATION_DIR=str(nodes.parent),
                               KGROUND_BUILD_ROOT=str(root / 'build'))
            configs = {}
            for version in (2, 1):
                for role in ('kfsw-gnd-can', 'kfsw-gnd-uhf', 'kfsw-gnd-uhf-bench'):
                    (nodes / f'{role}.env').write_text(
                        f'KFSW_ROLE={role}\nKFSW_CSP_NODE=16\nKFSW_CSP_PEER=2\n'
                        f'KFSW_CSP_VERSION={version}\n')
                    suffix = '-csp1' if version == 1 else ''
                    for node in (16, 17):
                        result = subprocess.run(
                            [str(REPO / 'tools/k-ground'), 'build', role, '--node', str(node)],
                            env=environment, text=True, capture_output=True, timeout=10)
                        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                        basename = f'{role}-node-{node}{suffix}'
                        build = root / 'build' / basename
                        config = root / 'build/config' / f'{basename}.conf'
                        self.assertIn(f'{build}\n{config}\n', result.stdout)
                        content = config.read_text()
                        self.assertIn(f'CONFIG_KFSW_ROLE="{role}"', content)
                        self.assertIn(f'CONFIG_KFSW_CSP_ADDRESS={node}\n', content)
                        self.assertEqual('CONFIG_KFSW_CSP_VERSION_1=y' in content, version == 1)
                        configs[config] = content
            for config, content in configs.items():
                self.assertEqual(config.read_text(), content)
            result = subprocess.run(
                [str(REPO / 'tools/k-ground'), 'build', 'kfsw-gnd-uhf-bench', '--node', '31'],
                env=environment, text=True, capture_output=True, timeout=10)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('CSP 1 range', result.stderr)


if __name__ == '__main__':
    unittest.main()
