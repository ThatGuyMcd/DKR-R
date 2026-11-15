#!/usr/bin/env python3
"""Check the owned-engine native boundary facts of the pinned first-party sources.

scripts/generate_replay_probe.py and scripts/character_portrait_pipeline.py pin
whole first-party files by sha256 and the comments beside each pin say what was
audited: no new guest store, no new guest call, no new hook. This script checks
those facts directly, beside the pins (it replaces nothing). For every file in
architecture/owned-native-boundary.toml it recomputes

* stores: guest store sites per function. A site is an assignment (or ++/--, or
  address-of) of a MEM_* macro, a call to a name in [vocabulary] store_calls, or
  a memcpy/memmove/memset;
* guest_calls: the guest functions the file calls. A guest function is a name
  in modding/reference-us-v*/functions.toml, func_XXXXXXXX, or *_recomp, called
  as a plain or namespace-qualified call (not a member call);
* hooks: the extern "C" dkr_* and *_recomp functions the file defines, and
  which of them probe_native.c names (it names the natives the owned engine
  handles, treats as identity, or blocks; it does not say which);
* digests: a token-stream hash of the few functions listed under digests, so
  layout and comments don't matter but any token does.

and compares them with the recorded values. A new store, call or hook, or an
edit inside a digested function, fails until the record is updated with
--record (digests keep the functions already listed), which is the audit step.

    native_boundary.py            check
    native_boundary.py --record   rewrite the record from the tree

What this cannot see (so it is not a proof of simulation equivalence): a changed
value, address or order of an existing store, arithmetic or control flow in a
function that is not digested, a store reached through a helper that is not in
store_calls, and anything a macro or template hides. See docs/CROSS-ARCHITECTURE.md
and the identity guard for the other gates.
"""

from __future__ import annotations

import argparse
import dataclasses
import hashlib
from pathlib import Path
import re
import sys
from typing import cast

import tomllib

sys.path.insert(0, str(Path(__file__).resolve().parent))

from format_tokens import tokens  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
RECORD = ROOT / "architecture/owned-native-boundary.toml"
PROBE_NATIVE = ROOT / "runtime-recomp/tests/replay_probe/probe_native.c"
FUNCTIONS = tuple(ROOT / f"modding/reference-us-v{rev}/functions.toml" for rev in ("77", "80"))

MEM_MACRO = re.compile(r"MEM_[A-Z]+")
BULK_COPY = {"memcpy", "memmove", "memset"}
GUEST_ALWAYS = re.compile(r"func_[0-9A-Fa-f]{8}|\w+_recomp")
HOOK_NAME = re.compile(r"dkr_\w+|\w+_recomp")
COMPOUND = {"+", "-", "*", "/", "%", "|", "&", "^"}
FUNCTION_TAIL = {"const", "noexcept", "override", "final", "volatile"}
SCOPE_WORDS = {"class", "struct", "union", "enum"}
PROBE_NAME = re.compile(r'"((?:dkr_\w+)|(?:\w+_recomp))"')


@dataclasses.dataclass(frozen=True)
class Vocabulary:
    """Names that write guest memory but are not MEM_* macros."""

    store_calls: frozenset[str]
    store_members: frozenset[str]


@dataclasses.dataclass(frozen=True)
class Guest:
    names: frozenset[str]
    payload_fields: frozenset[str]


@dataclasses.dataclass
class Facts:
    stores: dict[str, int]
    guest_calls: list[str]
    hooks: list[str]
    function_tokens: dict[str, list[str]]


@dataclasses.dataclass
class _Function:
    name: str
    extern_c: bool
    start: int  # first token of the signature
    body: int  # index of the opening brace
    end: int  # index of the closing brace


def _match_back(toks: list[str], close: int) -> int:
    depth = 0
    for i in range(close, -1, -1):
        if toks[i] == ")":
            depth += 1
        elif toks[i] == "(":
            depth -= 1
            if depth == 0:
                return i
    return -1


def _segment_start(toks: list[str], brace: int) -> int:
    i = brace
    while i > 0 and toks[i - 1] not in (";", "{", "}"):
        i -= 1
    return i


def _function_name(toks: list[str], open_paren: int) -> str:
    i = open_paren - 1
    if i < 0:
        return ""
    name = toks[i]
    if i >= 1 and toks[i - 1] == "~":
        name = "~" + name
        i -= 1
    while i >= 2 and toks[i - 1] == ":" and toks[i - 2] == ":" and i >= 3:
        name = toks[i - 3] + "::" + name
        i -= 3
    return name


def functions(toks: list[str]) -> list[_Function]:
    """Function definitions outside any function body, by brace matching.

    A brace opens a scope (namespace, extern "C", class), a function body (the
    segment ends in `)` plus qualifiers), or something else (an initialiser).
    Everything inside a function body, lambdas included, belongs to it.
    """
    found: list[_Function] = []
    stack: list[tuple[str, int]] = []  # (kind, index of the function or -1)
    for i, tok in enumerate(toks):
        if tok == "{":
            if any(kind == "function" for kind, _ in stack):
                stack.append(("inner", -1))
                continue
            start = _segment_start(toks, i)
            segment = toks[start:i]
            extern_c = "extern" in segment and any(t.startswith('"') for t in segment)
            if "namespace" in segment or (extern_c and "(" not in segment):
                stack.append(("extern_c" if extern_c else "scope", -1))
                continue
            if (SCOPE_WORDS & set(segment)) and ")" not in segment:
                stack.append(("scope", -1))
                continue
            j = i - 1
            while j >= start and toks[j] in FUNCTION_TAIL:
                j -= 1
            if j >= start and toks[j] == ")":
                open_paren = _match_back(toks, j)
                if open_paren > 0:
                    name = _function_name(toks, open_paren)
                    in_extern_c = extern_c or any(kind == "extern_c" for kind, _ in stack)
                    found.append(_Function(name, in_extern_c, start, i, -1))
                    stack.append(("function", len(found) - 1))
                    continue
            stack.append(("data", -1))
        elif tok == "}":
            if not stack:
                continue
            kind, index = stack.pop()
            if kind == "function":
                found[index].end = i
    return [f for f in found if f.end > 0]


def _match_forward(toks: list[str], open_paren: int) -> int:
    depth = 0
    for i in range(open_paren, len(toks)):
        if toks[i] == "(":
            depth += 1
        elif toks[i] == ")":
            depth -= 1
            if depth == 0:
                return i
    return len(toks) - 1


def _is_assignment(toks: list[str], after: int) -> bool:
    nxt = toks[after + 1 : after + 4] + ["", "", ""]
    if nxt[0] == "=" and nxt[1] != "=":
        return True
    if nxt[0] in COMPOUND and nxt[1] == "=" and nxt[2] != "=":
        return True
    if nxt[0] in ("<", ">") and nxt[1] == nxt[0] and nxt[2] == "=":
        return True
    return (nxt[0] == "+" and nxt[1] == "+") or (nxt[0] == "-" and nxt[1] == "-")


def _store_sites(toks: list[str], first: int, last: int, vocabulary: Vocabulary) -> int:
    count = 0
    for i in range(first, last):
        tok = toks[i]
        if i + 1 >= len(toks) or toks[i + 1] != "(":
            continue
        member = i > 0 and (toks[i - 1] == "." or (toks[i - 1] == ">" and i > 1 and toks[i - 2] == "-"))
        if MEM_MACRO.fullmatch(tok):
            close = _match_forward(toks, i + 1)
            address_of = i > 0 and toks[i - 1] == "&" and (i < 2 or toks[i - 2] in ("(", ",", "=", "return"))
            bound_to_reference = i > 3 and toks[i - 1] == "=" and toks[i - 3] == "&"
            if _is_assignment(toks, close) or address_of or bound_to_reference:
                count += 1
        elif member:
            if tok in vocabulary.store_members:
                count += 1
        elif tok in vocabulary.store_calls or tok in BULK_COPY:
            count += 1
    return count


def _guest_calls(toks: list[str], first: int, last: int, guest: Guest) -> set[str]:
    """Guest functions named between two token indices.

    Native code reaches the guest two ways: through a GamePayload entry point
    (`payload->get_settings(rdram, ...)`, also passed as a pointer), and by a
    plain call to a generated guest function or *_recomp import.
    """
    found: set[str] = set()
    for i in range(first, last):
        tok = toks[i]
        member = i > 0 and (toks[i - 1] == "." or (toks[i - 1] == ">" and i > 1 and toks[i - 2] == "-"))
        if member:
            if tok in guest.payload_fields:
                found.add(tok)
        elif i + 1 < len(toks) and toks[i + 1] == "(" and (tok in guest.names or GUEST_ALWAYS.fullmatch(tok)):
            found.add(tok)
    return found


def analyse(source: bytes, guest: Guest, vocabulary: Vocabulary) -> Facts:
    toks = tokens(source)
    stores: dict[str, int] = {}
    calls: set[str] = set()
    hooks: set[str] = set()
    digest_tokens: dict[str, list[str]] = {}
    for function in functions(toks):
        stores_here = _store_sites(toks, function.body + 1, function.end, vocabulary)
        if stores_here:
            stores[function.name] = stores.get(function.name, 0) + stores_here
        calls |= _guest_calls(toks, function.body + 1, function.end, guest)
        if function.extern_c and HOOK_NAME.fullmatch(function.name):
            hooks.add(function.name)
        digest_tokens.setdefault(function.name, []).extend(toks[function.start : function.end + 1])
    return Facts(dict(sorted(stores.items())), sorted(calls), sorted(hooks), digest_tokens)


def token_digest(function_tokens: list[str]) -> str:
    return hashlib.sha256("\n".join(function_tokens).encode("utf-8", errors="surrogateescape")).hexdigest()


# libc names the guest symbol tables also contain; a host memcpy is not a guest call.
HOST_LIBC = {"memcpy", "memset", "memmove", "sprintf", "sqrtf", "strcat", "strchr", "strcpy", "strlen", "strcmp"}
PAYLOAD_FIELD = re.compile(r"^\s*RecompiledEntrypoint\s+(\w+)\s*;", re.M)


def guest_names(root: Path = ROOT) -> Guest:
    names: set[str] = set()
    for path in (root / f"modding/reference-us-v{rev}/functions.toml" for rev in ("77", "80")):
        data = tomllib.loads(path.read_text(encoding="utf-8"))
        for section in data["section"]:
            for function in section.get("functions", []):
                names.add(str(function["name"]))
    payload = (root / "runtime-recomp/src/game/game_payload.hpp").read_text(encoding="utf-8")
    return Guest(frozenset(names - HOST_LIBC), frozenset(PAYLOAD_FIELD.findall(payload)))


def probe_names(root: Path = ROOT) -> set[str]:
    return set(PROBE_NAME.findall((root / "runtime-recomp/tests/replay_probe/probe_native.c").read_text()))


@dataclasses.dataclass
class Entry:
    path: str
    stores: dict[str, int]
    guest_calls: list[str]
    hooks: list[str]
    hooks_named_in_probe: list[str]
    digests: dict[str, str]


@dataclasses.dataclass
class Record:
    vocabulary: Vocabulary
    entries: list[Entry]


def _strings(value: object) -> list[str]:
    assert isinstance(value, list)
    return [str(item) for item in cast("list[object]", value)]


def _table(value: object) -> dict[str, object]:
    assert isinstance(value, dict)
    return {str(k): v for k, v in cast("dict[object, object]", value).items()}


def load_record(path: Path = RECORD) -> Record:
    data = tomllib.loads(path.read_text(encoding="utf-8"))
    vocabulary = _table(data["vocabulary"])
    entries: list[Entry] = []
    for raw in cast("list[object]", data["file"]):
        row = _table(raw)
        entries.append(
            Entry(
                str(row["path"]),
                {k: int(cast("int", v)) for k, v in _table(row["stores"]).items()},
                _strings(row["guest_calls"]),
                _strings(row["hooks"]),
                _strings(row["hooks_named_in_probe"]),
                {k: str(v) for k, v in _table(row["digests"]).items()},
            )
        )
    return Record(
        Vocabulary(frozenset(_strings(vocabulary["store_calls"])), frozenset(_strings(vocabulary["store_members"]))),
        entries,
    )


def _toml_list(values: list[str]) -> str:
    return "[" + ", ".join(f'"{v}"' for v in values) + "]"


def _key(name: str) -> str:
    return name if re.fullmatch(r"[A-Za-z0-9_-]+", name) else f'"{name}"'


def render(record: Record, header: str) -> str:
    out = [
        header.rstrip("\n"),
        "",
        "schema = 1",
        "",
        "[vocabulary]",
        f"store_calls = {_toml_list(sorted(record.vocabulary.store_calls))}",
        f"store_members = {_toml_list(sorted(record.vocabulary.store_members))}",
        "",
    ]
    for entry in record.entries:
        stores = ", ".join(f"{_key(k)} = {v}" for k, v in sorted(entry.stores.items()))
        digests = ", ".join(f'{_key(k)} = "{v}"' for k, v in sorted(entry.digests.items()))
        out += [
            "[[file]]",
            f'path = "{entry.path}"',
            f"stores = {{{stores}}}",
            f"guest_calls = {_toml_list(entry.guest_calls)}",
            f"hooks = {_toml_list(entry.hooks)}",
            f"hooks_named_in_probe = {_toml_list(entry.hooks_named_in_probe)}",
            f"digests = {{{digests}}}",
            "",
        ]
    return "\n".join(out)


def compare(entry: Entry, facts: Facts, named_in_probe: set[str]) -> list[str]:
    path = entry.path
    problems: list[str] = []
    for function in sorted(set(entry.stores) | set(facts.stores)):
        before, after = entry.stores.get(function, 0), facts.stores.get(function, 0)
        if before != after:
            problems.append(f"{path}: guest stores in {function}: recorded {before}, found {after}")
    for name in sorted(set(entry.guest_calls) ^ set(facts.guest_calls)):
        verb = "no longer called" if name in entry.guest_calls else "newly called"
        problems.append(f"{path}: guest function {name} {verb}")
    for name in sorted(set(entry.hooks) ^ set(facts.hooks)):
        verb = "removed" if name in entry.hooks else "added"
        problems.append(f'{path}: extern "C" hook {name} {verb}')
    actual_named = {h for h in facts.hooks if h in named_in_probe}
    for name in sorted(set(entry.hooks_named_in_probe) ^ actual_named):
        verb = "no longer named" if name in entry.hooks_named_in_probe else "now named"
        problems.append(f"{path}: hook {name} is {verb} in probe_native.c")
    for function, expected in sorted(entry.digests.items()):
        body = facts.function_tokens.get(function)
        if body is None:
            problems.append(f"{path}: digested function {function} not found")
        elif token_digest(body) != expected:
            problems.append(f"{path}: tokens of {function} changed")
    return problems


def check(root: Path = ROOT, record_path: Path = RECORD) -> list[str]:
    record = load_record(record_path)
    guest = guest_names(root)
    named_in_probe = probe_names(root)
    problems: list[str] = []
    for entry in record.entries:
        facts = analyse((root / entry.path).read_bytes(), guest, record.vocabulary)
        problems += compare(entry, facts, named_in_probe)
    return problems


def update(root: Path = ROOT, record_path: Path = RECORD) -> None:
    old = load_record(record_path)
    guest = guest_names(root)
    named_in_probe = probe_names(root)
    entries: list[Entry] = []
    for entry in old.entries:
        facts = analyse((root / entry.path).read_bytes(), guest, old.vocabulary)
        digests: dict[str, str] = {}
        for function in entry.digests:
            body = facts.function_tokens.get(function)
            if body is None:
                raise SystemExit(f"{entry.path}: digested function {function} not found")
            digests[function] = token_digest(body)
        entries.append(
            Entry(
                entry.path,
                facts.stores,
                facts.guest_calls,
                facts.hooks,
                sorted(h for h in facts.hooks if h in named_in_probe),
                digests,
            )
        )
    header = record_path.read_text(encoding="utf-8").split("schema = 1")[0]
    record_path.write_text(render(Record(old.vocabulary, entries), header) + "\n", encoding="utf-8")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--record", action="store_true", help="rewrite the record from the tree")
    args = parser.parse_args(argv)
    if args.record:
        update()
        print(f"native_boundary: recorded {RECORD.relative_to(ROOT)}")
        return 0
    problems = check()
    for problem in problems:
        print(problem)
    print(f"native_boundary: {len(problems)} problem(s)")
    return 1 if problems else 0


if __name__ == "__main__":
    raise SystemExit(main())
