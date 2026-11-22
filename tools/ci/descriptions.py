#!/usr/bin/env python3
"""Refuse a parameter or table description longer than the wire allows.

The cap lives in kfsw-services/include/kfsw/services/parameter.h; this reads it
from there so the two cannot drift.
"""

import codecs
import importlib.util
import pathlib
import re
import sys
import warnings

WORKSPACE = pathlib.Path(__file__).resolve().parents[3]
HEADER = WORKSPACE / "kfsw-services/include/kfsw/services/parameter.h"


def load_collector():
    path = pathlib.Path(__file__).resolve().parents[1] / "docs/icd-source.py"
    spec = importlib.util.spec_from_file_location("icd_source", path)
    collector = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(collector)
    return collector


def cap():
    found = re.search(r"#define KFSW_PARAM_DESCRIPTION_MAX (\d+)U", HEADER.read_text())
    if found is None:
        sys.exit(f"ERROR: KFSW_PARAM_DESCRIPTION_MAX not found in {HEADER}")
    return int(found.group(1))


def gather(collector, workspace):
    # The ICD collector selects the first preprocessor branch of each conditional.
    sets, arrays = collector.collect_parameters(workspace)
    for (where, _array), entries in arrays.items():
        for entry in entries:
            if "description" in entry:
                yield where, "parameter", collector.literal(entry["name"]), entry["description"]
    for table in sets:
        yield table["file"], "table", table["name"], table["description"]


def measure(collector, raw):
    raw = raw.replace("\\\n", "")
    pieces = collector.STRING_LITERAL.findall(raw)
    if not pieces or collector.STRING_LITERAL.sub("", raw).strip():
        return None
    try:
        with warnings.catch_warnings():
            warnings.simplefilter("error", DeprecationWarning)
            return sum(len(codecs.escape_decode(piece[1:-1].encode("utf-8"))[0])
                       for piece in pieces)
    except (ValueError, DeprecationWarning):
        return None


def main():
    limit = cap()
    collector = load_collector()
    checked = 0
    failures = []
    for where, kind, name, raw in gather(collector, WORKSPACE):
        checked += 1
        length = measure(collector, raw)
        if length is None:
            failures.append(f"{where}: {kind} {name!r} has an unmeasurable description: {raw}")
        elif length > limit:
            failures.append(f"{where}: {kind} {name!r} is {length} bytes, limit {limit}")
    if not checked:
        failures.append("no parameter descriptions found")
    print(f"QUALITY: parameter descriptions ({checked} found, max {limit})")
    if failures:
        print("PARAM DESCRIPTIONS RESULT: FAIL")
        for line in failures:
            print(f"  {line}")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
