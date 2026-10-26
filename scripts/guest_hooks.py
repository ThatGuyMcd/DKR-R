#!/usr/bin/env python3
"""Apply the declared guest hooks to recompiled guest code.

runtime-recomp/guest-hooks/hooks.toml lists every hook the runtime puts into
N64Recomp output: the guest function, which payloads it applies to (``funcs``:
the DKRRecompiledFuncsV77/V80 payload, ``owned``: the experimental owned game),
and up to three edits inside that function's body:

* ``entry``: text inserted at the top of the body;
* ``returns``: text put before every ``return;`` of the original body;
* ``edits``: exact ``match`` -> ``replace`` substitutions, each of which must
  occur exactly once in the body.

Text may use ``{revision}`` (77 or 80), ``{symbol:NAME}`` (the symbol's vram
from modding/reference-us-v<revision>/data.toml, as 0x%08X) and the hook's own
per-revision ``vars``. The body is the generated ``dkr_code_body_<function>``
when the code-mod dispatcher wrapped the function, else the ``RECOMP_FUNC``
itself.

Each hook must apply in exactly one file of the payload, or nothing is written.
Hooked files go to OUT under their own names, and OUT/hooked.txt lists them;
the build compiles those in place of the originals (cmake/GuestHooks.cmake).

    guest_hooks.py apply --spec hooks.toml --in DIR --out DIR --revision 77 --payload funcs

Exit status: 0 on success, 1 when a hook does not apply exactly once, 2 on
usage errors.
"""

from __future__ import annotations

import argparse
from collections.abc import Iterator
import dataclasses
from pathlib import Path
import re
import sys

import tomllib

ROOT = Path(__file__).resolve().parents[1]
PAYLOADS = ("funcs", "owned")
REVISIONS = ("77", "80")


class HookError(Exception):
    pass


@dataclasses.dataclass(frozen=True)
class Hook:
    id: str
    function: str
    payloads: tuple[str, ...]
    entry: str
    returns: str
    edits: tuple[tuple[str, str], ...]


def symbols(revision: str, root: Path | None = None) -> dict[str, int]:
    data = tomllib.loads(((root or ROOT) / f"modding/reference-us-v{revision}/data.toml").read_text(encoding="utf-8"))
    return {symbol["name"]: symbol["vram"] for section in data["section"] for symbol in section.get("symbols", [])}


def expand(text: str, revision: str, variables: dict[str, str], table: dict[str, int]) -> str:
    def value(match: re.Match[str]) -> str:
        key = match.group(1)
        if key == "revision":
            return revision
        if key.startswith("symbol:"):
            name = key.removeprefix("symbol:")
            if name not in table:
                raise HookError(f"unknown symbol {name} for US v{revision}")
            return f"0x{table[name]:08X}"
        if key not in variables:
            raise HookError(f"no value for {{{key}}} in US v{revision}")
        return variables[key]

    return re.sub(r"\{([A-Za-z_][A-Za-z0-9_:]*)\}", value, text)


def load(spec: Path, revision: str, payload: str, root: Path | None = None) -> list[Hook]:
    data = tomllib.loads(spec.read_text(encoding="utf-8"))
    if data.get("schema") != 1:
        raise HookError(f"{spec}: unsupported schema")
    table = symbols(revision, root)
    hooks: list[Hook] = []
    seen: set[str] = set()
    for entry in data.get("hook", []):
        hook_id = entry["id"]
        if hook_id in seen:
            raise HookError(f"duplicate hook id {hook_id}")
        seen.add(hook_id)
        payloads = tuple(entry["payloads"])
        if not payloads or set(payloads) - set(PAYLOADS):
            raise HookError(f"{hook_id}: payloads must be a subset of {PAYLOADS}")
        if payload not in payloads:
            continue
        variables = {key: str(value) for key, value in entry.get("vars", {}).get(revision, {}).items()}

        def text(value: str) -> str:
            return expand(value, revision, variables, table)

        edits = tuple((text(edit["match"]), text(edit["replace"])) for edit in entry.get("edits", []))
        hook = Hook(
            hook_id, entry["function"], payloads, text(entry.get("entry", "")), text(entry.get("returns", "")), edits
        )
        if not (hook.entry or hook.returns or hook.edits):
            raise HookError(f"{hook_id}: no entry, returns or edits")
        hooks.append(hook)
    return hooks


def bodies(text: str, function: str) -> list[tuple[int, int]]:
    """Spans of the function's body, between its braces: the dispatcher's
    wrapped body when there is one, else the function itself."""
    found: list[tuple[int, int]] = []
    for prefix in ("static void dkr_code_body_", "RECOMP_FUNC void "):
        pattern = re.escape(prefix + function) + r"\(uint8_t\* rdram, recomp_context\* ctx\) \{"
        for match in re.finditer(pattern, text):
            found.append((match.end(), closing_brace(text, match.end(), function)))
        if found:
            return found
    return found


def closing_brace(text: str, begin: int, function: str) -> int:
    depth = 1
    for token in re.finditer(r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"|\{|\}', text[begin:]):
        depth += {"{": 1, "}": -1}.get(token.group(), 0)
        if depth == 0:
            return begin + token.start()
    raise HookError(f"{function}: unbalanced braces")


def hooked_body(body: str, hook: Hook) -> str:
    for old, new in hook.edits:
        count = body.count(old)
        if count != 1:
            raise HookError(f"{hook.id}: edit matches {count} times in {hook.function}, not once: {old[:60]!r}")
        body = body.replace(old, new)
    if hook.returns:
        body, count = re.subn(r"\breturn;", lambda _: hook.returns + "return;", body)
        if count == 0:
            raise HookError(f"{hook.id}: {hook.function} has no return")
    if hook.entry:
        body = "\n" + hook.entry + body
    return body


def apply(sources: list[Path], hooks: list[Hook]) -> tuple[dict[str, str], list[str]]:
    """Hooked file text by file name, and one problem per hook that did not
    apply exactly once."""
    hits: dict[str, list[str]] = {hook.id: [] for hook in hooks}
    out: dict[str, str] = {}
    problems: list[str] = []
    for path in sources:
        text = path.read_text(encoding="utf-8")
        changed = False
        for hook in hooks:
            spans = bodies(text, hook.function)
            if not spans:
                continue
            hits[hook.id] += [path.name] * len(spans)
            if len(spans) != 1:
                continue
            start, end = spans[0]
            try:
                text = text[:start] + hooked_body(text[start:end], hook) + text[end:]
            except HookError as error:
                problems.append(f"{path.name}: {error}")
                continue
            changed = True
        if changed:
            out[path.name] = text
    for hook in hooks:
        if len(hits[hook.id]) != 1:
            problems.append(f"{hook.id}: {hook.function} found {len(hits[hook.id])} times {hits[hook.id]}, not once")
    return out, problems


def guest_sources(directory: Path) -> Iterator[Path]:
    yield from sorted(path for path in directory.glob("*.c") if path.is_file())


def run_apply(args: argparse.Namespace) -> int:
    try:
        hooks = load(args.spec, args.revision, args.payload)
        hooked, problems = apply(list(guest_sources(args.input)), hooks)
    except HookError as error:
        problems, hooked = [str(error)], {}
    for problem in problems:
        print(f"guest_hooks: {problem}", file=sys.stderr)
    if problems:
        return 1
    args.out.mkdir(parents=True, exist_ok=True)
    if args.out.resolve() == args.input.resolve():
        print("guest_hooks: --out must differ from --in", file=sys.stderr)
        return 2
    for name, text in sorted(hooked.items()):
        target = args.out / name
        if not target.exists() or target.read_text(encoding="utf-8") != text:
            target.write_text(text, encoding="utf-8", newline="\n")
    (args.out / "hooked.txt").write_text("".join(f"{name}\n" for name in sorted(hooked)), encoding="utf-8")
    return 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest="command", required=True)
    apply_parser = commands.add_parser("apply", help="write the hooked files of one payload")
    apply_parser.add_argument("--spec", type=Path, required=True)
    apply_parser.add_argument("--in", dest="input", type=Path, required=True)
    apply_parser.add_argument("--out", type=Path, required=True)
    apply_parser.add_argument("--revision", choices=REVISIONS, required=True)
    apply_parser.add_argument("--payload", choices=PAYLOADS, required=True)
    args = parser.parse_args(argv)
    return run_apply(args)


if __name__ == "__main__":
    sys.exit(main())
