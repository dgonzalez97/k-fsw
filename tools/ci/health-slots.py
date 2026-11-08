#!/usr/bin/env python3
"""Every health slot a composition names is registered, and the reverse.

A slot name is written twice: once in the composition's slot header and once in
the kfsw_health_register() call of the component. Nothing in the language ties
them, and a mismatch costs nothing at build time and everything in flight: the
slot reads absent for the life of the image, which is indistinguishable from a
component that died before it ever reported.
"""

import pathlib
import re

SLOT_LIST = re.compile(r"#\s*define\s+KFSW_HEALTH_SLOTS\(\w+\)(.*?)(?=\n\s*#|\Z)", re.DOTALL)
SLOT_ITEM = re.compile(r"\bSLOT\s*\(\s*(\w+)\s*,\s*(\w+)\s*\)")
REGISTER = re.compile(r'kfsw_health_register\s*\(\s*"([^"]+)"')


def slots(header):
    match = SLOT_LIST.search(header.read_text(errors="replace"))
    if not match:
        return {}
    return {name: index for name, index in SLOT_ITEM.findall(match.group(1))}


def registrations(sources):
    found = {}
    for path in sources:
        for name in REGISTER.findall(path.read_text(errors="replace")):
            found.setdefault(name, []).append(path)
    return found


def main():
    repository = pathlib.Path(__file__).resolve().parents[2]
    header = repository / "app" / "include" / "kfsw" / "app" / "health_slots.h"
    if not header.is_file():
        print(f"ERROR: no slot header at {header}")
        return 1

    listed = slots(header)
    if not listed:
        print(f"ERROR: {header} names no slots")
        return 1

    sources = sorted((repository / "app" / "src").rglob("*.c"))
    # The shell registers whatever name an operator types, so it is not a slot.
    sources = [path for path in sources if path.name != "shell_health.c"]
    registered = registrations(sources)

    failures = []
    for name in sorted(listed):
        if name not in registered:
            failures.append(f"slot {name!r} is listed but nothing registers it")
    for name, paths in sorted(registered.items()):
        if name not in listed:
            where = ", ".join(str(p.relative_to(repository)) for p in paths)
            failures.append(f"{where} registers {name!r}, which no slot names")

    indices = {}
    for name, index in listed.items():
        indices.setdefault(index, []).append(name)
    for index, names in sorted(indices.items()):
        if len(names) > 1:
            failures.append(f"index {index} is given to {' and '.join(sorted(names))}")

    if failures:
        print("HEALTH SLOTS RESULT: FAIL")
        for line in failures:
            print(f"  {line}")
        return 1

    print(f"HEALTH SLOTS: {len(listed)} named and registered "
          f"({', '.join(sorted(listed))})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
