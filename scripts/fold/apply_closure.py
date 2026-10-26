#!/usr/bin/env python3
"""Write the shipped first-party closure back into runtime-recomp/ (fold step B1).

The reference (scratch, never committed) is a materialization of tag
Version1.0.11: a Linux configure of the full 48-layer release chain whose
``final-sources.json`` lists every first-party file the four shipped binaries
compile, at the staged path the chain left it in. This script puts each of
those files at its shipped path under runtime-recomp/ (flat, no module moves):

* a ``runtime-recomp`` row goes back to its original path;
* a capsule-role file under ``native/{src,include,tests}/`` goes to the same
  path under runtime-recomp/ (the roles are staged copies of that tree);
* every other authored or composed file goes to runtime-recomp/src/game/
  (``mods/`` stays a subdirectory), unless OVERRIDES says otherwise.

Guest-derived files, guest transforms and dependency transforms are left alone
(fold steps B3 and B4).

The chain made those files visible through staged include directories and
absolute ``#include`` paths. The folded build has neither, so every include
directive in the closure is resolved twice: once the way the reference TU
resolved it (its compile command, the staged tree on disk) and once the way the
folded TU will (the same include path with staged directories mapped back into
runtime-recomp/ and per-layer generated directories dropped). Where the two
disagree the directive is respelled as a path relative to the including file.
Nothing else in a file changes.

A shipped binary held more than one version of 13 headers. A single-copy tree
keeps one; scripts/fold/header-variants.json names which and lists the TUs that
compiled against another one (docs/fold-in/header-decisions.md explains each).

    apply_closure.py --reference DIR --summary          row totals, no writes
    apply_closure.py --reference DIR --check            resolve everything, no writes
    apply_closure.py --reference DIR --write-variants   refresh header-variants.json
    apply_closure.py --reference DIR                    write the files

Writing also lists the capsule bases whose pins the folded bytes break in
scripts/fold/released-bases.txt (read by scripts/check_pinned_bytes.py).

Exit status: 0 on success, 1 when the totals, variants or include resolution
do not check out (nothing is written then), 2 on usage errors.
"""

from __future__ import annotations

import argparse
from collections import Counter, defaultdict
from collections.abc import Iterator
import dataclasses
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import sys

import tomllib

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
import check_pinned_bytes  # noqa: E402

VARIANTS = Path("scripts/fold/header-variants.json")
RELEASED_BASES = Path(check_pinned_bytes.RELEASED_BASES)

# Row totals of the v1.0.11 reference (plan 2.3). The script refuses to write
# anything from a final-sources.json that disagrees.
EXPECTED_ROWS = {
    ("first-party", "runtime-recomp", "unchanged"): 482,
    ("first-party", "runtime-recomp", "changed"): 139,
    ("first-party", "authored-verbatim", "added"): 135,
    ("first-party", "authored-modified", "added"): 45,
    ("first-party", "composed-new", "added"): 36,
    ("guest-derived", None, "skipped"): 6,
    ("guest-transform", None, "skipped"): 40,
    ("dependency-transform", "extern", "skipped"): 11,
}
EXPECTED_SUMMARY = {
    "runtime_recomp_paths_changed_vs_main": 112,
    "runtime_recomp_paths_identical_to_main": 394,
}

# Shipped paths that the generic rule would get wrong. The final launcher
# includes the upstream-sync graphics page, which replaces main's dead copy.
OVERRIDES = {
    "layer:upstream-sync/graphics.inl": "runtime-recomp/src/game/runtime_graphics_ui.inl",
}
# Composed headers that named a file on the configuring machine by absolute
# path. lobby-stability wrote librecomp's sp.cpp that way for the
# task_submission.cpp wrapper's macro include; librecomp's own include path
# has librecomp/src, so the bare name reaches the same file anywhere.
# (destination, absolute path inside the work tree, portable spelling)
ABSOLUTE_DEFINES = (
    ("runtime-recomp/src/game/sdk_task_source.hpp", "extern/n64-modern-runtime/librecomp/src/sp.cpp", "sp.cpp"),
)

# Headers scripts/gen_guest_symbols.py generates into the build tree (fold
# step B4). They keep their shipped destination for include resolution, but
# the closure never writes them: runtime-recomp/symbols/*.toml is the source.
GENERATED = frozenset(
    "runtime-recomp/src/game/" + tomllib.loads(path.read_text(encoding="utf-8"))["header"]
    for path in (ROOT / "runtime-recomp" / "symbols").glob("*.toml")
)

INCLUDE = re.compile(rb'^[ \t]*#[ \t]*include(?:_next)?[ \t]*([<"])([^>"\r\n]+)[>"]', re.M)


@dataclasses.dataclass(frozen=True)
class Row:
    kind: str
    origin: str | None
    staged: Path  # absolute, in the reference tree
    sha256: str
    original: str | None
    identical_to_main: bool


@dataclasses.dataclass(frozen=True)
class SearchPath:
    quote: tuple[Path, ...]
    angle: tuple[Path, ...]
    forced: tuple[Path, ...]


@dataclasses.dataclass
class Reference:
    probe: Path  # the reference tree: repo/, capsule/, cfg-*/
    build: Path  # the reference build directory inside probe
    rows: list[Row]
    summary: dict[str, object]
    compdb: dict[Path, SearchPath]
    deps: dict[Path, frozenset[Path]]
    layers: frozenset[str]


@dataclasses.dataclass(frozen=True)
class Edge:
    tu: Path  # reference TU
    includer: Path  # reference file holding the directive
    line: int
    spelling: str
    quoted: bool
    target: Path  # what the reference resolved it to


@dataclasses.dataclass
class Plan:
    destinations: dict[Path, str]  # reference staged path -> repo-relative destination
    contents: dict[str, bytes]  # destination -> bytes to write
    rewrites: dict[str, list[tuple[int, str, str]]]  # destination -> (line, old, new)
    variants: dict[str, list[Row]]  # destination -> rows with differing bytes
    problems: list[str]
    notes: list[str]
    report: dict[str, dict[str, object]]


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def norm(path: str | Path) -> Path:
    return Path(os.path.normpath(path))


def load_reference(directory: Path, root: Path) -> Reference:
    data = json.loads((directory / "final-sources.json").read_text(encoding="utf-8"))
    rows: list[Row] = []
    for entry in data["files"]:
        rows.append(
            Row(
                kind=entry["kind"],
                origin=entry["origin"],
                staged=root / entry["final_staged_path"],
                sha256=entry["sha256"],
                original=entry["original_path"],
                identical_to_main=bool(entry.get("identical_to_main")),
            )
        )
    probe = Path(os.path.commonpath([str(row.staged) for row in rows]))
    while probe.name not in ("", "build") and not (probe / "repo").is_dir():
        probe = probe.parent
    builds = sorted(
        {
            probe / row.staged.relative_to(probe).parts[0]
            for row in rows
            if row.staged.relative_to(probe).parts[1:2] == ("generated",)
        }
    )
    if len(builds) != 1:
        raise SystemExit(f"apply_closure: rows name {len(builds)} build directories under {probe}: {builds}")
    compdb: dict[Path, SearchPath] = {}
    for entry in json.loads((directory / "shipped-compdb.json").read_text(encoding="utf-8")):
        source = norm(Path(entry["directory"]) / entry["file"])
        compdb[source] = search_path(shlex.split(entry["command"]), Path(entry["directory"]))
    deps: dict[Path, frozenset[Path]] = {}
    for entry in json.loads((directory / "depscan.json").read_text(encoding="utf-8")):
        if entry["deps"] is not None:
            deps[norm(entry["source"])] = frozenset(norm(dep) for dep in entry["deps"])
    layers = frozenset(
        path.parent.relative_to(probe / "repo" / "patches").as_posix()
        for path in (probe / "repo" / "patches").rglob("ReleaseOverlay.cmake")
    )
    return Reference(probe, builds[0], rows, data["summary"], compdb, deps, layers)


def search_path(args: list[str], cwd: Path) -> SearchPath:
    """GCC's include search order for one compile command (system dirs omitted)."""
    iquote: list[Path] = []
    include: list[Path] = []
    isystem: list[Path] = []
    after: list[Path] = []
    forced: list[Path] = []
    flags = {"-iquote": iquote, "-I": include, "-isystem": isystem, "-idirafter": after, "-include": forced}
    index = 0
    while index < len(args):
        arg = args[index]
        for flag, bucket in flags.items():
            if arg == flag:
                bucket.append(norm(cwd / args[index + 1]))
                index += 1
                break
            if arg.startswith(flag) and flag in ("-I", "-isystem", "-iquote", "-idirafter"):
                bucket.append(norm(cwd / arg[len(flag) :]))
                break
        index += 1
    system = [path for path in isystem if path not in include] + after
    include = [path for path in include if path not in isystem]
    return SearchPath(tuple(iquote + include + system), tuple(include + system), tuple(forced))


def directives(data: bytes) -> Iterator[tuple[int, bool, str, tuple[int, int]]]:
    """(line, quoted, spelling, byte span of the spelling) per #include."""
    for match in INCLUDE.finditer(data):
        line = data.count(b"\n", 0, match.start()) + 1
        yield line, match.group(1) == b'"', match.group(2).decode("utf-8", "surrogateescape"), match.span(2)


class Resolver:
    """GCC include lookup over the disk plus ``virtual`` files not written yet."""

    def __init__(self, cache: dict[Path, bool] | None = None) -> None:
        self.cache: dict[Path, bool] = cache if cache is not None else {}
        self.virtual: set[Path] = set()

    def exists(self, path: Path) -> bool:
        if path in self.virtual:
            return True
        cached = self.cache.get(path)
        if cached is None:
            cached = self.cache[path] = path.is_file()
        return cached

    def __call__(self, spelling: str, quoted: bool, includer_dir: Path, search: SearchPath) -> Path | None:
        if spelling.startswith("/"):
            candidate = norm(spelling)
            return candidate if self.exists(candidate) else None
        dirs = ((includer_dir,) + search.quote) if quoted else search.angle
        for directory in dirs:
            candidate = norm(directory / spelling)
            if self.exists(candidate):
                return candidate
        return None


def row_class(row: Row) -> tuple[str, str | None, str]:
    if row.kind != "first-party":
        return row.kind, row.origin, "skipped"
    if row.origin == "runtime-recomp":
        return row.kind, row.origin, "unchanged" if row.identical_to_main else "changed"
    return row.kind, row.origin, "added"


def check_totals(reference: Reference) -> list[str]:
    counts = Counter(row_class(row) for row in reference.rows)
    problems = [
        f"row total {key}: {counts.get(key, 0)}, expected {value}"
        for key, value in EXPECTED_ROWS.items()
        if counts.get(key, 0) != value
    ]
    problems += [f"unexpected row class {key} ({value})" for key, value in counts.items() if key not in EXPECTED_ROWS]
    for key, value in EXPECTED_SUMMARY.items():
        if reference.summary.get(key) != value:
            problems.append(f"summary {key}: {reference.summary.get(key)}, expected {value}")
    return problems


def stage_tail(reference: Reference, row: Row) -> tuple[str, list[str]]:
    """The row's stage name and its path inside the stage directory."""
    relative = row.staged.relative_to(reference.probe).parts
    if relative[:2] == ("capsule", "roles"):
        return f"capsule:{relative[2]}", list(relative[3:])
    if relative[0] == reference.build.name and relative[1] == "generated":
        return f"layer:{relative[2]}", list(relative[3:])
    if relative[:2] == ("repo", "patches"):
        return f"authored:{relative[2]}", list(relative[3:])
    raise ValueError(f"unexpected staged location {row.staged}")


def destination(reference: Reference, row: Row, targets: frozenset[str]) -> str:
    if row.origin == "runtime-recomp":
        assert row.original is not None
        return row.original
    stage, tail = stage_tail(reference, row)
    override = OVERRIDES.get(f"{stage}/{'/'.join(tail)}")
    if override:
        return override
    if tail[:1] == ["native"]:
        tail = tail[1:]
    if tail[:1] == ["includes"] and len(tail) > 2:
        tail = tail[2:]
    if tail[:1] and tail[0] in targets:
        tail = tail[1:]
    if tail[:1] and tail[0] in ("src", "include", "tests"):
        return "runtime-recomp/" + "/".join(tail)
    if tail[:1] and tail[0] in ("game", "renderer"):
        tail = tail[1:]
    if len(tail) > 1 and tail[0] != "mods":
        raise ValueError(f"no destination rule for {row.staged}")
    return "runtime-recomp/src/game/" + "/".join(tail)


class Folder:
    """Maps reference-tree paths to the folded tree."""

    def __init__(self, reference: Reference, root: Path, destinations: dict[Path, str]) -> None:
        self.reference = reference
        self.root = root
        self.by_staged = {staged: root / dest for staged, dest in destinations.items()}

    def file(self, path: Path) -> Path | None:
        """Where a referenced file lives in the folded tree; None when the fold drops it."""
        if path in self.by_staged:
            return self.by_staged[path]
        probe = self.reference.probe
        if path.is_relative_to(probe / "repo" / "build"):
            return path  # prepared payloads (visual-model kernels) stay configure inputs
        if path.is_relative_to(probe / "repo" / "patches"):
            return None  # authored layer sources live in runtime-recomp/ once folded
        if path.is_relative_to(probe / "repo"):
            return self.root / path.relative_to(probe / "repo")
        if path.is_relative_to(probe / "capsule" / "roles"):
            # Roles copy modding/ verbatim (the address-only SDK kits).
            parts = path.relative_to(probe / "capsule" / "roles").parts[1:]
            if parts[:1] == ("modding",) and (self.root / Path(*parts)).read_bytes() == path.read_bytes():
                return self.root / Path(*parts)
            return None
        if self.layer_of(path) is not None:
            return None
        return path

    def directory(self, path: Path) -> Path | None:
        probe = self.reference.probe
        if path.is_relative_to(probe / "repo" / "build"):
            return path
        if path.is_relative_to(probe / "repo" / "patches"):
            return None
        if path.is_relative_to(probe / "repo"):
            return self.root / path.relative_to(probe / "repo")
        if path.is_relative_to(probe / "capsule" / "roles"):
            parts = path.relative_to(probe / "capsule" / "roles").parts[1:]
            if parts[:1] == ("modding",):
                return norm(self.root / Path(*parts))
            if parts[:1] == ("native",):
                parts = parts[1:]
            if parts[:1] and parts[0] in ("src", "include", "tests"):
                return norm(self.root / "runtime-recomp" / Path(*parts))
            if parts[:1] == ("renderer",):
                parts = parts[1:]
            return norm(self.root / "runtime-recomp" / "src" / "game" / Path(*parts))
        if self.layer_of(path) is not None:
            return None
        return path

    def layer_of(self, path: Path) -> str | None:
        generated = self.reference.build / "generated"
        if not path.is_relative_to(generated):
            return None
        parts = path.relative_to(generated).parts
        return parts[0] if parts and parts[0] in self.reference.layers else None

    def search(self, search: SearchPath) -> SearchPath:
        def mapped(paths: tuple[Path, ...]) -> tuple[Path, ...]:
            out: list[Path] = []
            for path in paths:
                folded = self.directory(path)
                if folded is not None and folded not in out:
                    out.append(folded)
            return tuple(out)

        forced = tuple(f for f in (self.file(path) for path in search.forced) if f is not None)
        return SearchPath(mapped(search.quote), mapped(search.angle), forced)


def edges(reference: Reference, resolver: Resolver) -> tuple[list[Edge], list[str]]:
    """Every include directive each reference TU with a first-party closure followed.

    The walk goes through third-party headers too: some first-party headers are
    only reached from a staged dependency header. Directives that resolve
    outside the TU's dependency list are a skipped preprocessor branch or a
    system header and are ignored. Headers named only through a macro
    (``#include MBEDTLS_CONFIG_FILE``) are not followed; they are returned as
    notes, and the preprocessed comparison (run_equivalence.sh level 2) covers
    them.
    """
    first_party = {row.staged for row in reference.rows if row.kind == "first-party"}
    parsed: dict[Path, list[tuple[int, bool, str, tuple[int, int]]]] = {}
    found: list[Edge] = []
    notes: list[str] = []
    for tu, search in sorted(reference.compdb.items()):
        deps = reference.deps.get(tu)
        if deps is None or not (tu in first_party or deps & first_party):
            continue
        seen: set[Path] = set()
        stack = [tu, *search.forced]
        while stack:
            current = stack.pop()
            if current in seen or (current != tu and current not in deps and current not in search.forced):
                continue
            if not resolver.exists(current):
                continue  # -MG names a header it could not find; nothing to walk
            seen.add(current)
            if current not in parsed:
                parsed[current] = list(directives(current.read_bytes()))
            for line, quoted, spelling, _ in parsed[current]:
                target = resolver(spelling, quoted, current.parent, search)
                if target is None or target not in deps:
                    continue
                found.append(Edge(tu, current, line, spelling, quoted, target))
                stack.append(target)
        missing = sorted(dep for dep in deps if dep in first_party and dep not in seen)
        notes += [f"{tu}: reached only through a macro include: {dep}" for dep in missing]
    return found, notes


def load_variants(root: Path) -> dict[str, dict[str, object]]:
    path = root / VARIANTS
    if not path.exists():
        return {}
    data = json.loads(path.read_text(encoding="utf-8"))
    return dict(data["headers"])


def tu_name(root: Path, folder: Folder, tu: Path) -> str:
    """A TU as header-variants.json names it: its folded repo path,
    ``staged:<layer>/...`` for a guest or dependency copy a layer staged (fold
    steps B3, B4), or ``payload:<dir>/<file>`` for a prepared-input TU."""
    if tu in folder.by_staged:
        return folder.by_staged[tu].relative_to(root).as_posix()
    for index, part in enumerate(tu.parts):
        if re.fullmatch(r"(owned|model|generated)-v\d+", part):
            return "payload:" + "/".join(tu.parts[index:])
    generated = folder.reference.build / "generated"
    if tu.is_relative_to(generated):
        return "staged:" + tu.relative_to(generated).as_posix()
    folded = folder.file(tu)
    if folded is not None and folded.is_relative_to(root):
        return folded.relative_to(root).as_posix()
    return tu.as_posix()


def forwards_to(row: Row, targets: list[Row]) -> bool:
    """A forwarding header: ``#pragma once`` plus one include of one of ``targets``."""
    lines = [line.strip() for line in row.staged.read_bytes().splitlines() if line.strip()]
    return any(lines == [b"#pragma once", b'#include "' + str(t.staged).encode() + b'"'] for t in targets)


def variant_report(
    reference: Reference, root: Path, folder: Folder, variants: dict[str, list[Row]], chosen: dict[str, Row]
) -> dict[str, dict[str, object]]:
    """The computed half of header-variants.json: who saw which other version."""
    users: dict[Path, set[Path]] = defaultdict(set)
    for tu, deps in reference.deps.items():
        for dep in deps:
            users[dep].add(tu)
    report: dict[str, dict[str, object]] = {}
    for dest, rows in sorted(variants.items()):
        pick = chosen.get(dest)
        if pick is None:
            continue
        others: dict[str, list[Row]] = defaultdict(list)
        for row in rows:
            if row.sha256 != pick.sha256:
                others[row.sha256].append(row)
        entries: list[dict[str, object]] = []
        for sha, group in sorted(others.items()):
            tus = set[Path]().union(*(users[row.staged] for row in group))
            tus |= {row.staged for row in group if row.staged in reference.compdb}
            entries.append(
                {
                    "sha256": sha,
                    "from": sorted(row.staged.relative_to(reference.probe).as_posix() for row in group),
                    "forwards_to_chosen": all(
                        forwards_to(row, [r for r in rows if r.sha256 == pick.sha256]) for row in group
                    ),
                    "tus": sorted({tu_name(root, folder, tu) for tu in tus}),
                }
            )
        kind = "forwarder" if all(entry["forwards_to_chosen"] for entry in entries) else "split"
        report[dest] = {
            "kind": kind,
            "chosen_from": sorted(
                row.staged.relative_to(reference.probe).as_posix() for row in rows if row.sha256 == pick.sha256
            ),
            "others": entries,
        }
    return report


def plan(reference: Reference, root: Path) -> Plan:
    problems = check_totals(reference)
    targets = frozenset(
        part
        for row in reference.rows
        for part in row.staged.parts
        if part.startswith("DKR") or part in ("rt64", "ultramodern", "librecomp")
    )
    folded_rows = [row for row in reference.rows if row.kind == "first-party"]
    destinations: dict[Path, str] = {}
    for row in folded_rows:
        try:
            destinations[row.staged] = destination(reference, row, targets)
        except ValueError as error:
            problems.append(str(error))
    by_destination: dict[str, list[Row]] = defaultdict(list)
    for row in folded_rows:
        if row.staged in destinations:
            by_destination[destinations[row.staged]].append(row)
    choices = load_variants(root)
    variants: dict[str, list[Row]] = {}
    chosen: dict[str, Row] = {}
    for dest, rows in sorted(by_destination.items()):
        shas = {row.sha256 for row in rows}
        if len(shas) == 1:
            chosen[dest] = rows[0]
            continue
        variants[dest] = rows
        pick = choices.get(dest, {}).get("chosen_sha256")
        match = [row for row in rows if row.sha256 == pick]
        if not match:
            problems.append(f"{dest}: {len(shas)} shipped versions and no decision in {VARIANTS}")
            continue
        chosen[dest] = match[0]
    problems += [f"{VARIANTS}: {dest} has one shipped version" for dest in choices if dest not in variants]

    # Folded tree as it will exist: the work tree plus every destination.
    resolver = Resolver()
    folder = Folder(reference, root, destinations)
    reference_edges, notes = edges(reference, resolver)
    folded = Resolver(resolver.cache)
    folded.virtual = {root / dest for dest in chosen}
    report = variant_report(reference, root, folder, variants, chosen)
    for dest, computed in report.items():
        recorded = {key: value for key, value in choices.get(dest, {}).items() if key in computed}
        if recorded != computed:
            problems.append(f"{VARIANTS}: {dest} is stale (rerun with --write-variants)")

    contents: dict[str, bytes] = {dest: row.staged.read_bytes() for dest, row in chosen.items()}
    wanted: dict[tuple[str, int], dict[str, set[Path]]] = defaultdict(lambda: defaultdict(set))
    deferred: set[str] = set()
    for edge in reference_edges:
        includer_dest = destinations.get(edge.includer)
        expected = folder.file(edge.target)
        if includer_dest is None:
            # A third-party includer: it keeps its bytes, so a first-party
            # header it reaches must be found through the include path alone.
            includer = folder.file(edge.includer)
            if edge.target not in destinations:
                continue
            if includer is None:
                deferred.add(f"{edge.includer}: staged by a layer, includes {expected} (fold step B3 or B4)")
                continue
            search = folder.search(reference.compdb[edge.tu])
            actual = folded(edge.spelling, edge.quoted, includer.parent, search)
            if actual != expected:
                problems.append(f"{includer}:{edge.line}: {edge.spelling} reaches {actual}, not {expected}")
            continue
        if chosen.get(includer_dest) is None or chosen[includer_dest].staged != edge.includer:
            continue  # a non-chosen variant's directives; its TUs are listed in header-variants.json
        if expected is None:
            problems.append(
                f"{edge.includer}:{edge.line}: {edge.spelling} resolves to {edge.target}, dropped by the fold"
            )
            continue
        search = folder.search(reference.compdb[edge.tu])
        actual = folded(edge.spelling, edge.quoted, (root / includer_dest).parent, search)
        spelling = edge.spelling
        if actual != expected:
            spelling = os.path.relpath(expected, (root / includer_dest).parent)
            if not edge.quoted or spelling.startswith("/") or expected.is_relative_to(reference.probe):
                problems.append(f"{includer_dest}:{edge.line}: cannot respell <{edge.spelling}> to reach {expected}")
                continue
        wanted[(includer_dest, edge.line)][spelling].add(edge.tu)
    notes += sorted(deferred)

    # Absolute staged includes inside branches no Linux TU takes (iOS, Android,
    # Windows) have no edge above. Map them by path instead.
    for dest, data in contents.items():
        for line, quoted, spelling, _ in directives(data):
            if (dest, line) in wanted or not spelling.startswith(str(reference.probe) + "/"):
                continue
            target = norm(spelling)
            mapped = folder.file(target)
            if mapped is None and target.is_relative_to(reference.probe / "capsule" / "roles"):
                # Not in the Linux closure: a role copy of a file the tree already has.
                parent = folder.directory(target.parent)
                candidate = None if parent is None else parent / target.name
                if candidate is not None and candidate.is_file() and candidate.read_bytes() == target.read_bytes():
                    mapped = candidate
            if mapped is None or not quoted:
                problems.append(f"{dest}:{line}: no folded file matches {spelling}")
                continue
            wanted[(dest, line)][os.path.relpath(mapped, (root / dest).parent)].add(target)

    rewrites: dict[str, list[tuple[int, str, str]]] = defaultdict(list)
    for (dest, line), spellings in sorted(wanted.items()):
        if len(spellings) > 1:
            detail = "; ".join(f"{s!r} for {len(tus)} TUs" for s, tus in spellings.items())
            problems.append(f"{dest}:{line}: TUs need different spellings: {detail}")
            continue
        (spelling,) = spellings
        data = contents[dest]
        for number, _, old, (start, end) in directives(data):
            if number == line and old != spelling:
                rewrites[dest].append((line, old, spelling))
    for dest, changes in rewrites.items():
        data = contents[dest]
        by_line = {line: new for line, _, new in changes}
        pieces: list[bytes] = []
        cursor = 0
        for number, _, _, (start, end) in directives(data):
            if number in by_line:
                pieces += [data[cursor:start], by_line[number].encode("utf-8")]
                cursor = end
        pieces.append(data[cursor:])
        contents[dest] = b"".join(pieces)
    for dest, absolute, portable in ABSOLUTE_DEFINES:
        pattern = re.compile(rb'"/[^"\n]*/' + re.escape(absolute.encode()) + rb'"')
        data, count = pattern.subn(b'"' + portable.encode() + b'"', contents.get(dest, b""))
        if count != 1:
            problems.append(f"{dest}: expected one absolute .../{absolute}, found {count}")
            continue
        contents[dest] = data
    # .gitattributes stores runtime-recomp text as LF (``* text=auto eol=lf``),
    # so stray CRLF lines in a staged copy (the character-portrait font files)
    # would not survive a commit anyway. Write what git will keep.
    for dest, data in contents.items():
        if b"\r\n" in data:
            contents[dest] = data.replace(b"\r\n", b"\n")
            notes.append(f"{dest}: CRLF lines written as LF (.gitattributes eol=lf)")
    return Plan(destinations, contents, dict(rewrites), variants, problems, notes, report)


def released_bases(root: Path, written: dict[str, bytes]) -> list[str]:
    """Capsule bases whose pin the folded bytes break (scripts/check_pinned_bytes.py)."""
    return sorted(
        base
        for base, (pin, _) in check_pinned_bytes.base_pins(root).items()
        if base in written and sha256(written[base].replace(b"\r\n", b"\n")) != pin
    )


def write_variants(root: Path, report: dict[str, dict[str, object]]) -> None:
    """Keep each decision (chosen_sha256, reason) and recompute the rest."""
    decisions = load_variants(root)
    headers: dict[str, dict[str, object]] = {}
    for dest, computed in report.items():
        decision = decisions.get(dest, {})
        headers[dest] = {
            "chosen_sha256": decision.get("chosen_sha256"),
            "reason": decision.get("reason", ""),
            **computed,
        }
    document = {
        "schema": 1,
        "about": "Headers the v1.0.11 binaries held in more than one version, the one the fold keeps, "
        "and the TUs that compiled against another one. Written by scripts/fold/apply_closure.py "
        "--write-variants; only chosen_sha256 and reason are edited by hand. "
        "See docs/fold-in/header-decisions.md.",
        "headers": headers,
    }
    (root / VARIANTS).write_text(json.dumps(document, indent=1) + "\n", encoding="utf-8")


def summary(reference: Reference, result: Plan | None) -> str:
    counts = Counter(row_class(row) for row in reference.rows)
    lines = ["| Class | Rows | Fold action |", "|---|---|---|"]
    actions = {
        "unchanged": "nothing",
        "changed": "write final bytes to the original path",
        "added": "write final bytes to runtime-recomp/ (shipped path)",
        "skipped": "not folded as a file (B3 dependency patches, B4 guest hooks)",
    }
    for key in EXPECTED_ROWS:
        kind, origin, state = key
        lines.append(f"| {kind} {origin or ''} {state} | {counts.get(key, 0)} | {actions[state]} |")
    if result is not None:
        lines.append("")
        lines.append(f"destinations: {len(set(result.destinations.values()))}")
        lines.append(f"files to write: {len(result.contents)}")
        lines.append(f"files with respelled includes: {len(result.rewrites)}")
        lines.append(f"directives respelled: {sum(len(v) for v in result.rewrites.values())}")
        lines.append(f"multi-version headers: {len(result.variants)}")
        lines.append(f"notes: {len(result.notes)} (--verbose lists them)")
    return "\n".join(lines)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=(__doc__ or "").splitlines()[0])
    parser.add_argument("--reference", type=Path, required=True, help="directory holding final-sources.json")
    parser.add_argument("--root", type=Path, default=ROOT, help="repository to write into")
    parser.add_argument("--reference-root", type=Path, help="what final_staged_path is relative to (default --root)")
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--summary", action="store_true", help="print the row table and stop")
    mode.add_argument("--check", action="store_true", help="resolve everything and report, write nothing")
    parser.add_argument("--verbose", action="store_true", help="list every respelled include")
    parser.add_argument(
        "--write-variants", action="store_true", help=f"refresh the computed fields of {VARIANTS} and stop"
    )
    args = parser.parse_args(argv)
    root: Path = args.root.resolve()
    reference = load_reference(args.reference, (args.reference_root or root).resolve())
    if args.summary:
        print(summary(reference, None))
        problems = check_totals(reference)
        for problem in problems:
            print(problem, file=sys.stderr)
        return 1 if problems else 0
    result = plan(reference, root)
    if args.write_variants:
        write_variants(root, result.report)
        return 0
    print(summary(reference, result))
    if args.verbose:
        for dest, changes in sorted(result.rewrites.items()):
            for line, old, new in changes:
                print(f"{dest}:{line}: {old} -> {new}")
        for note in result.notes:
            print(f"note: {note}")
    for problem in result.problems:
        print(problem, file=sys.stderr)
    if result.problems:
        print(f"apply_closure: {len(result.problems)} problems, nothing written", file=sys.stderr)
        return 1
    if args.check:
        return 0
    released = released_bases(root, result.contents)
    written = 0
    for dest, data in sorted(result.contents.items()):
        if dest in GENERATED:
            continue
        target = root / dest
        if target.exists() and target.read_bytes() == data:
            continue
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
        written += 1
    (root / RELEASED_BASES).write_text("".join(f"{base}\n" for base in released), encoding="utf-8")
    print(f"{len(result.contents)} files in the closure, {written} written; {len(released)} capsule bases released")
    return 0


if __name__ == "__main__":
    sys.exit(main())
