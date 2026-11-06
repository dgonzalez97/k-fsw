#!/usr/bin/env python3
"""Every shell command the sources define, read from the SHELL_* macros.

Reading the sources rather than a running image is deliberate: an image only
carries what its own configuration enabled, so it cannot describe a command
that exists but is switched off in that build.
"""

import argparse
import pathlib
import re
import sys

ROOTS = (
    "k-fsw/app",
    "kfsw-services/src",
    "kfsw-comms/src",
    "kfsw-platform/src",
    "kfsw-modules",
)

def strip_comments(text):
    """Remove comments without touching string literals."""
    out = []
    i = 0
    while i < len(text):
        ch = text[i]
        if ch == '"':
            j = i + 1
            while j < len(text):
                if text[j] == "\\":
                    j += 2
                    continue
                if text[j] == '"':
                    break
                j += 1
            out.append(text[i : j + 1])
            i = j + 1
        elif text.startswith("/*", i):
            end = text.find("*/", i + 2)
            i = len(text) if end < 0 else end + 2
            out.append(" ")
        elif text.startswith("//", i):
            end = text.find("\n", i)
            i = len(text) if end < 0 else end
            out.append(" ")
        else:
            out.append(ch)
            i += 1
    return "".join(out)


def macro_calls(text, macros, base=0):
    """Yield (macro, argument text, absolute offset) in source order.

    Longest name first, so SHELL_CMD_ARG is never read as SHELL_CMD.
    """
    pattern = "|".join(sorted(macros, key=len, reverse=True))
    for match in re.finditer(rf"\b({pattern})\s*\(", text):
        depth, i, in_string = 1, match.end(), False
        while i < len(text) and depth:
            ch = text[i]
            if in_string:
                if ch == "\\":
                    i += 2
                    continue
                if ch == '"':
                    in_string = False
            elif ch == '"':
                in_string = True
            elif ch == "(":
                depth += 1
            elif ch == ")":
                depth -= 1
            i += 1
        yield match.group(1), text[match.end() : i - 1], base + match.start()


def guard_at(text, offset):
    """The nearest #if still open at offset, as written."""
    stack = []
    for match in re.finditer(r"^[ \t]*#[ \t]*(if|ifdef|ifndef|elif|else|endif)\b[ \t]*(.*)$",
                             text[:offset], re.MULTILINE):
        kind, rest = match.group(1), match.group(2).strip()
        if kind in ("if", "ifdef", "ifndef"):
            prefix = {"if": "", "ifdef": "defined ", "ifndef": "!defined "}[kind]
            stack.append(prefix + rest)
        elif kind in ("elif", "else"):
            if stack:
                stack[-1] = ("elif " + rest) if kind == "elif" else "else"
        elif stack:
            stack.pop()
    for condition in reversed(stack):
        cleaned = re.sub(r"\s+", " ", condition).strip()
        if "CONFIG_" in cleaned:
            return cleaned
    return ""


def split_args(text):
    """Split on top-level commas, respecting strings, parens and braces."""
    parts, depth, in_string, current = [], 0, False, []
    i = 0
    while i < len(text):
        ch = text[i]
        if in_string:
            current.append(ch)
            if ch == "\\":
                current.append(text[i + 1])
                i += 2
                continue
            if ch == '"':
                in_string = False
        elif ch == '"':
            in_string = True
            current.append(ch)
        elif ch in "([{":
            depth += 1
            current.append(ch)
        elif ch in ")]}":
            depth -= 1
            current.append(ch)
        elif ch == "," and depth == 0:
            parts.append("".join(current).strip())
            current = []
        else:
            current.append(ch)
        i += 1
    if current:
        parts.append("".join(current).strip())
    return parts


def literal(text):
    """Join adjacent C string literals into one value."""
    pieces = re.findall(r'"((?:[^"\\]|\\.)*)"', text or "")
    joined = "".join(pieces)
    return joined.replace('\\"', '"').replace("\\n", " ").replace("\\t", " ").strip()


def set_name(text):
    text = (text or "").strip()
    if text in ("NULL", "", "0"):
        return None
    return text.lstrip("&").strip()


def arity(minimum, optional):
    try:
        low = int(re.sub(r"[^0-9]", "", minimum or "")) - 1
        extra = int(re.sub(r"[^0-9]", "", optional or ""))
    except ValueError:
        return ""
    if extra >= 255:
        return f"{low}+" if low else "any"
    high = low + extra
    if low == high:
        return str(low)
    return f"{low}-{high}"


def collect(workspace):
    sets, roots, attach = {}, [], {}
    files = []
    for root in ROOTS:
        base = workspace / root
        if base.is_dir():
            files.extend(sorted(base.rglob("*.c")))

    for path in files:
        try:
            text = strip_comments(path.read_text(errors="replace"))
        except OSError:
            continue
        if "SHELL_" not in text:
            continue
        where = str(path.relative_to(workspace))

        for _macro, body, offset in macro_calls(
            text, ("SHELL_STATIC_SUBCMD_SET_CREATE", "SHELL_SUBCMD_SET_CREATE")
        ):
            args = split_args(body)
            if not args:
                continue
            name = args[0].strip()
            # Preprocessor lines live inside the argument list, so scan the
            # whole body for entries rather than trusting comma splitting.
            start = text.index(body, offset) if body else offset
            entries = []
            for kind, inner, at in macro_calls(
                body, ("SHELL_CMD_ARG", "SHELL_CMD"), base=start
            ):
                label = "arg" if kind == "SHELL_CMD_ARG" else "plain"
                entries.append((label, split_args(inner), where, guard_at(text, at)))
            for raw in args[1:]:
                if raw.strip().startswith("("):
                    attach.setdefault(raw.strip("() "), name)
            sets.setdefault(name, []).extend(entries)

        for kind, body, at in macro_calls(
            text, ("SHELL_CMD_ARG_REGISTER", "SHELL_CMD_REGISTER", "SHELL_SUBCMD_ADD")
        ):
            args = split_args(body)
            guard = guard_at(text, at)
            if kind == "SHELL_CMD_REGISTER" and len(args) >= 3:
                roots.append(
                    (args[0].strip(), set_name(args[1]), literal(args[2]), "", where, guard)
                )
            elif kind == "SHELL_CMD_ARG_REGISTER" and len(args) >= 6:
                roots.append(
                    (
                        args[0].strip(),
                        set_name(args[1]),
                        literal(args[2]),
                        arity(args[4], args[5]),
                        where,
                        guard,
                    )
                )
            elif kind == "SHELL_SUBCMD_ADD" and len(args) >= 7:
                key = args[0].strip("() ")
                sets.setdefault(f"@{key}", []).append(
                    (
                        "add",
                        [args[1].strip(), args[2], args[3], args[4], args[5], args[6]],
                        where,
                        guard,
                    )
                )
    return sets, roots, attach


def entry_fields(kind, args, where, guard):
    name = args[0].strip()
    child = set_name(args[1]) if len(args) > 1 else None
    help_text = literal(args[2]) if len(args) > 2 else ""
    if kind == "plain":
        return name, child, help_text, "", where, guard
    if len(args) >= 6:
        return name, child, help_text, arity(args[4], args[5]), where, guard
    return name, child, help_text, "", where, guard


def walk(sets, attach, set_ref, path, rows, seen, inherited=""):
    entries = list(sets.get(set_ref, []))
    for key, holder in attach.items():
        if holder == set_ref:
            entries.extend(sets.get(f"@{key}", []))
    for kind, args, where, guard in entries:
        name, child, help_text, args_text, origin, cond = entry_fields(
            kind, args, where, guard
        )
        full = f"{path} {name}".strip()
        if full in seen:
            continue
        seen.add(full)
        rows.append((full, args_text, help_text, origin, cond or inherited or ""))
        if child:
            walk(sets, attach, child, full, rows, seen, cond or inherited)


def render(rows):
    out = ["## Shell commands", ""]
    out.append(
        "Read from the `SHELL_*` macros in the sources, so a command a particular "
        "image switches off is still listed."
    )
    out.append("")
    out.append("| Command | Args | What it does | Built when | Defined in |")
    out.append("| --- | --- | --- | --- | --- |")
    for full, args_text, help_text, origin, cond in rows:
        gate = f"`{cond}`" if cond else "always"
        out.append(
            f"| `{full}` | {args_text or '-'} | {help_text or '-'} | {gate} | `{origin}` |"
        )
    out.append("")
    return "\n".join(out)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--workspace", default=None)
    parser.add_argument(
        "--write", action="store_true", help="write docs/icd.md instead of stdout"
    )
    parser.add_argument(
        "--check",
        action="store_true",
        help="compare with the committed page and fail on a difference",
    )
    arguments = parser.parse_args()

    repository = pathlib.Path(__file__).resolve().parents[2]
    workspace = pathlib.Path(arguments.workspace) if arguments.workspace else repository.parent

    sets, roots, attach = collect(workspace)
    rows, seen = [], set()
    for name, child, help_text, args_text, where, guard in sorted(roots):
        rows.append((name, args_text, help_text, where, guard))
        seen.add(name)
        if child:
            walk(sets, attach, child, name, rows, seen, guard)

    param_sets, param_arrays = collect_parameters(workspace)
    parameters_page, parameter_count = render_parameters(param_sets, param_arrays)

    registry = collect_commands(workspace)
    page = "\n".join(
        [
            "# Interface control document {#icd}",
            "",
            "Every shell command, remote command and parameter the sources define."
            " Read from the sources and not from a running image, so an interface"
            " a particular configuration switches off is still described. The"
            " `Built when` column names the condition that has to hold for it to"
            " be in an image.",
            "",
            "Mode letters: `r` read only, `rw` writable, `b` kept across a reset,"
            " `c` operator configuration, `i` system identity, `d` diagnostic,"
            " `o` owner persists it, `l` applied live, `n` local writes only.",
            "",
            render(rows),
            render_commands(registry),
            parameters_page,
        ]
    )

    counts = (
        f"{len(rows)} shell commands, {len(registry)} remote commands, "
        f"{len(param_sets)} tables, {parameter_count} parameters"
    )
    target = repository / "docs" / "icd.md"

    if arguments.check:
        committed = target.read_text() if target.is_file() else ""
        if committed.rstrip("\n") != page.rstrip("\n"):
            print(f"ERROR: {target} is stale; regenerate it with --write")
            return 1
        print(f"ICD: up to date ({counts})")
        return 0

    if arguments.write:
        target.write_text(page.rstrip("\n") + "\n")
        print(f"ICD: wrote {target} ({counts})")
        return 0

    print(page)
    print(f"<!-- {counts} -->", file=sys.stderr)
    return 0




# --------------------------------------------------------------------------
# Parameters: the definition sets and their entries, also read from source.
# --------------------------------------------------------------------------

HEADER_ROOTS = ROOTS + ("k-fsw/app/src/parameters", "kfsw-services/include")

TYPE_NAMES = {
    "KFSW_PARAM_U8": "u8", "KFSW_PARAM_U16": "u16", "KFSW_PARAM_U32": "u32",
    "KFSW_PARAM_U64": "u64", "KFSW_PARAM_I8": "i8", "KFSW_PARAM_I16": "i16",
    "KFSW_PARAM_I32": "i32", "KFSW_PARAM_I64": "i64",
    "KFSW_PARAM_FLOAT": "f32", "KFSW_PARAM_DOUBLE": "f64",
    "KFSW_PARAM_STRING": "str", "KFSW_PARAM_DATA": "data",
}


def balanced(text, start, opener="{", closer="}"):
    """Return (body, end) for the braced block beginning at or after start."""
    begin = text.index(opener, start)
    depth, i, in_string = 1, begin + 1, False
    while i < len(text) and depth:
        ch = text[i]
        if in_string:
            if ch == "\\":
                i += 2
                continue
            if ch == '"':
                in_string = False
        elif ch == '"':
            in_string = True
        elif ch == opener:
            depth += 1
        elif ch == closer:
            depth -= 1
        i += 1
    return text[begin + 1 : i - 1], i


def designators(body):
    """The top-level `.key = value` pairs of a struct initializer."""
    fields = {}
    for part in split_args(body):
        match = re.match(r"\.\s*(\w+)\s*=\s*(.*)$", part, re.DOTALL)
        if match:
            fields[match.group(1)] = match.group(2).strip()
    return fields


def collect_defines(workspace):
    values = {}
    for root in HEADER_ROOTS:
        base = workspace / root
        if not base.is_dir():
            continue
        for path in base.rglob("*.h"):
            try:
                text = path.read_text(errors="replace")
            except OSError:
                continue
            for name, raw in re.findall(
                r"^[ \t]*#[ \t]*define[ \t]+(KFSW_\w*TABLE\w*)[ \t]+(.+)$",
                text,
                re.MULTILINE,
            ):
                raw = raw.split("/*")[0].strip()
                if raw.startswith('"'):
                    values[name] = literal(raw)
                else:
                    digits = re.sub(r"[^0-9]", "", raw)
                    values[name] = digits or raw
    return values


def resolve(token, defines):
    token = (token or "").strip()
    if token.startswith('"'):
        return literal(token)
    return defines.get(token, token)


def collect_parameters(workspace):
    defines = collect_defines(workspace)
    arrays, sets = {}, []
    for root in ROOTS:
        base = workspace / root
        if not base.is_dir():
            continue
        for path in sorted(base.rglob("*.c")):
            try:
                text = strip_comments(path.read_text(errors="replace"))
            except OSError:
                continue
            if "kfsw_param_definition" not in text:
                continue
            where = str(path.relative_to(workspace))

            for match in re.finditer(
                r"struct\s+kfsw_param_definition\s+(\w+)\s*\[\s*\]\s*=", text
            ):
                body, _ = balanced(text, match.end())
                entries = []
                position = 0
                while True:
                    try:
                        inner, position = balanced(body, position)
                    except ValueError:
                        break
                    fields = designators(inner)
                    if "name" in fields:
                        entries.append(fields)
                arrays[match.group(1)] = entries

            for match in re.finditer(
                r"struct\s+kfsw_param_definition_set\s+(\w+)\s*=", text
            ):
                body, _ = balanced(text, match.end())
                fields = designators(body)
                sets.append(
                    {
                        "table": resolve(fields.get("table"), defines),
                        "name": resolve(fields.get("name"), defines),
                        "holds": literal(fields.get("description", "")),
                        "array": (fields.get("definitions") or "").strip(),
                        "file": where,
                        "guard": guard_at(text, match.start()),
                    }
                )
    return sets, arrays


FLAG_LETTERS = {
    "READ_ONLY": "r",
    "PERSISTENT": "b",
    "CONFIGURATION": "c",
    "SYSTEM_INFO": "i",
    "DEBUG": "d",
    "OWNER_STORE": "o",
    "LIVE": "l",
    "LOCAL_ONLY": "n",
}


def flags_text(raw):
    """The flags as letters, in the order FLAG_LETTERS declares them."""
    found = set(re.findall(r"KFSW_PARAM_FLAG_(\w+)", raw or ""))
    if not found:
        return "rw"
    letters = [letter for name, letter in FLAG_LETTERS.items() if name in found]
    unknown = sorted(found - set(FLAG_LETTERS))
    text = "".join(letters)
    if "r" not in text:
        text = "rw" + text
    return text + ("+" + ",".join(unknown) if unknown else "")


def render_parameters(sets, arrays):
    out = ["## Parameter tables", ""]
    out.append("| Table | Name | Params | Holds | Built when | Defined in |")
    out.append("| --- | --- | --- | --- | --- | --- |")
    for item in sorted(sets, key=lambda s: (str(s["table"]).zfill(3), s["name"])):
        count = len(arrays.get(item["array"], []))
        gate = f"`{item['guard']}`" if item["guard"] else "always"
        out.append(
            f"| {item['table']} | `{item['name']}` | {count} | {item['holds'] or '-'} "
            f"| {gate} | `{item['file']}` |"
        )
    out.append("")
    out.append("## Parameters")
    out.append("")
    out.append("| Table | Offset | Name | Type | Mode | Unit | Holds |")
    out.append("| --- | --- | --- | --- | --- | --- | --- |")
    total = 0
    for item in sorted(sets, key=lambda s: (str(s["table"]).zfill(3), s["name"])):
        for entry in arrays.get(item["array"], []):
            total += 1
            offset = entry.get("offset", "").rstrip("U")
            kind = TYPE_NAMES.get(entry.get("type", "").strip(), entry.get("type", ""))
            out.append(
                f"| `{item['name']}` | {offset} | `{literal(entry.get('name',''))}` "
                f"| {kind} | {flags_text(entry.get('flags'))} "
                f"| {literal(entry.get('unit','')) or '-'} "
                f"| {literal(entry.get('description','')) or '-'} |"
            )
    out.append("")
    return "\n".join(out), total




# --------------------------------------------------------------------------
# The command registry: what another node can be asked to run over CSP.
# --------------------------------------------------------------------------


def collect_commands(workspace):
    ids = {}
    for root in sorted(set(HEADER_ROOTS + ROOTS)):
        base = workspace / root
        if not base.is_dir():
            continue
        # The ids are defined beside the table as often as in a header.
        candidates = list(base.rglob("*.h")) + list(base.rglob("*.c"))
        for path in candidates:
            if "/tests/" in str(path):
                continue
            try:
                text = path.read_text(errors="replace")
            except OSError:
                continue
            for name, raw in re.findall(
                r"^[ \t]*#[ \t]*define[ \t]+(KFSW_COMMAND_ID_\w+)[ \t]+(.+)$",
                text,
                re.MULTILINE,
            ):
                digits = re.sub(r"[^0-9]", "", raw.split("/*")[0])
                ids[name] = digits or raw.strip()

    rows = []
    for root in ROOTS:
        base = workspace / root
        if not base.is_dir():
            continue
        for path in sorted(base.rglob("*.c")):
            # A fixture is not an interface.
            if "/tests/" in str(path) or path.name == "main.c":
                continue
            try:
                text = strip_comments(path.read_text(errors="replace"))
            except OSError:
                continue
            if "kfsw_command_definition" not in text:
                continue
            where = str(path.relative_to(workspace))
            for match in re.finditer(
                r"struct\s+kfsw_command_definition\s+(\w+)\s*\[\s*\]\s*=", text
            ):
                body, _ = balanced(text, match.end())
                offset_in_file = text.index(body, match.end())
                position = 0
                while True:
                    try:
                        inner, position = balanced(body, position)
                    except ValueError:
                        break
                    fields = designators(inner)
                    if "name" not in fields:
                        continue
                    identifier = (fields.get("id") or "").strip()
                    rows.append(
                        {
                            "id": ids.get(identifier, identifier),
                            "name": literal(fields.get("name", "")),
                            "help": literal(fields.get("help", "")),
                            "args": (fields.get("arg_count") or "0").rstrip("U"),
                            "mutating": "MUTATING" in (fields.get("flags") or ""),
                            "file": where,
                            "guard": guard_at(text, offset_in_file + position),
                        }
                    )
    return rows


def render_commands(rows):
    out = ["## Command registry", ""]
    out.append(
        "What a ground station or another node can ask this one to run over CSP. "
        "`Claim` marks a command that changes something and may only be sent by a "
        "caller that already holds the right to."
    )
    out.append("")
    out.append("| ID | Command | Args | Claim | What it does | Built when | Defined in |")
    out.append("| --- | --- | --- | --- | --- | --- | --- |")

    def sort_key(row):
        try:
            return (0, int(row["id"]), row["name"])
        except (TypeError, ValueError):
            return (1, 0, row["name"])

    for row in sorted(rows, key=sort_key):
        gate = f"`{row['guard']}`" if row["guard"] else "always"
        out.append(
            f"| {row['id']} | `{row['name']}` | {row['args']} "
            f"| {'yes' if row['mutating'] else '-'} | {row['help'] or '-'} "
            f"| {gate} | `{row['file']}` |"
        )
    out.append("")
    return "\n".join(out)


if __name__ == "__main__":
    raise SystemExit(main())
