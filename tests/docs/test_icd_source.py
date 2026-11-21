"""Command claims and parameter modes in the source ICD."""

import importlib.util
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("icd_source", ROOT / "tools/docs/icd-source.py")
ICD = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(ICD)


class IcdSource(unittest.TestCase):
    def test_hk_commands_require_a_claim(self):
        with tempfile.TemporaryDirectory() as directory:
            workspace = Path(directory)
            (workspace / "k-fsw").symlink_to(ROOT, target_is_directory=True)
            commands = {row["name"]: row for row in ICD.collect_commands(workspace)}
        for name in ("hk_define", "hk_period", "hk_clear"):
            with self.subTest(command=name):
                self.assertTrue(commands[name]["mutating"])
                self.assertIn(f"`{name}`", ICD.render_commands([commands[name]]))
                self.assertIn("| yes |", ICD.render_commands([commands[name]]))
        self.assertFalse(commands["info"]["mutating"])

    def test_parameter_callbacks_imply_live_mode(self):
        with tempfile.TemporaryDirectory() as directory:
            workspace = Path(directory)
            app = workspace / "k-fsw/app"
            app.mkdir(parents=True)
            source = app / "parameters.c"
            for callback in ("apply", "changed", "changed_text", "changed_data"):
                for value, expected in (("handler", "wl"), ("NULL", "wb"), ("0", "wb")):
                    with self.subTest(callback=callback, value=value):
                        source.write_text(
                            'static const struct kfsw_param_definition params[] = {\n'
                            f'{{.name = "example", .{callback} = {value}}},\n'
                            '};\n'
                        )
                        _, arrays = ICD.collect_parameters(workspace)
                        entry = arrays[("k-fsw/app/parameters.c", "params")][0]
                        self.assertEqual(ICD.flags_text(entry.get("flags")), expected)

    def test_parameter_modes_match_shell_write_semantics(self):
        for flags, expected in (
            ("", "wb"),
            ("READ_ONLY", "r"),
            ("PERSISTENT", "wpb"),
            ("READ_ONLY PERSISTENT", "rp"),
            ("LIVE", "wl"),
            ("PERSISTENT LIVE", "wpl"),
            ("READ_ONLY LIVE", "rl"),
            ("READ_ONLY PERSISTENT LIVE", "rpl"),
        ):
            with self.subTest(flags=flags):
                raw = " | ".join("KFSW_PARAM_FLAG_" + flag for flag in flags.split())
                self.assertEqual(ICD.flags_text(raw), expected)


if __name__ == "__main__":
    unittest.main()
