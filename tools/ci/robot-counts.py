#!/usr/bin/env python3
"""Count Robot results and optionally require named cases to have passed."""

import argparse
import hashlib
import json
import os
import re
import sys
import xml.etree.ElementTree as ET


def unique_keys(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f"duplicate JSON key: {key}")
        result[key] = value
    return result


def read_json(path):
    with open(path) as source:
        return json.load(source, object_pairs_hook=unique_keys)


def required_cases(path):
    manifest = read_json(path)
    if (not isinstance(manifest, dict)
            or set(manifest) not in ({"version", "required"},
                                     {"version", "required", "evidence_kind"})
            or type(manifest["version"]) is not int or manifest["version"] != 1):
        raise ValueError("required manifest needs version 1 and a required list")
    kind = manifest.get("evidence_kind")
    if "evidence_kind" in manifest and kind not in ("hosted", "physical"):
        raise ValueError("required evidence_kind must be hosted or physical")
    rows = manifest["required"]
    if not isinstance(rows, list) or not rows:
        raise ValueError("required manifest must select at least one case")
    required, identifiers = {}, set()
    for row in rows:
        if not isinstance(row, dict) or set(row) != {"id", "suite", "test", "source"}:
            raise ValueError("each required case needs id, suite, test and source")
        suite = row["suite"]
        if not isinstance(suite, list) or not suite:
            raise ValueError("required suite must be a nonempty path of names")
        if any(not isinstance(value, str) or not value.strip() or value != value.strip()
               for value in [row["id"], row["test"], row["source"], *suite]):
            raise ValueError("required case names and source must be nonempty strings")
        identity = (*suite, row["test"])
        if identity in required or row["id"] in identifiers:
            raise ValueError(f"duplicate required case or id: {row['id']}")
        required[identity] = row["id"]
        identifiers.add(row["id"])
    return required, kind


def sha256(path):
    with open(path, "rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def check_hash(value, name):
    if not isinstance(value, str) or not re.fullmatch(r"[0-9a-f]{64}", value):
        raise ValueError(f"{name} must be a lowercase SHA-256 digest")


def check_context(context):
    fields = {"version", "sources", "profile", "config_sha256", "evidence_kind"}
    if (not isinstance(context, dict)
            or set(context) not in (fields, fields | {"snapshot_sha256"})
            or type(context["version"]) is not int or context["version"] != 1):
        raise ValueError("binding context needs version 1, sources, profile, config_sha256 and evidence_kind")
    sources = context["sources"]
    if (not isinstance(sources, dict) or "k-fsw" not in sources
            or any(not isinstance(name, str) or not name.strip() or name != name.strip()
                   or not isinstance(revision, str) or not re.fullmatch(r"[0-9a-f]{40}", revision)
                   for name, revision in sources.items())):
        raise ValueError("context sources must include k-fsw and map names to full lowercase commit SHAs")
    profile = context["profile"]
    if not isinstance(profile, str) or not profile.strip() or profile != profile.strip():
        raise ValueError("context profile must be a nonempty name")
    if context["evidence_kind"] not in ("hosted", "physical"):
        raise ValueError("context evidence_kind must be hosted or physical")
    check_hash(context["config_sha256"], "context config_sha256")
    if "snapshot_sha256" in context:
        check_hash(context["snapshot_sha256"], "context snapshot_sha256")


def check_binding(arguments, selected_kind, report_sha256):
    expected = read_json(arguments.context)
    check_context(expected)
    record = read_json(arguments.binding)
    if (not isinstance(record, dict)
            or set(record) != {"version", "report_sha256", "image_sha256", "context"}
            or type(record["version"]) is not int or record["version"] != 1):
        raise ValueError("binding needs version 1, report_sha256, image_sha256 and context")
    check_context(record["context"])
    if record["context"] != expected:
        raise ValueError("binding context differs from independently expected source/profile/evidence kind")
    if selected_kind is None or selected_kind != expected["evidence_kind"]:
        raise ValueError("binding evidence_kind differs from the required selection")
    if arguments.board and selected_kind != "physical":
        raise ValueError("hosted binding cannot satisfy --board evidence")
    for name, path, wanted in (
        ("report", arguments.report, record["report_sha256"]),
        ("image", arguments.image, record["image_sha256"]),
        ("config", arguments.config, expected["config_sha256"]),
    ):
        check_hash(wanted, f"{name}_sha256")
        actual = report_sha256 if name == "report" else sha256(path)
        if actual != wanted:
            raise ValueError(f"{name} sha256 differs from binding context")


def observed_cases(root):
    if root.tag != "robot" or not root.findall("suite"):
        raise ValueError("expected a Robot report with a root suite")
    observed = {}

    def visit(suite, parents, ancestors_passed=True):
        name = suite.get("name")
        if not name or not name.strip():
            raise ValueError("report suite has no name")
        path = (*parents, name)
        summary = suite.findall("status")
        if len(summary) != 1 or summary[0].get("status") not in {"PASS", "SKIP", "FAIL"}:
            raise ValueError(f"invalid suite status: {path}")
        if summary[0].get("status") == "FAIL":
            raise ValueError(f"suite failed: {path}")
        passed = ancestors_passed and summary[0].get("status") == "PASS"
        for test in suite.findall("test"):
            name = test.get("name")
            statuses = test.findall("status")
            if (not name or not name.strip() or len(statuses) != 1
                    or statuses[0].get("status") not in {"PASS", "SKIP", "FAIL"}):
                raise ValueError(f"invalid test name or status in {path}")
            if statuses[0].get("status") == "PASS" and not passed:
                raise ValueError(f"passing test in a skipped suite: {path}")
            observed.setdefault((*path, name), []).append(statuses[0].get("status"))
        for child in suite.findall("suite"):
            visit(child, path, passed)

    for suite in root.findall("suite"):
        visit(suite, ())
    if sum(len(values) for values in observed.values()) != len(list(root.iter("test"))):
        raise ValueError("report contains a test outside its suite")
    return observed


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("report", help="Robot output.xml")
    parser.add_argument("--board", action="store_true")
    parser.add_argument("--required", metavar="MANIFEST", help="require each selected case to PASS")
    parser.add_argument("--binding", metavar="SIDECAR", help="verify report/image/context binding")
    parser.add_argument("--image", help="actual executable or firmware image used by the run")
    parser.add_argument("--context", help="independently recorded expected source/profile context")
    parser.add_argument("--config", help="actual resolved image configuration (.config)")
    arguments = parser.parse_args()
    binding_options = (arguments.binding, arguments.image, arguments.context, arguments.config)
    if any(value is not None for value in binding_options):
        if not all(value is not None for value in binding_options) or arguments.required is None:
            parser.error("--binding, --image, --context and --config require each other and --required")
    try:
        if arguments.binding is not None:
            with open(arguments.report, "rb") as source:
                report = source.read()
            root = ET.fromstring(report)
            report_sha256 = hashlib.sha256(report).hexdigest()
        else:
            root = ET.parse(arguments.report).getroot()
        tests = list(root.iter("test"))
        statuses = [test.find("status").get("status") for test in tests]
        required, kind = (required_cases(arguments.required)
                          if arguments.required is not None else (None, None))
        observed = observed_cases(root) if required else None
        if arguments.binding is not None:
            check_binding(arguments, kind, report_sha256)
    except (OSError, ValueError, ET.ParseError, AttributeError) as error:
        print(f"ROBOT COUNTS ERROR: {error}", file=sys.stderr)
        return 1
    skipped = statuses.count("SKIP")
    failed = statuses.count("FAIL")
    executed = len(statuses) - skipped
    print(f"ROBOT COUNTS: executed={executed} skipped={skipped} failed={failed}")
    if required:
        errors = []
        for identity, identifier in required.items():
            states = observed.get(identity, [])
            if states != ["PASS"]:
                state = "missing" if not states else ", ".join(states)
                errors.append(f"{identifier}: {identity}: expected one PASS, observed {state}")
        if errors:
            for error in errors:
                print(f"REQUIRED CASE: FAIL {error}")
            return 1
        print(f"REQUIRED CASES: PASS {len(required)}")
    if arguments.binding is not None:
        print("IMAGE BINDING: PASS (file hashes and expected context match)")
    if arguments.board:
        for test in tests:
            status = test.find("status")
            if status.get("status") == "SKIP":
                reason = " ".join("".join(status.itertext()).split())
                print(f"NOT RUN: {test.get('name')}: {reason}")
    acceptance = os.environ.get("KFSW_ROBOT_ACCEPTANCE", "0") == "1"
    if failed or (not arguments.board and acceptance and (skipped or not executed)):
        return 1
    if arguments.board and not executed:
        print("NOT RUN: every case skipped" if tests else "NOT RUN: no cases selected")
        return 3
    return 0


if __name__ == "__main__":
    sys.exit(main())
