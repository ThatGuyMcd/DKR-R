#!/usr/bin/env python3
"""Generate the guest symbol headers from runtime-recomp/symbols/*.toml.

Each request file names one header, the symbol table its addresses come from
and a template for the header text:

* ``table = "sdk"``: modding/reference-us-v<revision>/data.toml, the admitted
  address-only SDK kit;
* ``table = "decomp"``: extern/dkr-decomp/ver/symbols/symbol_addrs.us.v<revision>.txt.
* ``table = "functions"``: patch sites, ``[[site]]`` entries of ``name``, ``function``
  and a word aligned byte ``offset`` (and ``revisions = ["77"]`` for a site only
  one revision has), resolved against
  modding/reference-us-v<revision>/functions.toml. An unknown function, an offset
  outside the function and a site for a revision lacking the function are errors.
  The template names each site with ``@name:<revision>@``.

With ``table = "sdk"``, ``decomp = [...]`` lists symbols that come from the decomp
list instead, and ``[names.X]`` gives a symbol that is named differently in each
revision's table (``"77" = ...``, ``"80" = ...``).

``@ALL:<revision>@`` in the template becomes every listed symbol's address for
that revision, comma-separated, and ``@<name>:<revision>@`` one symbol's. An
address prints as ``0x%08X`` (``hex = "upper"``) or ``0x%08x``
(``hex = "lower"``) followed by ``U``. A symbol that is missing or listed more
than once in its table is an error; nothing is guessed.

    gen_guest_symbols.py --out DIR [SPEC...]          write the headers
    gen_guest_symbols.py --out DIR --check [SPEC...]  fail unless DIR holds them

``--dependencies DIR`` selects the prepared dependency directory for decomp
tables. Requests and first-party SDK tables stay in this worktree.

With no SPEC every runtime-recomp/symbols/*.toml is generated. Headers are
written only when their bytes change, and a *.hpp in DIR that no SPEC names
is removed (reported, with --check): DIR holds the requested headers only.

Exit status: 0 on success, 1 on a missing/ambiguous symbol or a --check
difference, 2 on usage errors.
"""

from __future__ import annotations

import argparse
import dataclasses
from pathlib import Path
import re
import sys
from typing import Any

import tomllib

ROOT = Path(__file__).resolve().parents[1]
SPECS = ROOT / "runtime-recomp" / "symbols"
REVISIONS = ("77", "80")
DECOMP_LINE = re.compile(r"^([A-Za-z_][A-Za-z0-9_]*) = (0x[0-9A-Fa-f]+);", re.M)


class SymbolError(Exception):
    pass


def _no_names() -> dict[str, dict[str, str]]:
    return {}


@dataclasses.dataclass(frozen=True)
class Site:
    name: str
    function: str
    offset: int
    revisions: tuple[str, ...]


@dataclasses.dataclass(frozen=True)
class Request:
    header: str
    table: str
    hex: str
    symbols: tuple[str, ...]
    template: str
    # Listed symbols that come from the decomp list although ``table`` is sdk.
    decomp: tuple[str, ...] = ()
    # A listed symbol that has a different name in a revision's table, as
    # {symbol: {"77": name, "80": name}} (the SDK names unnamed data D_<address>).
    names: dict[str, dict[str, str]] = dataclasses.field(default_factory=_no_names, hash=False)
    # table = "functions": named patch sites, each a function plus a byte offset
    # into it, resolved per revision from the function tables.
    sites: tuple[Site, ...] = dataclasses.field(default=(), hash=False)


def load(path: Path) -> Request:
    data = tomllib.loads(path.read_text(encoding="utf-8"))
    if data.get("schema") != 1:
        raise SymbolError(f"{path.name}: unsupported schema")
    request = Request(
        data["header"],
        data["table"],
        data["hex"],
        tuple(data.get("symbols", ())),
        data["template"],
        tuple(data.get("decomp", ())),
        {symbol: dict(by_revision) for symbol, by_revision in data.get("names", {}).items()},
        tuple(load_site(path, entry) for entry in data.get("site", ())),
    )
    if request.table == "functions":
        if not request.sites or request.symbols or request.decomp or request.names:
            raise SymbolError(f"{path.name}: a functions request lists [[site]] entries and nothing else")
        if len({site.name for site in request.sites}) != len(request.sites):
            raise SymbolError(f"{path.name}: site names must be unique")
    elif request.sites:
        raise SymbolError(f"{path.name}: [[site]] needs table = functions")
    for listed in (*request.decomp, *request.names):
        if listed not in request.symbols:
            raise SymbolError(f"{path.name}: {listed} is not a listed symbol")
    for symbol, by_revision in request.names.items():
        if set(by_revision) - set(REVISIONS):
            raise SymbolError(f"{path.name}: names for {symbol} may only name revisions {REVISIONS}")
    if request.table not in ("sdk", "decomp", "functions") or request.hex not in ("upper", "lower"):
        raise SymbolError(f"{path.name}: table must be sdk, decomp or functions, hex upper or lower")
    if "/" in request.header or not request.header.endswith(".hpp"):
        raise SymbolError(f"{path.name}: header must be a bare *.hpp name")
    return request


def load_site(path: Path, entry: dict[str, Any]) -> Site:
    missing = [key for key in ("name", "function", "offset") if key not in entry]
    if missing:
        raise SymbolError(f"{path.name}: a [[site]] needs name, function and offset ({missing[0]!r} is absent)")
    name: Any = entry["name"]
    function: Any = entry["function"]
    offset: Any = entry["offset"]
    listed: Any = entry.get("revisions", REVISIONS)
    revisions = tuple(str(revision) for revision in listed)
    if (
        not isinstance(name, str)
        or not isinstance(function, str)
        or not isinstance(offset, int)
        or offset < 0
        or offset % 4
        or not revisions
        or set(revisions) - set(REVISIONS)
    ):
        raise SymbolError(f"{path.name}: site {name} needs a word aligned offset and revisions within {REVISIONS}")
    return Site(name, function, offset, revisions)


def function_table(revision: str, root: Path) -> dict[str, list[tuple[int, int]]]:
    """Every (address, size) each function has in one revision's function table."""
    found: dict[str, list[tuple[int, int]]] = {}
    data = tomllib.loads((root / f"modding/reference-us-v{revision}/functions.toml").read_text(encoding="utf-8"))
    for section in data["section"]:
        for function in section.get("functions", []):
            found.setdefault(function["name"], []).append((function["vram"], function["size"]))
    return found


def table(kind: str, revision: str, root: Path, dependencies: Path | None = None) -> dict[str, list[int]]:
    """Every address each symbol has in one revision's table."""
    found: dict[str, list[int]] = {}
    if kind == "sdk":
        data = tomllib.loads((root / f"modding/reference-us-v{revision}/data.toml").read_text(encoding="utf-8"))
        for section in data["section"]:
            for symbol in section.get("symbols", []):
                found.setdefault(symbol["name"], []).append(symbol["vram"])
    elif kind == "functions":
        raise SymbolError("function tables hold sites; use a [[site]] request")
    else:
        text = (
            (dependencies or root / "extern") / f"dkr-decomp/ver/symbols/symbol_addrs.us.v{revision}.txt"
        ).read_text(encoding="utf-8")
        for name, value in DECOMP_LINE.findall(text):
            found.setdefault(name, []).append(int(value, 16))
    return found


def render_sites(request: Request, root: Path) -> str:
    functions = {revision: function_table(revision, root) for revision in REVISIONS}
    resolved: dict[tuple[str, str], int] = {}
    for site in request.sites:
        for revision in site.revisions:
            found = functions[revision].get(site.function, [])
            if len(found) != 1:
                raise SymbolError(
                    f"{request.header}: {site.function} has {len(found)} entries in the US v{revision} function table"
                )
            base, size = found[0]
            if site.offset >= size:
                raise SymbolError(
                    f"{request.header}: site {site.name} offset {site.offset:#x} is outside "
                    f"{site.function} (size {size:#x}) in US v{revision}"
                )
            resolved[(site.name, revision)] = base + site.offset

    def text(value: int) -> str:
        return (f"0x{value:08X}" if request.hex == "upper" else f"0x{value:08x}") + "U"

    def substitute(match: re.Match[str]) -> str:
        name, revision = match.group(1), match.group(2)
        if name == "ALL":
            return ",".join(
                text(resolved[(site.name, revision)]) for site in request.sites if revision in site.revisions
            )
        if (name, revision) not in resolved:
            raise SymbolError(f"{request.header}: @{name}:{revision}@ is not a site declared for US v{revision}")
        return text(resolved[(name, revision)])

    return re.sub(r"@([A-Za-z_][A-Za-z0-9_]*):(77|80)@", substitute, request.template)


def render(request: Request, root: Path, dependencies: Path | None = None) -> str:
    if request.table == "functions":
        return render_sites(request, root)

    def address(name: str, revision: str, addresses: dict[str, list[int]]) -> str:
        kind = "decomp" if name in request.decomp else request.table
        looked_up = request.names.get(name, {}).get(revision, name)
        values = (decomp_tables[revision] if name in request.decomp else addresses).get(looked_up, [])
        if len(values) != 1:
            raise SymbolError(
                f"{request.header}: {looked_up} has {len(values)} addresses in the US v{revision} {kind} table"
            )
        return (f"0x{values[0]:08X}" if request.hex == "upper" else f"0x{values[0]:08x}") + "U"

    tables = {revision: table(request.table, revision, root, dependencies) for revision in REVISIONS}
    decomp_tables = {
        revision: table("decomp", revision, root, dependencies) for revision in REVISIONS if request.decomp
    }

    def substitute(match: re.Match[str]) -> str:
        name, revision = match.group(1), match.group(2)
        if name == "ALL":
            return ",".join(address(symbol, revision, tables[revision]) for symbol in request.symbols)
        if name not in request.symbols:
            raise SymbolError(f"{request.header}: @{name}:{revision}@ is not a listed symbol")
        return address(name, revision, tables[revision])

    return re.sub(r"@([A-Za-z_][A-Za-z0-9_]*):(77|80)@", substitute, request.template)


def generate(specs: list[Path], root: Path | None = None, dependencies: Path | None = None) -> dict[str, str]:
    headers: dict[str, str] = {}
    for spec in specs:
        request = load(spec)
        if request.header in headers:
            raise SymbolError(f"{request.header} is requested twice")
        headers[request.header] = render(request, root or ROOT, dependencies)
    return headers


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--dependencies", type=Path, default=ROOT / "extern", help="prepared dependency directory")
    parser.add_argument("--check", action="store_true")
    parser.add_argument("spec", nargs="*", type=Path)
    args = parser.parse_args(argv)
    specs = args.spec or sorted(SPECS.glob("*.toml"))
    try:
        headers = generate(specs, dependencies=args.dependencies)
    except SymbolError as error:
        print(f"gen_guest_symbols: {error}", file=sys.stderr)
        return 1
    stale: list[str] = []
    for name, text in sorted(headers.items()):
        path = args.out / name
        current = path.read_bytes() if path.is_file() else None
        if current == text.encode("utf-8"):
            continue
        if args.check:
            stale.append(name)
            continue
        args.out.mkdir(parents=True, exist_ok=True)
        path.write_bytes(text.encode("utf-8"))
    # A header whose request was deleted must not linger for includers to find.
    orphans = sorted(path for path in args.out.glob("*.hpp") if path.name not in headers) if args.out.is_dir() else []
    for path in orphans:
        if args.check:
            print(f"gen_guest_symbols: {path} has no request", file=sys.stderr)
        else:
            path.unlink()
    for name in stale:
        print(f"gen_guest_symbols: {args.out / name} differs from its request", file=sys.stderr)
    return 1 if stale or (args.check and orphans) else 0


if __name__ == "__main__":
    sys.exit(main())
