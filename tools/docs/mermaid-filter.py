#!/usr/bin/env python3
"""Doxygen input filter: a ```mermaid fence becomes a <pre class="mermaid">.

Doxygen prints an unknown fence as plain code, so the diagram would show as
its source. The fence stays in the Markdown, where GitHub draws it, and the
manual gets the block the renderer looks for. Every line keeps its number,
so Doxygen's warnings still point at the right place.
"""

import html
import sys


def convert(lines):
    inside = False
    for line in lines:
        stripped = line.strip()
        if not inside and stripped == "```mermaid":
            inside = True
            yield '\\htmlonly<pre class="mermaid">\n'
        elif inside and stripped == "```":
            inside = False
            yield "</pre>\\endhtmlonly\n"
        elif inside:
            yield html.escape(line, quote=False)
        else:
            yield line
    if inside:
        raise SystemExit("unterminated mermaid fence")


def main():
    with open(sys.argv[1], encoding="utf-8") as source:
        sys.stdout.writelines(convert(source))


if __name__ == "__main__":
    main()
