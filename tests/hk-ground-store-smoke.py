#!/usr/bin/env python3
"""The ground store keeps what came down, and reports what did not.

Driven through the bridge's replay path, so the same code that records a real
pass records this one.
"""

import json
from pathlib import Path
import sqlite3
import struct
import subprocess
import sys
import tempfile

REPO = Path(__file__).resolve().parent.parent
BRIDGE = REPO / "tools" / "ground" / "hk-bridge.py"
HEADER = struct.Struct(">BBHIBB")
NODE = 2
REPORT = 0


def sample(sequence, report=REPORT, seconds=1900000000, count=3, flags=0):
    return HEADER.pack(1, report, sequence, seconds, count, flags) + bytes(
        range(1, count + 1)
    )


def unset_clock_sample(sequence, report):
    """A node whose clock was never set reports zero seconds and says so."""
    return HEADER.pack(1, report, sequence, 0, 3, 2) + bytes(range(1, 4))


def capture(path, series):
    """Each series is one report, so a gap in one is not a jump between two."""
    with open(path, "w", encoding="utf-8") as stream:
        index = 0
        for report, sequences in series:
            for sequence in sequences:
                stream.write(
                    json.dumps(
                        {
                            "version": 1,
                            "node": NODE,
                            "received_ms": 1700000000000 + index,
                            "sample": sample(sequence, report).hex(),
                        }
                    )
                    + "\n"
                )
                index += 1


def replay(capture_path, database, definitions=None):
    command = [
        sys.executable,
        str(BRIDGE),
        "--replay",
        str(capture_path),
        "--store",
        str(database),
        "--yamcs",
        "none",
    ]
    if definitions:
        command += ["--definitions", str(definitions)]
    result = subprocess.run(command, capture_output=True, text=True, timeout=120)
    if result.returncode != 0:
        print(result.stdout)
        print(result.stderr, file=sys.stderr)
        raise SystemExit("the bridge failed to replay into the store")


def fail(message):
    print(f"HK GROUND STORE RESULT: FAIL\n  {message}")
    raise SystemExit(1)


def main():
    with tempfile.TemporaryDirectory() as folder:
        folder = Path(folder)
        database = folder / "beacon.db"
        definitions = folder / "reports.json"
        definitions.write_text('{"report": 0, "fields": ["battery_mv"]}\n', encoding="utf-8")

        # Report 0 loses two samples. Report 1 crosses the sixteen-bit wrap
        # with nothing lost, which must not read as a gap of sixty-five thousand.
        first = folder / "first.jsonl"
        capture(first, [(0, [1, 2, 3, 6]), (1, [65534, 65535, 0, 1])])
        replay(first, database, definitions)

        connection = sqlite3.connect(database)
        kept = connection.execute("SELECT count(*) FROM sample").fetchone()[0]
        if kept != 8:
            fail(f"kept {kept} samples instead of 8")

        gaps = connection.execute(
            "SELECT report, after_sequence, before_sequence, missing FROM gap"
            " ORDER BY report, after_sequence"
        ).fetchall()
        if gaps != [(0, 3, 6, 2)]:
            fail(f"the gap view reported {gaps}")

        # Whether the node knew the time is kept, because a sample stamped with
        # the host's clock is not the same evidence as one stamped by the node.
        if connection.execute(
            "SELECT count(*) FROM sample WHERE clock_set = 1"
        ).fetchone()[0] != 8:
            fail("samples carrying a node time were not recorded as clock set")

        unset = folder / "unset.jsonl"
        with open(unset, "w", encoding="utf-8") as stream:
            stream.write(
                json.dumps(
                    {
                        "version": 1,
                        "node": NODE,
                        "received_ms": 1700000000500,
                        "sample": unset_clock_sample(9, 2).hex(),
                    }
                )
                + "\n"
            )
        replay(unset, database, definitions)
        row = connection.execute(
            "SELECT clock_set, node_seconds FROM sample WHERE report = 2"
        ).fetchone()
        if row != (0, 0):
            fail(f"a sample with no node clock was recorded as {row}")

        definition_rows = connection.execute("SELECT count(*) FROM definition").fetchone()[0]
        if definition_rows != 1:
            fail(f"stored {definition_rows} definitions instead of 1")
        unlinked = connection.execute(
            "SELECT count(*) FROM sample WHERE definition IS NULL"
        ).fetchone()[0]
        if unlinked:
            fail(f"{unlinked} samples were stored with no definition")
        first_definition = connection.execute(
            "SELECT definition FROM sample ORDER BY id LIMIT 1"
        ).fetchone()[0]

        # A definition that changes must not be applied to what came before it.
        definitions.write_text(
            '{"report": 0, "fields": ["battery_mv", "battery_ma"]}\n', encoding="utf-8"
        )
        second = folder / "second.jsonl"
        capture(second, [(0, [7, 8])])
        replay(second, database, definitions)

        definition_rows = connection.execute("SELECT count(*) FROM definition").fetchone()[0]
        if definition_rows != 2:
            fail(f"a changed definition produced {definition_rows} rows instead of 2")
        still_old = connection.execute(
            "SELECT definition FROM sample ORDER BY id LIMIT 1"
        ).fetchone()[0]
        if still_old != first_definition:
            fail("an earlier sample was repointed at the new definition")
        newest = connection.execute(
            "SELECT definition FROM sample ORDER BY id DESC LIMIT 1"
        ).fetchone()[0]
        if newest == first_definition:
            fail("a sample after the change kept the old definition")

        total = connection.execute("SELECT count(*) FROM sample").fetchone()[0]
        connection.close()
        print(
            f"HK GROUND STORE RESULT: PASS {total} samples, {definition_rows} definitions, "
            f"{len(gaps)} gaps reported"
        )
    return 0


if __name__ == "__main__":
    sys.exit(main())
