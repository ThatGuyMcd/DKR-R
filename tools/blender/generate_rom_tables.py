"""Generate ``dkr_track_editor/data/rom_tables.json.gz`` from the DKR decomp.

The addon can extract its asset tree straight from the author's ROM
(:mod:`dkr_track_editor.rom_extract`), so nobody needs a decomp checkout just to
see a coin as a coin. What the ROM does not hold is the *names*: a record is
only bytes, and ``ASSET_OBJECT_PALMTREETOP`` / ``palm_tree_top_0.png`` come from
the decomp's ``tools/dkr_assets_tool_extract.json``, which identifies every
record by its SHA1. The decoding rules for object maps, level headers and
object headers also lean on the decomp's C headers - enum symbols and the
``LevelObjectEntry_*`` struct layouts.

This script distils exactly that into one compact table the addon ships:

* the supported input ROMs and where their asset block sits
* the 50 asset sections and the per-record names, keyed by SHA1
* every enum in ``include/enums.h`` and ``include/object_behaviors.h``, as the
  asset tool evaluates them (first symbol wins for a repeated value)
* the ``LevelObjectEntry_*`` structs, laid out the way the asset tool lays them
  out - packed, with no alignment padding
* the DKRJP character table level names use for Japanese

Nothing here is ROM content. Run it again whenever the decomp moves.

Usage::

    python tools/blender/generate_rom_tables.py
    python tools/blender/generate_rom_tables.py --decomp path/to/dkr-decomp --check
"""

from __future__ import annotations

import argparse
import ast
import gzip
import json
import operator
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
DEFAULT_DECOMP = os.path.join(REPO_ROOT, "extern", "dkr-decomp")
DEFAULT_OUTPUT = os.path.join(HERE, "dkr_track_editor", "data", "rom_tables.json.gz")

SCHEMA_VERSION = 1

#: ``misc/constants.hpp`` - written into ``assets.meta.json`` so the tree the
#: addon extracts identifies itself exactly as the asset tool's does.
DKRAT_VERSION = {"release": 0, "major": 5, "minor": 2}

#: C types the asset tool knows, as ``(byte size, signed)``.
C_TYPES = {
    "u8": (1, False), "s8": (1, True),
    "u16": (2, False), "s16": (2, True),
    "u32": (4, False), "s32": (4, True),
    "u64": (8, False), "s64": (8, True),
    "f32": (4, True), "f64": (8, True),
}

# ---------------------------------------------------------------------------
# Comments
# ---------------------------------------------------------------------------

_COMMENT_RE = re.compile(r"//[^\n]*|/\*.*?\*/", re.DOTALL)


def strip_comments(text: str) -> str:
    return _COMMENT_RE.sub(" ", text)


# ---------------------------------------------------------------------------
# Enums (cEnumsHelper.cpp semantics)
# ---------------------------------------------------------------------------

_ENUM_RE = re.compile(
    r"(typedef)?\s*\benum\s*([A-Za-z_]\w*)?\s*\{([^}]*)\}\s*([A-Za-z_]\w*)?\s*;",
    re.DOTALL,
)

_BINARY = {
    ast.Add: operator.add, ast.Sub: operator.sub, ast.Mult: operator.mul,
    ast.FloorDiv: operator.floordiv, ast.Div: operator.floordiv,
    ast.Mod: operator.mod, ast.LShift: operator.lshift,
    ast.RShift: operator.rshift, ast.BitOr: operator.or_,
    ast.BitAnd: operator.and_, ast.BitXor: operator.xor,
}
_UNARY = {ast.USub: operator.neg, ast.UAdd: operator.pos, ast.Invert: operator.invert}


def _evaluate(expression: str, symbols: dict) -> int:
    """An enum initialiser, with symbols substituted, as an integer."""
    expression = re.sub(r"\b(0[xX][0-9A-Fa-f]+|\d+)[uUlL]*\b", r"\1", expression.strip())

    def visit(node):
        if isinstance(node, ast.Expression):
            return visit(node.body)
        if isinstance(node, ast.Constant) and isinstance(node.value, int):
            return node.value
        if isinstance(node, ast.Name):
            if node.id not in symbols:
                raise KeyError(node.id)
            return symbols[node.id]
        if isinstance(node, ast.BinOp) and type(node.op) in _BINARY:
            return _BINARY[type(node.op)](visit(node.left), visit(node.right))
        if isinstance(node, ast.UnaryOp) and type(node.op) in _UNARY:
            return _UNARY[type(node.op)](visit(node.operand))
        raise ValueError("unsupported enum expression %r" % expression)

    return int(visit(ast.parse(expression, mode="eval")))


def parse_enums(paths) -> dict:
    """``{enum_name: {"values": {value: first_symbol}, "count": members}}``.

    Members are numbered the way C does it; a repeated value keeps the symbol
    declared first, which is the one ``CEnum::get_symbol_of_value`` returns.
    """
    context = {}
    enums = {}
    for path in paths:
        with open(path, "r", encoding="utf-8") as handle:
            source = strip_comments(handle.read())
        for match in _ENUM_RE.finditer(source):
            name = match.group(2) or match.group(4)
            if not name:
                continue
            members = {}
            values = {}
            next_value = 0
            for chunk in match.group(3).split(","):
                label, _, initialiser = chunk.partition("=")
                label = label.strip()
                if not re.fullmatch(r"[A-Za-z_]\w*", label or ""):
                    continue
                if initialiser.strip():
                    scope = dict(context)
                    scope.update(members)
                    value = _evaluate(initialiser, scope)
                else:
                    value = next_value
                members[label] = value
                values.setdefault(value, label)
                next_value = value + 1
            context.update(members)
            enums[name] = {
                "values": {str(k): v for k, v in sorted(values.items())},
                "count": len(members),
            }
    return enums


# ---------------------------------------------------------------------------
# Structs (cStructHelper.cpp semantics)
# ---------------------------------------------------------------------------

_STRUCT_RE = re.compile(
    r"typedef\s+struct\s+(\w+)\s*\{(.*?)\}\s*(\w+)\s*;", re.DOTALL
)
_HINT_RE = re.compile(r"\bHint\s*\(\((.*?)\)\)", re.DOTALL)


def _parse_hint(args: str) -> dict:
    """``Angle, DivideBy:64`` -> ``{"type": "Angle", "DivideBy": "64"}``."""
    hint = {}
    for index, part in enumerate(p.strip() for p in args.split(",")):
        if not part:
            continue
        key, _, value = part.partition(":")
        key = key.strip()
        if index == 0:
            hint["type"] = key
        hint[key] = value.strip()
    return hint


def parse_structs(path) -> dict:
    """Every ``typedef struct`` in ``level_object_entries.h``.

    Offsets are the running sum of member sizes, exactly as
    ``CStruct::calculate_offsets`` computes them - the asset tool does not pad
    for alignment - and a struct's size is the plain sum, which is what the
    object-map extractor checks every entry's size byte against.
    """
    with open(path, "r", encoding="utf-8") as handle:
        source = handle.read()
    # ``Hint(...)`` is a no-op macro sitting after the member's semicolon.
    source = re.sub(r"#define\s+Hint\(args\)[^\n]*", "", source)
    source = strip_comments(source)

    structs = {}
    for match in _STRUCT_RE.finditer(source):
        name = match.group(3)
        body = match.group(2)
        if "union" in body or "struct" in body:
            continue  # LevelObjectEntry itself; never decoded member by member
        members = []
        offset = 0
        statements = body.split(";")
        pending_hint = None
        for statement in statements:
            hint_match = _HINT_RE.search(statement)
            if hint_match:
                # A hint belongs to the member that precedes it.
                if members:
                    for member in members[pending_hint:]:
                        member["hint"] = _parse_hint(hint_match.group(1))
                statement = statement[:hint_match.start()] + statement[hint_match.end():]
            statement = statement.strip()
            if not statement:
                continue
            type_match = re.match(r"([A-Za-z_]\w*)\s+(.*)$", statement, re.DOTALL)
            if not type_match:
                continue
            ctype, declarations = type_match.group(1), type_match.group(2)
            pending_hint = len(members)
            for declaration in declarations.split(","):
                declaration = declaration.strip()
                decl = re.fullmatch(r"([A-Za-z_]\w*)\s*(?:\[\s*([^\]]+)\s*\])?", declaration)
                if not decl:
                    raise ValueError("%s: cannot parse member %r" % (name, declaration))
                count = int(decl.group(2), 0) if decl.group(2) else 1
                if ctype in C_TYPES:
                    size, signed = C_TYPES[ctype]
                elif ctype in structs:
                    size, signed = structs[ctype]["size"], False
                else:
                    raise ValueError("%s: unknown member type %s" % (name, ctype))
                member = {
                    "name": decl.group(1),
                    "type": ctype,
                    "offset": offset,
                    "size": size,
                    "signed": signed,
                }
                if decl.group(2):
                    member["count"] = count
                members.append(member)
                offset += size * count
        structs[name] = {"size": offset, "members": members}
    return structs


# ---------------------------------------------------------------------------
# DKRJP characters (text/dkrText.cpp)
# ---------------------------------------------------------------------------

def parse_dkrjp_characters(path) -> list:
    with open(path, "r", encoding="utf-8-sig") as handle:
        source = handle.read()
    match = re.search(r"DKRJP_FONT_CHARACTERS\s*=\s*\{(.*?)\};", source, re.DOTALL)
    if not match:
        raise ValueError("DKRJP_FONT_CHARACTERS not found in %s" % path)
    body = strip_comments(match.group(1))
    characters = []
    for literal in re.findall(r'"((?:[^"\\]|\\.)*)"', body):
        characters.append(re.sub(r"\\(.)", r"\1", literal))
    return characters


# ---------------------------------------------------------------------------
# The table
# ---------------------------------------------------------------------------

def _decomp_commit(decomp_root):
    try:
        return subprocess.check_output(
            ["git", "-C", decomp_root, "rev-parse", "HEAD"],
            stderr=subprocess.DEVNULL, text=True).strip()
    except (OSError, subprocess.CalledProcessError):
        return ""


def build_tables(decomp_root) -> dict:
    config_path = os.path.join(decomp_root, "tools", "dkr_assets_tool_extract.json")
    with open(config_path, "r", encoding="utf-8") as handle:
        config = json.load(handle)

    include = os.path.join(decomp_root, "include")
    enums = parse_enums([os.path.join(include, "enums.h"),
                         os.path.join(include, "object_behaviors.h")])
    all_structs = parse_structs(os.path.join(include, "level_object_entries.h"))

    entry_order = list(config["misc"]["default-object-entries-order"])
    wanted = set(entry_order) | {"LevelObjectEntryCommon"}
    missing = sorted(wanted - set(all_structs))
    if missing:
        raise ValueError("structs missing from level_object_entries.h: %s" % missing)
    structs = {name: all_structs[name] for name in sorted(wanted)}

    for struct in structs.values():
        for member in struct["members"]:
            hint = member.get("hint") or {}
            if hint.get("type") == "Enum" and hint.get("Enum") not in enums:
                raise ValueError("hint names unknown enum %s" % hint.get("Enum"))

    files = []
    for entry in config["files"]:
        row = [entry["sha1"], entry["build-id"], entry["filename"],
               entry.get("folder", ""), entry.get("type", "")]
        if entry.get("version", "v77") != "v77":
            row.append(entry["version"])
        files.append(row)

    return {
        "schema": SCHEMA_VERSION,
        "source": {
            "decomp-commit": _decomp_commit(decomp_root),
            "dkrat-version": DKRAT_VERSION,
        },
        "inputs": config["inputs-supported"],
        "sections": config["sections"],
        "files": files,
        "menu-text-build-ids":
            config["file-type-attributes"]["MenuText"]["menu-text-build-ids"],
        "default-object-entries-order": entry_order,
        "enums": enums,
        "structs": structs,
        "dkrjp-characters": parse_dkrjp_characters(os.path.join(
            decomp_root, "tools", "dkr_assets_tool_src", "text", "dkrText.cpp")),
    }


def encode(tables) -> bytes:
    text = json.dumps(tables, sort_keys=True, separators=(",", ":"), ensure_ascii=False)
    # mtime=0 keeps the file byte-identical between runs.
    return gzip.compress(text.encode("utf-8"), compresslevel=9, mtime=0)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--decomp", default=DEFAULT_DECOMP)
    parser.add_argument("--output", default=DEFAULT_OUTPUT)
    parser.add_argument("--check", action="store_true",
                        help="fail if the committed table is out of date")
    args = parser.parse_args(argv)

    tables = build_tables(args.decomp)
    data = encode(tables)

    if args.check:
        try:
            with open(args.output, "rb") as handle:
                current = handle.read()
        except OSError:
            current = b""
        if (gzip.decompress(current) if current else b"") != gzip.decompress(data):
            print("FAIL: %s is out of date; run generate_rom_tables.py" % args.output)
            return 1
        print("ok: %s is current" % args.output)
        return 0

    os.makedirs(os.path.dirname(args.output), exist_ok=True)
    with open(args.output, "wb") as handle:
        handle.write(data)
    print("files    : %d" % len(tables["files"]))
    print("sections : %d" % len(tables["sections"]))
    print("enums    : %d" % len(tables["enums"]))
    print("structs  : %d" % len(tables["structs"]))
    print("size     : %.1f KiB" % (len(data) / 1024.0))
    print("wrote    : %s" % os.path.relpath(args.output, REPO_ROOT))
    return 0


if __name__ == "__main__":
    sys.exit(main())
