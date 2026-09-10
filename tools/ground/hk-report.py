#!/usr/bin/env python3
"""Turn a housekeeping report definition into the two things that must agree.

Housekeeping keeps parameter names off the wire: a sample is values back to
back in the order the report was defined, and nothing in the frame says what
they are. That is the right trade for a radio and the wrong one for a person,
so the definition has to live somewhere on the ground.

This is that somewhere. `define` prints the command that tells a node what to
collect, `xtce` writes the mission database Yamcs decodes it with, and `check`
proves the file still matches what the node actually registers.
"""

import argparse
import re
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

import yaml

XTCE_NS = "http://www.omg.org/spec/XTCE/20180204"
XSI_NS = "http://www.w3.org/2001/XMLSchema-instance"

# The bridge's envelope, ahead of the frame the node sent. Yamcs reads an
# 8-byte time and a 4-byte count at fixed offsets, and the housekeeping header
# carries 4 and 2, so the bridge restates them in the shape Yamcs asks for.
# Without it every sample of a pull lands at the same reception instant and the
# history the ring kept collapses into one moment.
ENVELOPE_BYTES = 12

# Widths must match entry_width() in kfsw-services/src/hk/hk.c. A report is
# packed from the declared type, not from the value, so every sample of a
# report has the same layout.
TYPES = {
    "u8": (8, False),
    "u16": (16, False),
    "u32": (32, False),
    "u64": (64, False),
    "i8": (8, True),
    "i16": (16, True),
    "i32": (32, True),
    "i64": (64, True),
    "x8": (8, False),
    "x16": (16, False),
    "x32": (32, False),
    "x64": (64, False),
}
FLOAT_TYPES = {"float": 32, "double": 64}


def load(path):
    report = yaml.safe_load(Path(path).read_text())
    for key in ("report", "node", "name", "entries"):
        if key not in report:
            raise SystemExit(f"{path}: missing '{key}'")
    for entry in report["entries"]:
        kind = entry.get("type")
        if kind not in TYPES and kind not in FLOAT_TYPES:
            raise SystemExit(f"{path}: {entry.get('parameter')} has unsupported type {kind!r}")
    return report


def display_name(entry):
    return entry.get("as", entry["parameter"])


def payload_bytes(report):
    total = 0
    for entry in report["entries"]:
        bits = TYPES[entry["type"]][0] if entry["type"] in TYPES else FLOAT_TYPES[entry["type"]]
        total += bits // 8
    return total


def cmd_define(report, _args):
    entries = " ".join(f"{e['table']}:0x{e['offset']:02x}" for e in report["entries"])
    print(f"hk define {report['report']} {entries}")
    return 0


def cmd_check(report, args):
    """Compare the file against a `param list` capture from a node.

    The file names a table and an offset; the node knows what lives there. If
    those disagree the frame still decodes, silently and wrongly, which is the
    failure this exists to catch.
    """
    # `table_name  0xNN  name  type  mode  value`, columns padded with spaces.
    row = re.compile(r"^(\S+)\s+0x([0-9a-f]{2})\s+(\S+)\s+(\S+)\s")
    seen = {}
    for line in Path(args.capture).read_text().splitlines():
        match = row.match(line.replace("\r", ""))
        if match:
            seen[(match.group(1), int(match.group(2), 16))] = (match.group(3), match.group(4))

    if not seen:
        raise SystemExit(f"{args.capture}: no parameter rows found; is this `param list` output?")

    problems = []
    for entry in report["entries"]:
        name = entry["parameter"]
        matches = [(k, v) for k, v in seen.items() if v[0] == name]
        if not matches:
            problems.append(f"{name}: the node does not register it")
            continue
        (_, offset), (_, kind) = matches[0]
        if offset != entry["offset"]:
            problems.append(f"{name}: file says offset 0x{entry['offset']:02x}, node says 0x{offset:02x}")
        if kind != entry["type"]:
            problems.append(f"{name}: file says {entry['type']}, node says {kind}")

    for problem in problems:
        print(f"MISMATCH {problem}", file=sys.stderr)
    if problems:
        return 1
    print(f"{len(report['entries'])} entries agree with the node")
    return 0


def sub(parent, tag, **attrs):
    return ET.SubElement(parent, f"{{{XTCE_NS}}}{tag}", {k: str(v) for k, v in attrs.items()})


def integer_type(types, name, bits, signed, unit=None, calibration=None, valid=None):
    if calibration:
        node = sub(types, "FloatParameterType", name=name, sizeInBits=32)
    else:
        node = sub(types, "IntegerParameterType", name=name, signed="true" if signed else "false")
    units = sub(node, "UnitSet")
    if unit:
        sub(units, "Unit").text = unit
    encoding = sub(
        node,
        "IntegerDataEncoding",
        sizeInBits=bits,
        encoding="twosComplement" if signed else "unsigned",
    )
    if calibration:
        calibrator = sub(sub(encoding, "DefaultCalibrator"), "PolynomialCalibrator")
        sub(calibrator, "Term", coefficient=calibration.get("intercept", 0.0), exponent=0)
        sub(calibrator, "Term", coefficient=calibration["slope"], exponent=1)
    if valid:
        # A parameter that reports a reserved value when it has nothing to say
        # would otherwise drag every plot to that reserved value. Marking the
        # range keeps the absence visible without letting it set the scale.
        sub(node, "ValidRange", minInclusive=valid["min"], maxInclusive=valid["max"])
    return node


def float_type(types, name, bits, unit=None):
    node = sub(types, "FloatParameterType", name=name, sizeInBits=bits)
    units = sub(node, "UnitSet")
    if unit:
        sub(units, "Unit").text = unit
    sub(node, "FloatDataEncoding", sizeInBits=bits, encoding="IEEE754_1985")
    return node


def build_xtce(reports):
    root = ET.Element(f"{{{XTCE_NS}}}SpaceSystem", {"name": "kfsw"})
    sub(root, "LongDescription").text = (
        "K-FSW housekeeping. Generated by tools/ground/hk-report.py from the report "
        "definitions under ground-station/reports; edit those, not this file."
    )
    telemetry = sub(root, "TelemetryMetaData")
    types = sub(telemetry, "ParameterTypeSet")
    parameters = sub(telemetry, "ParameterSet")
    containers = sub(telemetry, "ContainerSet")

    # The envelope and the frame header, shared by every report.
    integer_type(types, "gs_time_type", 64, False, unit="ms")
    integer_type(types, "gs_sequence_type", 32, False)
    integer_type(types, "hk_u8_type", 8, False)
    integer_type(types, "hk_u16_type", 16, False)
    integer_type(types, "hk_seconds_type", 32, False, unit="s")

    flags = sub(types, "BooleanParameterType", name="hk_flags_type",
                zeroStringValue="complete", oneStringValue="incomplete")
    sub(flags, "UnitSet")
    sub(flags, "IntegerDataEncoding", sizeInBits=8, encoding="unsigned")

    header = [
        ("gs_time_ms", "gs_time_type",
         "When the bridge says the sample was taken. The frame's own clock when it is set, "
         "the host clock when it is not."),
        ("gs_sequence", "gs_sequence_type", "The frame's sequence, restated for Yamcs."),
        ("hk_version", "hk_u8_type", "Housekeeping protocol version."),
        ("hk_report", "hk_u8_type", "Which report this sample belongs to."),
        ("hk_sequence", "hk_u16_type",
         "Per-report counter. A gap here is a lost sample, not a lost value."),
        ("hk_seconds", "hk_seconds_type",
         "UTC seconds when collection started, or zero if the node's clock was never set. "
         "A remote value cannot be simultaneous with anything, so this is a start, not a snapshot."),
        ("hk_entry_count", "hk_u8_type", "Values in this sample."),
        ("hk_flags", "hk_flags_type",
         "Incomplete means at least one value could not be read and was zero-filled. "
         "The frame does not say which one."),
    ]
    for name, type_ref, description in header:
        parameter = sub(parameters, "Parameter", name=name, parameterTypeRef=type_ref)
        sub(parameter, "LongDescription").text = description

    frame = sub(containers, "SequenceContainer", name="hk_frame", abstract="true")
    sub(frame, "LongDescription").text = (
        f"The {ENVELOPE_BYTES}-byte bridge envelope and the 10-byte housekeeping header."
    )
    entry_list = sub(frame, "EntryList")
    for name, _, _ in header:
        sub(entry_list, "ParameterRefEntry", parameterRef=name)

    for report in reports:
        add_report(types, parameters, containers, report)

    return root


def add_report(types, parameters, containers, report):
    prefix = report["name"]
    for entry in report["entries"]:
        name = f"{prefix}_{display_name(entry)}"
        type_name = f"{name}_type"
        if entry["type"] in FLOAT_TYPES:
            float_type(types, type_name, FLOAT_TYPES[entry["type"]], entry.get("unit"))
        else:
            bits, signed = TYPES[entry["type"]]
            integer_type(types, type_name, bits, signed, entry.get("unit"),
                         entry.get("calibration"), entry.get("valid_range"))
        parameter = sub(parameters, "Parameter", name=name, parameterTypeRef=type_name)
        description = entry.get("description")
        source = f"Table {entry['table']} offset 0x{entry['offset']:02x}, {entry['parameter']}."
        sub(parameter, "LongDescription").text = (
            f"{description.strip()} {source}" if description else source
        )

    container = sub(containers, "SequenceContainer", name=prefix)
    description = report.get("description")
    if description:
        sub(container, "LongDescription").text = description.strip()
    entry_list = sub(container, "EntryList")
    for entry in report["entries"]:
        sub(entry_list, "ParameterRefEntry", parameterRef=f"{prefix}_{display_name(entry)}")
    base = sub(container, "BaseContainer", containerRef="hk_frame")
    criteria = sub(base, "RestrictionCriteria")
    # A bare Comparison rather than a ComparisonList: the schema wants at least
    # two entries in a list, and the report id is the only discriminator there is.
    sub(criteria, "Comparison", parameterRef="hk_report", value=report["report"])


def cmd_xtce(report, args):
    ET.register_namespace("", XTCE_NS)
    root = build_xtce([report])
    ET.indent(root, space="\t")
    xml = ET.tostring(root, encoding="unicode")
    # The schema hint carries a prefix ElementTree will not emit alongside a
    # default namespace, so it is stitched on rather than fought with.
    xml = xml.replace(
        "<SpaceSystem ",
        f'<SpaceSystem xmlns:xsi="{XSI_NS}" '
        f'xsi:schemaLocation="{XTCE_NS} '
        'https://www.omg.org/spec/XTCE/20180204/SpaceSystem.xsd" ',
        1,
    )
    text = f'<?xml version="1.0" encoding="UTF-8"?>\n{xml}\n'

    if args.output == "-":
        sys.stdout.write(text)
    else:
        Path(args.output).write_text(text)
        print(f"{args.output}: {payload_bytes(report)} bytes of values in report "
              f"{report['report']}, {ENVELOPE_BYTES + 10 + payload_bytes(report)} on the wire")
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("definition", help="report definition under ground-station/reports")
    commands = parser.add_subparsers(dest="command", required=True)

    commands.add_parser("define", help="print the hk define command for a node")

    xtce = commands.add_parser("xtce", help="write the Yamcs mission database")
    xtce.add_argument("-o", "--output", default="-", help="destination, or - for stdout")

    check = commands.add_parser("check", help="compare against a node's `param list` output")
    check.add_argument("capture", help="file holding `param list` output")

    args = parser.parse_args()
    report = load(args.definition)
    return {"define": cmd_define, "xtce": cmd_xtce, "check": cmd_check}[args.command](report, args)


if __name__ == "__main__":
    sys.exit(main())
