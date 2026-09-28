"""Printable-guide links, including Pandoc's file-scoped identifiers."""

import importlib.util
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("check_links", ROOT / "tools/docs/check-links.py")
CHECK = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(CHECK)


class PdfLinks(unittest.TestCase):
    def test_missing_anchor_is_rejected(self):
        guide = CHECK.Guide()
        guide.feed('<a href="#missing">link</a>')
        self.assertEqual(guide.check(), ["missing anchor: #missing"])

    def test_duplicate_ids_and_local_markdown_are_rejected(self):
        guide = CHECK.Guide()
        guide.feed('<h1 id="same"></h1><h2 id="same"></h2><a href="guide.md">link</a>')
        self.assertEqual(len(guide.check()), 2)

    def test_scoped_chapters_and_repeated_headings_resolve(self):
        with tempfile.TemporaryDirectory() as folder:
            directory = Path(folder)
            sources = []
            for chapter in ("commands", "communications"):
                path = directory / chapter / "index.md"
                path.parent.mkdir()
                path.write_text(
                    f"# {chapter} {{#{chapter}}}\n\n"
                    "@ref communications.\n\n## Shared heading\n\n"
                    "[local](#shared-heading)\n\n"
                    "[other](../communications/index.md#shared-heading)\n",
                    encoding="utf-8",
                )
                sources.append(str(path))
            result = subprocess.run(
                ["pandoc", "--file-scope", "--from=markdown-citations", "--to=html",
                 f"--lua-filter={ROOT / 'docs/pdf/links.lua'}", *sources],
                check=True, capture_output=True, text=True,
            )
        guide = CHECK.Guide()
        guide.feed(result.stdout)
        self.assertEqual(guide.check(), [])
        self.assertIn("commands-shared-heading", guide.ids)
        self.assertIn("communications-shared-heading", guide.ids)


if __name__ == "__main__":
    unittest.main()
