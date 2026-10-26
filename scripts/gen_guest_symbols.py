#!/usr/bin/env python3
"""Generate the guest symbol headers from runtime-recomp/symbols/*.toml.

Each request file names one header, the symbol table its addresses come from
and a template for the header text:

* ``table = "sdk"``: modding/reference-us-v<revision>/data.toml, the admitted
  address-only SDK kit;
* ``table = "decomp"``: extern/dkr-decomp/ver/symbols/symbol_addrs.us.v<revision>.txt.

``@ALL:<revision>@`` in the template becomes every listed symbol's address for
that revision, comma-separated, and ``@<name>:<revision>@`` one symbol's. An
address prints as ``0x%08X`` (``hex = "upper"``) or ``0x%08x``
(``hex = "lower"``) followed by ``U``. A symbol that is missing or listed more
than once in its table is an error; nothing is guessed.

    gen_guest_symbols.py --out DIR [SPEC...]          write the headers
    gen_guest_symbols.py --out DIR --check [SPEC...]  fail unless DIR holds them

With no SPEC every runtime-recomp/symbols/*.toml is generated. Headers are
written only when their bytes change.

Exit status: 0 on success, 1 on a missing/ambiguous symbol or a --check
difference, 2 on usage errors.
"""

from __future__ import annotations

import argparse
import dataclasses
from pathlib import Path
import re
import sys

import tomllib

ROOT = Path(__file__).resolve().parents[1]
SPECS = ROOT / "runtime-recomp" / "symbols"
REVISIONS = ("77", "80")
DECOMP_LINE = re.compile(r"^([A-Za-z_][A-Za-z0-9_]*) = (0x[0-9A-Fa-f]+);", re.M)


class SymbolError(Exception):
    pass


@dataclasses.dataclass(frozen=True)
class Request:
    header: str
    table: str
    hex: str
    symbols: tuple[str, ...]
    template: str


def load(path: Path) -> Request:
    data = tomllib.loads(path.read_text(encoding="utf-8"))
    if data.get("schema") != 1:
        raise SymbolError(f"{path.name}: unsupported schema")
    request = Request(data["header"], data["table"], data["hex"], tuple(data["symbols"]), data["template"])
    if request.table not in ("sdk", "decomp") or request.hex not in ("upper", "lower"):
        raise SymbolError(f"{path.name}: table must be sdk or decomp, hex upper or lower")
    if "/" in request.header or not request.header.endswith(".hpp"):
        raise SymbolError(f"{path.name}: header must be a bare *.hpp name")
    return request


def table(kind: str, revision: str, root: Path) -> dict[str, list[int]]:
    """Every address each symbol has in one revision's table."""
    found: dict[str, list[int]] = {}
    if kind == "sdk":
        data = tomllib.loads((root / f"modding/reference-us-v{revision}/data.toml").read_text(encoding="utf-8"))
        for section in data["section"]:
            for symbol in section.get("symbols", []):
                found.setdefault(symbol["name"], []).append(symbol["vram"])
    else:
        text = (root / f"extern/dkr-decomp/ver/symbols/symbol_addrs.us.v{revision}.txt").read_text(encoding="utf-8")
        for name, value in DECOMP_LINE.findall(text):
            found.setdefault(name, []).append(int(value, 16))
    return found


def render(request: Request, root: Path) -> str:
    def address(name: str, revision: str, addresses: dict[str, list[int]]) -> str:
        values = addresses.get(name, [])
        if len(values) != 1:
            raise SymbolError(
                f"{request.header}: {name} has {len(values)} addresses in the US v{revision} {request.table} table"
            )
        return (f"0x{values[0]:08X}" if request.hex == "upper" else f"0x{values[0]:08x}") + "U"

    tables = {revision: table(request.table, revision, root) for revision in REVISIONS}

    def substitute(match: re.Match[str]) -> str:
        name, revision = match.group(1), match.group(2)
        if name == "ALL":
            return ",".join(address(symbol, revision, tables[revision]) for symbol in request.symbols)
        if name not in request.symbols:
            raise SymbolError(f"{request.header}: @{name}:{revision}@ is not a listed symbol")
        return address(name, revision, tables[revision])

    return re.sub(r"@([A-Za-z_][A-Za-z0-9_]*):(77|80)@", substitute, request.template)


def generate(specs: list[Path], root: Path | None = None) -> dict[str, str]:
    headers: dict[str, str] = {}
    for spec in specs:
        request = load(spec)
        if request.header in headers:
            raise SymbolError(f"{request.header} is requested twice")
        headers[request.header] = render(request, root or ROOT)
    return headers


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--check", action="store_true")
    parser.add_argument("spec", nargs="*", type=Path)
    args = parser.parse_args(argv)
    specs = args.spec or sorted(SPECS.glob("*.toml"))
    try:
        headers = generate(specs)
    except SymbolError as error:
        print(f"gen_guest_symbols: {error}", file=sys.stderr)
        return 1
    stale = []
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
    for name in stale:
        print(f"gen_guest_symbols: {args.out / name} differs from its request", file=sys.stderr)
    return 1 if stale else 0


if __name__ == "__main__":
    sys.exit(main())
