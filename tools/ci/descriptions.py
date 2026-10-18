#!/usr/bin/env python3
"""Refuse a parameter or table description longer than the wire allows.

The cap lives in kfsw-services/include/kfsw/services/parameter.h; this reads it
from there so the two cannot drift.
"""

import pathlib
import re
import sys

WORKSPACE = pathlib.Path(__file__).resolve().parents[3]
HEADER = WORKSPACE / "kfsw-services/include/kfsw/services/parameter.h"
ROOTS = (
    "k-fsw/app/src",
    "kfsw-services/src",
    "kfsw-comms/src",
    "kfsw-modules",
    "kfsw-platform/src",
)
DESCRIPTION = re.compile(r'\.description\s*=\s*"((?:[^"\\]|\\.)*)"')


def cap():
    found = re.search(r"#define KFSW_PARAM_DESCRIPTION_MAX (\d+)U", HEADER.read_text())
    if found is None:
        sys.exit(f"ERROR: KFSW_PARAM_DESCRIPTION_MAX not found in {HEADER}")
    return int(found.group(1))


def main():
    limit = cap()
    checked = 0
    over = []
    for root in ROOTS:
        for source in sorted((WORKSPACE / root).rglob("*.c")):
            if "third_party" in source.parts:
                continue
            for number, line in enumerate(source.read_text().splitlines(), 1):
                for match in DESCRIPTION.finditer(line):
                    checked += 1
                    length = len(match.group(1))
                    if length > limit:
                        over.append((source, number, length, match.group(1)))
    print(f"QUALITY: parameter descriptions ({checked} found, max {limit})")
    for source, number, length, text in over:
        print(f"{source.relative_to(WORKSPACE)}:{number}: {length} characters: {text}")
    if over:
        print(f"ERROR: {len(over)} description(s) over {limit} characters")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
