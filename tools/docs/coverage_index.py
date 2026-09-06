#!/usr/bin/env python3
"""Write the landing page for the per-repository coverage reports.

One number for the whole workspace would hide a thin layer behind a well
covered one, so the reports are separate and this page only gathers them.
"""

import json
import pathlib
import sys

ORDER = ["k-fsw", "kfsw-platform", "kfsw-services", "kfsw-comms", "kfsw-modules"]

OWNS = {
    "k-fsw": "Composition, core parameter tables, shell adapters",
    "kfsw-platform": "Time, storage, reset cause, watchdog, last words",
    "kfsw-services": "Log, parameters, files, events, commands, health, update",
    "kfsw-comms": "CSP lifecycle, routing, KISS and CAN transports",
    "kfsw-modules": "Device and subsystem modules",
}


def band(percent):
    """Colour by how much of the layer the unit suites reach."""
    if percent >= 75.0:
        return "#28a96b"
    if percent >= 50.0:
        return "#c8912a"
    return "#c05545"


def main():
    root = pathlib.Path(sys.argv[1])
    rows = []

    for name in ORDER:
        summary = root / name / "summary.json"
        if not summary.is_file():
            continue
        data = json.loads(summary.read_text())
        percent = data.get("line_percent", 0.0)
        rows.append(
            "<tr>"
            f'<td><a href="{name}/index.html">{name}</a></td>'
            f"<td>{OWNS.get(name, '')}</td>"
            f'<td class="n" style="color:{band(percent)}"><b>{percent:.1f}%</b></td>'
            f'<td class="n">{data.get("line_covered", 0)} / {data.get("line_total", 0)}</td>'
            "</tr>"
        )

    print(
        """<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>K-FSW coverage</title>
<style>
  body { font: 15px/1.6 -apple-system, Segoe UI, Helvetica, sans-serif;
         max-width: 52rem; margin: 3rem auto; padding: 0 1.5rem; color: #1c2b24; }
  h1 { font-size: 1.5rem; margin-bottom: .25rem; }
  p { color: #4a5a53; }
  table { border-collapse: collapse; width: 100%; margin: 1.5rem 0; }
  th, td { text-align: left; padding: .6rem .5rem; border-bottom: 1px solid #dde5e1; }
  th { font-size: .8rem; text-transform: uppercase; letter-spacing: .04em; color: #6b7a73; }
  td.n { text-align: right; font-variant-numeric: tabular-nums; white-space: nowrap; }
  a { color: #1c7a4d; }
  .note { font-size: .9rem; border-left: 3px solid #dde5e1; padding-left: 1rem; }
  @media (prefers-color-scheme: dark) {
    body { background: #12201b; color: #dce8e2; }
    p, th { color: #93a89f; }
    th, td { border-bottom-color: #24382f; }
    a { color: #4fc98a; }
    .note { border-left-color: #24382f; }
  }
</style>
</head>
<body>
<h1>K-FSW coverage</h1>
<p>Line coverage of the unit suites, one report per repository.</p>
<table>
<tr><th>Repository</th><th>Owns</th><th>Lines</th><th>Covered</th></tr>
"""
        + "\n".join(rows)
        + """
</table>
<p class="note">
These are the <b>unit</b> suites only. The integration scripts and the
hardware-in-the-loop suites exercise a great deal more, but they drive a built
image rather than instrumented objects, so counting them here would claim a
coverage these numbers do not describe. A low figure means a layer is tested
mostly on a bench, not that it is untested.
</p>
<p><a href="../index.html">Back to the K-FSW manual</a></p>
</body>
</html>"""
    )


if __name__ == "__main__":
    main()
