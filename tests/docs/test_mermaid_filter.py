"""Mermaid fences in the manual's Doxygen input filter."""

import importlib.util
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("mermaid_filter", ROOT / "tools/docs/mermaid-filter.py")
FILTER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(FILTER)


class MermaidFilter(unittest.TestCase):
    def test_fence_becomes_an_escaped_block_with_the_same_line_count(self):
        source = ["Text\n", "```mermaid\n", 'A["a"] --> B\n', "```\n", "More\n"]
        output = list(FILTER.convert(source))
        self.assertEqual(len(output), len(source))
        self.assertEqual(output[1], '\\htmlonly<pre class="mermaid">\n')
        self.assertEqual(output[2], 'A["a"] --&gt; B\n')
        self.assertEqual(output[3], "</pre>\\endhtmlonly\n")
        self.assertEqual(output[4], "More\n")

    def test_other_fences_are_left_alone(self):
        source = ["```text\n", "a --> b\n", "```\n"]
        self.assertEqual(list(FILTER.convert(source)), source)

    def test_unterminated_fence_is_refused(self):
        with self.assertRaises(SystemExit):
            list(FILTER.convert(["```mermaid\n", "A --> B\n"]))


if __name__ == "__main__":
    unittest.main()
