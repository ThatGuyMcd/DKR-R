#!/usr/bin/env python3
"""Compare a folded build of runtime-recomp/ with the v1.0.11 reference.

Fold plan 2.4. The reference is the materialized release chain
(scripts/fold/apply_closure.py describes it); the folded build is a plain
configure of this tree with the same options and no overlay. Paths are
compared through the same mapping apply_closure.py used to write the files,
so a staged path and its folded path count as equal.

Subcommands (run_equivalence.sh drives them; the ones that compile run in
the build image):

    options     the reference's configure options for the folded tree
    shipped     the compile commands behind the four shipped binaries
    depscan     the headers each shipped TU reads (-M -MG)
    preprocess  -E -P output of each shipped TU
    targets     per-source compile settings (CMake File API codemodel of both
                configures: language, defines, include directories, compile
                fragments), link lines and target dependencies
    level1      the same shipped TUs, the same header closure per TU, and the
                same bytes per file once #include spellings, line endings and
                the one absolute define apply_closure.py makes portable are
                set aside
    level2      preprocessed tokens (file path strings normalized) and compile
                flags of every shipped first-party TU
    level3      objects of the TUs level 2 lets differ for a header variant,
                compiled from path-normalized preprocessed output

Differences that are expected are listed with their reason and do not fail:
the TUs scripts/fold/header-variants.json names (which then need level 3),
the online identity values fold step B7 freezes, the TUs that belong to later
fold steps (dependency patches B3, guest hooks B4), and the entries of
scripts/fold/allowed-deltas.json. Anything else fails.

Exit status: 0 no unexplained differences, 1 unexplained differences,
2 usage errors.
"""

from __future__ import annotations

import argparse
from collections import defaultdict
from collections.abc import Iterable, Iterator
from concurrent.futures import ThreadPoolExecutor
import dataclasses
import difflib
import functools
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys
from typing import cast

sys.path.insert(0, str(Path(__file__).resolve().parent))
import apply_closure  # noqa: E402

ROOT = apply_closure.ROOT
ALLOWED = ROOT / "scripts/fold/allowed-deltas.json"
# Folded targets that compile what one reference target compiled: the game's
# TUs moved into an object library so host builds without a generated payload
# compile them too. Compile units and dependencies are compared under the
# reference name; the executable's link line is compared as it is.
COMPILED_BY = {"DKRPortGameCore": "DKRPortGame"}
BINARIES = (
    "bin/Release/DKR-R",
    "bin/Release/libexec/dkr-r/DKR-R-ModWorker",
    "bin/Release/libexec/dkr-r/DKR-R-Updater",
    "bin/Release/libexec/dkr-r/DKR-R-InputHost",
)
# Online identity: fold step B7 replaces these computed values with the
# v1.0.11 constants, so until then they differ by design.
IDENTITY_DEFINES = re.compile(r"^(DKR_OWNED_BUILD_ID(_V\d+)?|DKR_ENGINE_IDENTITY|DKR_LOBBY_STABILITY_CONTRACT)=")
IDENTITY_DEFINE_FLAG = re.compile(r"^-D(DKR_OWNED_BUILD_ID(_V\d+)?|DKR_ENGINE_IDENTITY|DKR_LOBBY_STABILITY_CONTRACT)=")


@dataclasses.dataclass(frozen=True)
class Group:
    language: str
    defines: tuple[str, ...]
    includes: tuple[str, ...]
    fragments: tuple[str, ...]


@dataclasses.dataclass
class Model:
    source_dir: Path
    build_dir: Path
    targets: dict[str, dict[Path, Group]]  # target -> source -> compile settings
    links: dict[str, tuple[str, ...]]  # executable -> link command fragments
    dependencies: dict[str, frozenset[str]]  # target -> targets it depends on


def load_codemodel(reply: Path) -> Model:
    index = json.loads(sorted(reply.glob("index-*.json"))[-1].read_text(encoding="utf-8"))
    codemodel = json.loads((reply / index["reply"]["codemodel-v2"]["jsonFile"]).read_text(encoding="utf-8"))
    source_dir = Path(codemodel["paths"]["source"])
    build_dir = Path(codemodel["paths"]["build"])
    targets: dict[str, dict[Path, Group]] = {}
    links: dict[str, tuple[str, ...]] = {}
    dependencies: dict[str, frozenset[str]] = {}
    for entry in codemodel["configurations"][0]["targets"]:
        target = json.loads((reply / entry["jsonFile"]).read_text(encoding="utf-8"))
        if "link" in target:
            links[target["name"]] = tuple(f["fragment"] for f in target["link"]["commandFragments"])
        dependencies[target["name"]] = frozenset(d["id"].split("::")[0] for d in target.get("dependencies", []))
        groups = [
            Group(
                language=group["language"],
                defines=tuple(sorted(d["define"] for d in group.get("defines", []))),
                includes=tuple(str(apply_closure.norm(i["path"])) for i in group.get("includes", [])),
                fragments=tuple(f["fragment"] for f in group.get("compileCommandFragments", [])),
            )
            for group in target.get("compileGroups", [])
        ]
        sources: dict[Path, Group] = {}
        for source in target.get("sources", []):
            if "compileGroupIndex" not in source:
                continue
            path = Path(source["path"])
            sources[apply_closure.norm(path if path.is_absolute() else source_dir / path)] = groups[
                source["compileGroupIndex"]
            ]
        targets[target["name"]] = sources
    for folded, original in COMPILED_BY.items():
        if folded in targets:
            targets[original] = {**targets.get(original, {}), **targets.pop(folded)}
            merged = dependencies.get(original, frozenset()) | dependencies.pop(folded, frozenset())
            dependencies[original] = merged - {folded}
    return Model(source_dir, build_dir, targets, links, dependencies)


class Mapping:
    """Reference paths in folded-tree terms (apply_closure.Folder plus the build directory)."""

    def __init__(self, reference_dir: Path, folded_build: Path) -> None:
        self.reference = apply_closure.load_reference(reference_dir, ROOT)
        result = apply_closure.plan(self.reference, ROOT)
        if result.problems:
            raise SystemExit("equivalence: apply_closure.py --check fails; fix that first")
        self.folder = apply_closure.Folder(self.reference, ROOT, result.destinations)
        self.folded_build = folded_build
        self.result = result
        self.aliases = self.build_aliases()

    def build_aliases(self) -> dict[str, str]:
        """Build directories named after a content hash whose inputs include
        script bytes (code-dispatch: scripts/stage_magic_code_payload.py, which
        the tooling commits reformatted). Paired by revision; the run script
        checks that each pair holds the same files."""
        aliases: dict[str, str] = {}
        for revision in ("77", "80"):
            old = sorted((self.reference.build / "generated").glob(f"code-dispatch-v{revision}-*"))
            new = sorted((self.folded_build / "generated").glob(f"code-dispatch-v{revision}-*"))
            if len(old) == 1 and len(new) == 1:
                aliases[old[0].name] = new[0].name
        return aliases

    def build_path(self, path: Path) -> Path:
        parts = list(path.relative_to(self.reference.build).parts)
        if len(parts) > 1 and parts[0] == "generated" and parts[1] in self.aliases:
            parts[1] = self.aliases[parts[1]]
        return self.folded_build.joinpath(*parts)

    def file(self, path: Path) -> Path | None:
        folded = self.folder.file(path)
        if folded is not None and folded.is_relative_to(self.reference.build):
            return self.build_path(folded)
        return folded

    def directory(self, path: Path) -> Path | None:
        folded = self.folder.directory(path)
        if folded is not None and folded.is_relative_to(self.reference.build):
            return self.build_path(folded)
        return folded

    def text(self, value: str) -> str:
        """A flag or define with reference paths rewritten."""
        out = value
        for old, new in self.aliases.items():
            out = out.replace(f"/generated/{old}", f"/generated/{new}")
        out = out.replace(str(self.reference.build) + "/", str(self.folded_build) + "/")
        return out.replace(str(self.reference.probe / "repo") + "/", str(ROOT) + "/")

    def deferred(self, path: Path) -> str | None:
        """Why a reference TU has no folded counterpart yet, or None."""
        layer = self.folder.layer_of(path)
        rows = {row.staged: row for row in self.reference.rows}
        row = rows.get(path)
        if row is not None and row.kind == "dependency-transform":
            return "dependency transform (fold step B3)"
        if row is not None and row.kind in ("guest-transform", "guest-derived"):
            return "guest code (fold step B4)"
        if layer is not None:
            return f"staged by layer {layer} (fold step B3 or B4)"
        return None


def normalized_includes(mapping: Mapping, includes: Iterable[str]) -> tuple[str, ...]:
    out: list[str] = []
    for include in includes:
        folded = mapping.directory(apply_closure.norm(include))
        if folded is not None and str(folded) not in out:
            out.append(str(folded))
    return tuple(out)


@functools.cache
def headers_under(directory: str) -> frozenset[str]:
    """Relative paths of the headers an #include could name under a directory."""
    found: set[str] = set()
    for parent, _, files in os.walk(directory):
        for name in files:
            if name.endswith((".h", ".hh", ".hpp", ".hxx", ".inl", ".inc")):
                found.add(os.path.relpath(os.path.join(parent, name), directory))
    return frozenset(found)


def order_collisions(before: tuple[str, ...], after: tuple[str, ...]) -> list[str]:
    """Header names that two directories whose relative order changed both hold.
    An empty list means the reordering cannot change any lookup."""
    position = {directory: index for index, directory in enumerate(after)}
    shared: set[str] = set()
    for i, first in enumerate(before):
        for second in before[i + 1 :]:
            if position[second] < position[first]:
                shared |= headers_under(first) & headers_under(second)
    return sorted(shared)


def compare_targets(
    mapping: Mapping, reference: Model, folded: Model, shipped_by_target: dict[str, set[Path]]
) -> Iterator[str]:
    """One line per unexplained difference in the shipped TUs' compile settings."""
    for name, shipped in sorted(shipped_by_target.items()):
        ref_sources = reference.targets.get(name, {})
        new_sources = folded.targets.get(name)
        if new_sources is None:
            yield f"{name}: target missing from the folded build"
            continue
        mapped: dict[Path, tuple[Path, Group]] = {}
        for path, group in ref_sources.items():
            if path not in shipped:
                continue
            target = mapping.file(path)
            if target is None:
                reason = mapping.deferred(path)
                if reason is None:
                    yield f"{name}: {path}: no folded counterpart"
                continue
            mapped[target] = (path, group)
        for target, (path, ref) in sorted(mapped.items()):
            new = new_sources.get(target)
            if new is None:
                yield f"{name}: {target.relative_to(ROOT) if target.is_relative_to(ROOT) else target}: not built"
                continue
            label = f"{name}: {target.relative_to(ROOT) if target.is_relative_to(ROOT) else target}"
            if ref.language != new.language:
                yield f"{label}: language {ref.language} -> {new.language}"
            ref_defines = {mapping.text(d) for d in ref.defines if not IDENTITY_DEFINES.match(d)}
            new_defines = {d for d in new.defines if not IDENTITY_DEFINES.match(d)}
            for define in sorted(ref_defines - new_defines):
                yield f"{label}: lost define {define}"
            for define in sorted(new_defines - ref_defines):
                yield f"{label}: extra define {define}"
            # GCC keeps the first of repeated -I directories, so compare
            # deduplicated lists. The layers re-added a relocated TU's original
            # directory as an include; folded, the TU is back in it and quote
            # lookup searches it first anyway, so it is left out of both sides
            # (level 1 still sees any header it would resolve differently).
            own = str(target.parent)
            ref_includes = tuple(i for i in normalized_includes(mapping, ref.includes) if i != own)
            new_includes = tuple(i for i in dict.fromkeys(new.includes) if i != own)
            if ref_includes != new_includes:
                lost = [i for i in ref_includes if i not in new_includes]
                extra = [i for i in new_includes if i not in ref_includes]
                if lost or extra:
                    yield f"{label}: include dirs lost {lost} extra {extra}"
                else:
                    shared = order_collisions(ref_includes, new_includes)
                    if shared:
                        yield f"{label}: include dir order differs and changes lookup of {shared}"
            ref_fragments = tuple(mapping.text(f) for f in ref.fragments)
            if ref_fragments != new.fragments:
                yield f"{label}: flags {list(ref_fragments)} -> {list(new.fragments)}"
        extra_sources = {
            path
            for path in new_sources
            if path not in mapped
            and path.is_relative_to(ROOT / "runtime-recomp")
            and new_sources[path].language in ("C", "CXX")
        }
        for path in sorted(extra_sources):
            yield f"{name}: {path.relative_to(ROOT)}: built, but not in the reference"


def shipped_pairs(compdb: Path) -> dict[str, set[Path]]:
    """target -> sources it compiles into the four shipped binaries."""
    pairs: dict[str, set[Path]] = defaultdict(set)
    for entry in json.loads(compdb.read_text(encoding="utf-8")):
        match = re.search(r"CMakeFiles/([^/]+)\.dir/", entry["output"])
        if match:
            target = COMPILED_BY.get(match.group(1), match.group(1))
            pairs[target].add(apply_closure.norm(Path(entry["directory"]) / entry["file"]))
    return pairs


def common_order(old: tuple[str, ...], new: tuple[str, ...]) -> list[str]:
    """The longest subsequence both link lines share, in order."""
    matcher = difflib.SequenceMatcher(a=old, b=new, autojunk=False)
    return [item for block in matcher.get_matching_blocks() for item in old[block.a : block.a + block.size]]


def compare_links(
    mapping: Mapping, reference: Model, folded: Model, shipped_by_target: dict[str, set[Path]]
) -> Iterator[str]:
    """The four shipped executables' link lines, and every shipped target's dependencies."""
    for name in sorted(shipped_by_target):
        if name in folded.dependencies and reference.dependencies.get(name) != folded.dependencies[name]:
            old, new = reference.dependencies.get(name, frozenset()), folded.dependencies[name]
            yield f"{name}: dependencies lost {sorted(old - new)} extra {sorted(new - old)}"
    for name, fragments in sorted(reference.links.items()):
        if name not in folded.links or name not in shipped_by_target:
            continue
        old = tuple(mapping.text(fragment) for fragment in fragments)
        new = folded.links[name]
        if old == new:
            continue
        moved = sorted({f for f in set(old) | set(new) if old.count(f) == new.count(f)} - set(common_order(old, new)))
        if sorted(old) == sorted(new) and moved:
            yield f"{name}: link order moved {moved}, everything else in order"
        else:
            yield f"{name}: link line {list(old)} -> {list(new)}"


def load_allowed(path: Path) -> list[dict[str, str]]:
    return list(json.loads(path.read_text(encoding="utf-8"))["deltas"]) if path.exists() else []


def explained(problem: str, allowed: list[dict[str, str]]) -> str | None:
    for entry in allowed:
        if problem == entry["difference"]:
            return entry["reason"]
    return None


def run_targets(args: argparse.Namespace) -> int:
    mapping = Mapping(args.reference, args.folded_build)
    reference = load_codemodel(args.reference / "codemodel")
    folded = load_codemodel(args.folded_build / ".cmake/api/v1/reply")
    shipped = shipped_pairs(args.reference / "shipped-compdb.json")
    if args.target:
        shipped = {name: sources for name, sources in shipped.items() if name in args.target}
    allowed = load_allowed(ALLOWED)
    found = list(compare_targets(mapping, reference, folded, shipped))
    found += compare_links(mapping, reference, folded, shipped)
    problems: list[str] = []
    for problem in found:
        portable = problem.replace(str(args.folded_build), "<build>").replace(str(ROOT) + "/", "")
        reason = explained(portable, allowed)
        if reason is None:
            problems.append(portable)
            print(portable)
        elif args.verbose:
            print(f"allowed: {portable} ({reason})")
    print(
        f"targets: {len(shipped)} compared, {len(found) - len(problems)} allowed, "
        f"{len(problems)} unexplained differences",
        file=sys.stderr,
    )
    return 1 if problems else 0


def write_shipped(args: argparse.Namespace) -> int:
    """The compile commands whose objects link into the four shipped binaries."""
    build: Path = args.build
    compdb = json.loads(
        subprocess.run(["ninja", "-C", str(build), "-t", "compdb"], check=True, capture_output=True, text=True).stdout
    )
    inputs = subprocess.run(
        ["ninja", "-C", str(build), "-t", "inputs", *BINARIES], check=True, capture_output=True, text=True
    ).stdout.split()
    objects = {line for line in inputs if line.endswith(".o")}
    kept = [entry for entry in compdb if entry["output"] in objects]
    args.out.write_text(json.dumps(kept, indent=1) + "\n", encoding="utf-8")
    print(f"shipped TUs: {len(kept)}", file=sys.stderr)
    return 0


def compile_args(entry: dict[str, str]) -> list[str]:
    """A compile command without its output, dependency-file and -c arguments."""
    args = shlex.split(entry["command"])
    out: list[str] = []
    index = 0
    while index < len(args):
        arg = args[index]
        if arg in ("-o", "-MF", "-MT", "-MQ"):
            index += 2
            continue
        if arg not in ("-c", "-MD", "-MMD"):
            out.append(arg)
        index += 1
    return out


def first_party_or_prepared(entry: dict[str, str], build: Path) -> bool:
    """TUs the closure is about: everything outside extern/ and the dependency
    build trees (generated payload TUs included, fold plan review F10)."""
    source = apply_closure.norm(Path(entry["directory"]) / entry["file"])
    if "/extern/" in str(source):
        return False
    return not source.is_relative_to(build) or source.is_relative_to(build / "generated")


def run_tool(command: list[str], cwd: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        command, cwd=cwd, capture_output=True, text=True, env={**os.environ, "SOURCE_DATE_EPOCH": "0"}
    )


TU = tuple[str, Path]  # (CMake target, source)


def tu_key(entry: dict[str, str]) -> TU:
    match = re.search(r"CMakeFiles/([^/]+)\.dir/", entry["output"])
    target = match.group(1) if match else entry["output"]
    return COMPILED_BY.get(target, target), apply_closure.norm(Path(entry["directory"]) / entry["file"])


def load_entries(compdb: Path) -> dict[TU, dict[str, str]]:
    return {tu_key(entry): entry for entry in json.loads(compdb.read_text(encoding="utf-8"))}


def output_name(key: TU) -> str:
    return hashlib.sha256(f"{key[0]}|{key[1]}".encode()).hexdigest()[:24]


def run_depscan(args: argparse.Namespace) -> int:
    """Run inside the build image: the headers each TU reads (-M -MG)."""
    entries = [e for e in load_entries(args.compdb).values() if first_party_or_prepared(e, args.build)]

    def scan(entry: dict[str, str]) -> dict[str, object]:
        target, source = tu_key(entry)
        done = run_tool([*compile_args(entry), "-M", "-MG", "-MF", "/dev/stdout", "-w"], entry["directory"])
        if done.returncode:
            return {"target": target, "source": str(source), "deps": None, "error": done.stderr[-400:]}
        text = done.stdout.replace("\\\n", " ")
        deps = text.split(":", 1)[1].split() if ":" in text else []
        folded = [str(apply_closure.norm(Path(entry["directory"]) / dep)) for dep in deps]
        return {"target": target, "source": str(source), "deps": folded, "error": None}

    with ThreadPoolExecutor(args.jobs) as pool:
        results = list(pool.map(scan, entries))
    args.out.write_text(json.dumps(results) + "\n", encoding="utf-8")
    errors = [result for result in results if result["error"]]
    for result in errors:
        print(f"{result['source']}: {result['error']}", file=sys.stderr)
    print(f"depscan: {len(results)} TUs, {len(errors)} errors", file=sys.stderr)
    return 1 if errors else 0


def run_preprocess(args: argparse.Namespace) -> int:
    """Run inside the build image: -E -P output of every first-party TU.
    TUs the build generates (compiled shaders) do not exist after a configure
    and are skipped."""
    candidates = [e for e in load_entries(args.compdb).values() if first_party_or_prepared(e, args.build)]
    entries = [e for e in candidates if tu_key(e)[1].is_file()]
    print(f"preprocess: skipping {len(candidates) - len(entries)} build-time generated TUs", file=sys.stderr)
    args.out.mkdir(parents=True, exist_ok=True)

    def preprocess(entry: dict[str, str]) -> tuple[TU, str | None]:
        key = tu_key(entry)
        out = args.out / f"{output_name(key)}.i"
        done = run_tool([*compile_args(entry), "-E", "-P", "-w", "-o", str(out)], entry["directory"])
        return key, None if done.returncode == 0 else f"exit {done.returncode}: {done.stderr[:600]}"

    with ThreadPoolExecutor(args.jobs) as pool:
        results = list(pool.map(preprocess, entries))
    # rt64's water renderer includes compiled shader headers the build writes later.
    pending = [
        (key, error) for key, error in results if error and re.search(r"shaders/[^:\s]+\.h: No such file", error)
    ]
    errors = [(key, error) for key, error in results if error and (key, error) not in pending]
    for key, error in errors:
        print(f"{key[0]}: {key[1]}: {error}", file=sys.stderr)
    (args.out / "pending.json").write_text(
        json.dumps(sorted(f"{output_name(key)}.i" for key, _ in pending)) + "\n", encoding="utf-8"
    )
    print(
        f"preprocess: {len(results)} TUs, {len(pending)} need build-time shader headers, {len(errors)} errors",
        file=sys.stderr,
    )
    return 1 if errors else 0


INCLUDE_SPELLING = re.compile(rb'^([ \t]*#[ \t]*include(?:_next)?[ \t]*)[<"][^>"\r\n]*[>"]', re.M)


def comparable(data: bytes) -> bytes:
    """File bytes with what the fold may change set aside: #include spellings,
    line endings and the absolute define apply_closure.py makes portable."""
    data = INCLUDE_SPELLING.sub(rb"\1<>", data.replace(b"\r\n", b"\n"))
    for _, absolute, portable in apply_closure.ABSOLUTE_DEFINES:
        data = re.sub(rb'"/[^"\n]*/' + re.escape(absolute.encode()) + rb'"', b'"' + portable.encode() + b'"', data)
    return data


def variant_tus(root: Path, *, forwarders: bool) -> dict[str, set[str]]:
    """header -> TUs that compiled against a version the fold did not keep.
    Forwarder TUs read other bytes but the same tokens; ``forwarders`` says
    whether to count them."""
    found: dict[str, set[str]] = {}
    for header, entry in apply_closure.load_variants(root).items():
        others = cast("list[dict[str, object]]", entry.get("others", []))
        tus: set[str] = set()
        for other in others:
            if forwarders or not other.get("forwards_to_chosen"):
                tus |= set(cast("list[str]", other.get("tus", [])))
        found[header] = tus
    return found


def load_deps(path: Path) -> dict[TU, frozenset[Path]]:
    return {
        (entry["target"], apply_closure.norm(entry["source"])): frozenset(apply_closure.norm(d) for d in entry["deps"])
        for entry in json.loads(path.read_text(encoding="utf-8"))
        if entry["deps"] is not None
    }


@dataclasses.dataclass
class Pairing:
    """Reference TUs and their folded counterparts, keyed by (target, source)."""

    pairs: dict[TU, TU]  # reference -> folded
    notes: list[str]
    unpaired_folded: set[TU]


def pair_tus(mapping: Mapping, reference: dict[TU, dict[str, str]], folded: dict[TU, dict[str, str]]) -> Pairing:
    first_party = {row.staged for row in mapping.reference.rows if row.kind == "first-party"}
    pairs: dict[TU, TU] = {}
    notes: list[str] = []
    for target, source in sorted(reference):
        deps = mapping.reference.deps.get(source, frozenset())
        if source not in first_party and not deps & first_party:
            continue
        folded_source = mapping.file(source)
        if folded_source is None:
            notes.append(f"{target}: {source}: {mapping.deferred(source) or 'no folded counterpart'}")
            continue
        pairs[(target, source)] = (target, folded_source)
    every = {(target, mapping.file(source)) for target, source in reference}
    unpaired = {key for key in folded if key[1].is_relative_to(ROOT / "runtime-recomp")} - every
    return Pairing(pairs, notes, unpaired)


def run_level1(args: argparse.Namespace) -> int:
    mapping = Mapping(args.reference, args.folded_build)
    first_party = {row.staged for row in mapping.reference.rows if row.kind == "first-party"}
    reference_entries = load_entries(args.reference / "shipped-compdb.json")
    folded_entries = load_entries(args.folded_compdb)
    reference_deps = load_deps(args.reference / "depscan-by-target.json")
    folded_deps = load_deps(args.folded_deps)
    pairing = pair_tus(mapping, reference_entries, folded_entries)
    allow = variant_tus(ROOT, forwarders=True)
    allowed = load_allowed(ALLOWED)
    problems: list[str] = []
    notes = list(pairing.notes)
    same_bytes: dict[tuple[Path, Path], bool] = {}

    def name(path: Path) -> str:
        return str(path).replace(str(args.folded_build), "<build>").replace(str(ROOT) + "/", "")

    for old_key, new_key in sorted(pairing.pairs.items()):
        label = f"{new_key[0]}: {name(new_key[1])}"
        tu_name = apply_closure.tu_name(ROOT, mapping.folder, old_key[1])
        if new_key not in folded_entries:
            problems.append(f"{label}: not a shipped TU of the folded build")
            continue
        old, new = reference_deps.get(old_key), folded_deps.get(new_key)
        if old is None or new is None:
            problems.append(f"{label}: no dependency scan")
            continue
        expected: set[Path] = set()
        for dep in old:
            folded = mapping.file(dep)
            if folded is None:
                reason = mapping.deferred(dep)
                if reason is None:
                    problems.append(f"{label}: read {name(dep)}, which the fold drops")
                else:
                    notes.append(f"{label}: read {name(dep)}: {reason}")
                continue
            expected.add(folded)
            if dep not in first_party:
                continue
            if (dep, folded) not in same_bytes:
                same_bytes[(dep, folded)] = folded.is_file() and comparable(dep.read_bytes()) == comparable(
                    folded.read_bytes()
                )
            if not same_bytes[(dep, folded)] and tu_name not in allow.get(name(folded), set()):
                problems.append(f"{label}: {name(folded)} differs from what the reference TU read")
        problems += [f"{label}: no longer reads {name(dep)}" for dep in sorted(expected - new) if dep.exists()]
        problems += [f"{label}: now reads {name(dep)}" for dep in sorted(new - expected)]
    problems += [
        f"{key[0]}: {name(key[1])}: shipped by the folded build only" for key in sorted(pairing.unpaired_folded)
    ]
    unexplained = [problem for problem in problems if explained(problem, allowed) is None]
    for problem in unexplained:
        print(problem)
    if args.verbose:
        for note in notes:
            print(f"note: {note}")
    print(
        f"level1: {len(pairing.pairs)} TUs, {len(same_bytes)} files byte-checked, {len(notes)} notes "
        f"(fold steps B3/B4), {len(problems) - len(unexplained)} allowed, {len(unexplained)} unexplained",
        file=sys.stderr,
    )
    return 1 if unexplained else 0


QUOTED_PATH = re.compile(r'"(/[^"\n]+)"')
DIGEST = re.compile(r"[0-9a-f]{64}")


def path_string(match: re.Match[str], mapping: Mapping | None) -> str:
    """A quoted absolute path, normalized and (with a mapping) folded."""
    path = apply_closure.norm(match.group(1))
    folded = mapping.file(path) if mapping is not None else path
    return f'"{folded if folded is not None else path}"'


def tokens(text: str, mapping: Mapping | None) -> list[str]:
    """Whitespace-separated tokens. File path strings (__FILE__, assert
    messages) are normalized, and with a mapping, reference paths are
    rewritten to their folded paths: a respelled include changes the spelling
    __FILE__ reports, not the file."""

    return QUOTED_PATH.sub(lambda match: path_string(match, mapping), text).split()


def normalized_command(mapping: Mapping | None, entry: dict[str, str]) -> list[str]:
    """A compile command without the compiler, output, include-path and source
    arguments (include paths are compared by the targets subcommand)."""
    args = compile_args(entry)[1:]
    # The layers set LANGUAGE on relocated sources, so CMake spelled out the
    # language their extension already implies.
    language = {".c": "c", ".cc": "c++", ".cpp": "c++", ".cxx": "c++"}.get(Path(entry["file"]).suffix)
    for index in range(len(args) - 1):
        if args[index] == "-x" and args[index + 1] == language:
            args = args[:index] + args[index + 2 :]
            break
    out: list[str] = []
    skip = False
    for arg in args:
        if skip:
            skip = False
            continue
        if arg in ("-I", "-isystem", "-iquote", "-idirafter"):
            skip = True
            continue
        if arg.startswith(("-I", "-isystem", "-iquote", "-idirafter")) or arg == entry["file"]:
            continue
        out.append(mapping.text(arg) if mapping is not None else arg)
    return out


def run_level2(args: argparse.Namespace) -> int:
    mapping = Mapping(args.reference, args.folded_build)
    reference_entries = load_entries(args.reference / "shipped-compdb.json")
    folded_entries = load_entries(args.folded_compdb)
    pairing = pair_tus(mapping, reference_entries, folded_entries)
    split = variant_tus(ROOT, forwarders=False)
    level3_names = set[str]().union(*split.values())
    allowed = load_allowed(ALLOWED)
    problems: list[str] = []
    level3: list[str] = []
    identity_only: list[str] = []
    pending: set[str] = set()
    for directory in (args.reference_pp, args.folded_pp):
        if (directory / "pending.json").exists():
            pending |= set(json.loads((directory / "pending.json").read_text(encoding="utf-8")))
    same = 0
    for old_key, new_key in sorted(pairing.pairs.items()):
        if new_key not in folded_entries:
            continue
        label = f"{new_key[0]}: {str(new_key[1]).replace(str(ROOT) + '/', '')}"
        old_cmd = normalized_command(mapping, reference_entries[old_key])
        new_cmd = normalized_command(None, folded_entries[new_key])
        old_flags = [a for a in old_cmd if not IDENTITY_DEFINE_FLAG.match(a)]
        new_flags = [a for a in new_cmd if not IDENTITY_DEFINE_FLAG.match(a)]
        if old_flags != new_flags:
            problems.append(f"{label}: compile flags {old_cmd} -> {new_cmd}")
        identity_changed = old_cmd != new_cmd
        old_file = args.reference_pp / f"{output_name(old_key)}.i"
        new_file = args.folded_pp / f"{output_name(new_key)}.i"
        if old_file.name in pending or new_file.name in pending:
            continue  # third-party TU waiting for build-time shader headers
        if not old_file.is_file() or not new_file.is_file():
            missing = "reference" if not old_file.is_file() else "folded"
            problems.append(f"{label}: no {missing} preprocessed output")
            continue
        old = tokens(old_file.read_text(errors="surrogateescape"), mapping)
        new = tokens(new_file.read_text(errors="surrogateescape"), None)
        if old == new:
            same += 1
            continue
        if apply_closure.tu_name(ROOT, mapping.folder, old_key[1]) in level3_names:
            level3.append(label)
            continue
        # Fold step B7 freezes the online identity; until then a TU whose
        # identity defines changed may differ in those 64-hex digests only.
        if identity_changed and [DIGEST.sub("<id>", t) for t in old] == [DIGEST.sub("<id>", t) for t in new]:
            identity_only.append(label)
            continue
        first = next((i for i, (a, b) in enumerate(zip(old, new)) if a != b), min(len(old), len(new)))
        was, now = " ".join(old[first : first + 8]), " ".join(new[first : first + 8])
        problems.append(f"{label}: tokens differ at {first}: {was!r} vs {now!r}")
    unexplained = [problem for problem in problems if explained(problem, allowed) is None]
    for problem in unexplained:
        print(problem)
    for label in level3:
        print(f"level 3 required: {label}")
    for label in identity_only:
        print(f"identity only (B7): {label}")
    print(
        f"level2: {same} TUs token-identical, {len(level3)} header-variant TUs left for level 3, "
        f"{len(identity_only)} identity-only (B7), {len(pending)} waiting for build-time shaders, "
        f"{len(problems) - len(unexplained)} allowed, {len(unexplained)} unexplained",
        file=sys.stderr,
    )
    return 1 if unexplained else 0


def object_sections(path: Path) -> dict[str, list[str]]:
    """Disassembly with relocations and the contents of every section except
    comments, notes and debug information, one block per section, sorted by
    section name (COMDAT order follows first use, which a header variant may
    move)."""
    blocks: dict[str, list[str]] = defaultdict(list)
    disassembly = subprocess.run(
        ["objdump", "-d", "-r", "--no-show-raw-insn", str(path)], check=True, capture_output=True, text=True
    ).stdout
    section = ""
    for line in disassembly.splitlines():
        if line.startswith("Disassembly of section "):
            section = "text " + line.removeprefix("Disassembly of section ").rstrip(":")
        elif section and "file format" not in line:
            blocks[section].append(line)
    dump = subprocess.run(["objdump", "-s", str(path)], check=True, capture_output=True, text=True).stdout
    section = ""
    for line in dump.splitlines():
        if line.startswith("Contents of section "):
            name = line.removeprefix("Contents of section ").rstrip(":")
            section = "" if name.startswith((".comment", ".note", ".debug")) else "data " + name
        elif section:
            blocks[section].append(line)
    return dict(blocks)


def object_text(sections: dict[str, list[str]]) -> str:
    return "\n".join(f"== {name}\n" + "\n".join(lines) for name, lines in sorted(sections.items()))


PREPROCESSOR_FLAGS = ("-D", "-U", "-I", "-isystem", "-iquote", "-idirafter", "-include", "-imacros")


def codegen_args(entry: dict[str, str]) -> list[str]:
    """A compile command minus preprocessor options, for compiling -E output."""
    args = compile_args(entry)
    out: list[str] = [args[0]]
    index = 1
    while index < len(args):
        arg = args[index]
        if arg in PREPROCESSOR_FLAGS:
            index += 2
            continue
        if arg.startswith(PREPROCESSOR_FLAGS) or arg == entry["file"] or arg.endswith(entry["file"]):
            index += 1
            continue
        if arg == "-x":
            index += 2
            continue
        out.append(arg)
        index += 1
    return out


def run_level3(args: argparse.Namespace) -> int:
    """Run inside the build image: object equality for the header-variant TUs
    (the ones level 2 lets differ). Each side's -E -P output is compiled with
    its file path strings normalized as level 2 compares them, so only the
    tokens the header variant changes can make the objects differ."""
    mapping = Mapping(args.reference, args.folded_build)
    reference_entries = load_entries(args.reference / "shipped-compdb.json")
    folded_entries = load_entries(args.folded_compdb)
    pairing = pair_tus(mapping, reference_entries, folded_entries)
    split = set[str]().union(*variant_tus(ROOT, forwarders=False).values())
    args.out.mkdir(parents=True, exist_ok=True)
    problems: list[str] = []
    checked: list[str] = []
    for old_key, new_key in sorted(pairing.pairs.items()):
        if apply_closure.tu_name(ROOT, mapping.folder, old_key[1]) not in split or new_key not in folded_entries:
            continue
        label = f"{new_key[0]}: {str(new_key[1]).replace(str(ROOT) + '/', '')}"
        old_text = (args.reference_pp / f"{output_name(old_key)}.i").read_text(errors="surrogateescape")
        new_text = (args.folded_pp / f"{output_name(new_key)}.i").read_text(errors="surrogateescape")
        if tokens(old_text, mapping) == tokens(new_text, None):
            continue  # already token-identical at level 2
        language = "cpp-output" if new_key[1].suffix == ".c" else "c++-cpp-output"
        objects: list[dict[str, list[str]]] = []
        sides = (
            ("reference", old_key, reference_entries, args.reference_pp, mapping),
            ("folded", new_key, folded_entries, args.folded_pp, None),
        )
        for side, key, entries, pp, side_mapping in sides:
            source = (pp / f"{output_name(key)}.i").read_text(errors="surrogateescape")
            normalized = QUOTED_PATH.sub(lambda m, sm=side_mapping: path_string(m, sm), source)
            text = args.out / f"{side}-{output_name(key)}.i"
            text.write_text(normalized, errors="surrogateescape")
            obj = text.with_suffix(".o")
            entry = entries[key]
            done = run_tool([*codegen_args(entry), "-x", language, str(text), "-c", "-o", str(obj)], entry["directory"])
            if done.returncode:
                problems.append(f"{label}: {side} compile failed: {done.stderr[-300:]}")
                break
            objects.append(object_sections(obj))
        if len(objects) != 2:
            continue
        checked.append(label)
        old_sections, new_sections = objects
        differ = sorted(n for n in set(old_sections) | set(new_sections) if old_sections.get(n) != new_sections.get(n))
        if differ:
            old_text, new_text = object_text(old_sections), object_text(new_sections)
            (args.out / f"{output_name(new_key)}.diff").write_text(
                "".join(difflib.unified_diff(old_text.splitlines(True), new_text.splitlines(True))), encoding="utf-8"
            )
            problems.append(f"{label}: object sections differ: {', '.join(differ)}")
    allowed = load_allowed(ALLOWED)
    unexplained = [problem for problem in problems if explained(problem, allowed) is None]
    for label in checked:
        if not any(problem.startswith(label) for problem in problems):
            print(f"identical objects: {label}")
    for problem in problems:
        reason = explained(problem, allowed)
        print(problem if reason is None else f"allowed: {problem} ({reason})")
    print(
        f"level3: {len(checked)} header-variant TUs compiled, {len(problems) - len(unexplained)} allowed, "
        f"{len(unexplained)} unexplained (diffs in {args.out})",
        file=sys.stderr,
    )
    return 1 if unexplained else 0


# The reference configure options the folded tree takes too: the build type
# and the DKR_* options, except the ones only the release chain read (stage
# and capsule paths, evidence and materialization outputs).
FOLDED_OPTION = re.compile(r"^(CMAKE_BUILD_TYPE|DKR_\w+)$")
CHAIN_ONLY = re.compile(r"^DKR_(MATERIALIZE_\w+|RELEASE_EVIDENCE|RELEASE_CAPSULE|\w+_STAGE)$")


def folded_options(reference: Path) -> tuple[list[str], set[Path]]:
    """The reference's -D options for a folded configure, and the paths
    outside the work tree they name (to mount into the build image)."""
    script = (reference / "configure-linux.sh").read_text(encoding="utf-8")
    command = next(line for line in script.splitlines() if line.startswith("cmake "))
    options: list[str] = []
    capsule: Path | None = None
    for arg in shlex.split(command):
        if not arg.startswith("-D"):
            continue
        name, _, value = arg[2:].partition("=")
        name = name.split(":")[0]
        if name == "DKR_RELEASE_CAPSULE":
            capsule = Path(value)
        if FOLDED_OPTION.match(name) and not CHAIN_ONLY.match(name):
            options.append(arg)
    if capsule is None:
        raise SystemExit("equivalence: the reference configure names no capsule")
    # Camera++ shadow kernels: the chain read them from the capsule role; the
    # folded tree takes them as prepared inputs until fold step B4 generates them.
    for revision in ("77", "80"):
        options.append(f"-DDKR_CAMERA_SHADOW_KERNEL_V{revision}={capsule}/roles/CAMERA_SHADOW/kernel-v{revision}")
    mounts = {
        Path(arg.partition("=")[2])
        for arg in options
        if "=" in arg and arg.partition("=")[2].startswith("/") and not Path(arg.partition("=")[2]).is_relative_to(ROOT)
    }
    return options, mounts


def run_options(args: argparse.Namespace) -> int:
    if args.reference_build:
        print(apply_closure.load_reference(args.reference, ROOT).build)
        return 0
    options, mounts = folded_options(args.reference)
    if args.mounts:
        print("\n".join(sorted({str(p) for p in mounts})))
    else:
        print("\n".join(options))
    return 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=(__doc__ or "").splitlines()[0])
    sub = parser.add_subparsers(dest="command", required=True)

    def compared(name: str, help_text: str) -> argparse.ArgumentParser:
        command = sub.add_parser(name, help=help_text)
        command.add_argument("--reference", type=Path, required=True, help="the reference directory")
        command.add_argument("--folded-build", type=Path, required=True, help="the folded build directory")
        command.add_argument("--verbose", action="store_true", help="also list allowed differences and notes")
        return command

    targets = compared("targets", "compare per-source compile settings and link lines")
    targets.add_argument("--target", action="append", help="limit to these targets")
    level1 = compared("level1", "compare TU sets, header closures and file bytes")
    level1.add_argument("--folded-compdb", type=Path, required=True)
    level1.add_argument("--folded-deps", type=Path, required=True)
    level2 = compared("level2", "compare preprocessed token streams and compile flags")
    level2.add_argument("--folded-compdb", type=Path, required=True)
    level2.add_argument("--reference-pp", type=Path, required=True)
    level2.add_argument("--folded-pp", type=Path, required=True)
    options = sub.add_parser("options", help="print the folded configure options, one per line")
    options.add_argument("--reference", type=Path, required=True)
    options.add_argument("--mounts", action="store_true", help="print the paths outside the tree they name instead")
    options.add_argument("--reference-build", action="store_true", help="print the reference build directory instead")
    level3 = compared("level3", "object equality for the header-variant TUs (in the build image)")
    level3.add_argument("--folded-compdb", type=Path, required=True)
    level3.add_argument("--reference-pp", type=Path, required=True)
    level3.add_argument("--folded-pp", type=Path, required=True)
    level3.add_argument("--out", type=Path, required=True)
    shipped = sub.add_parser("shipped", help="write the shipped compile commands of a build")
    for command in (shipped,):
        command.add_argument("--build", type=Path, required=True)
        command.add_argument("--out", type=Path, required=True)
    for name, help_text in (
        ("depscan", "scan header dependencies (in the build image)"),
        ("preprocess", "preprocess first-party TUs (in the build image)"),
    ):
        command = sub.add_parser(name, help=help_text)
        command.add_argument("--compdb", type=Path, required=True)
        command.add_argument("--build", type=Path, required=True)
        command.add_argument("--out", type=Path, required=True)
        command.add_argument("--jobs", type=int, default=os.cpu_count() or 4)
    args = parser.parse_args(argv)
    for key in ("reference", "folded_build", "build"):
        if getattr(args, key, None) is not None:
            setattr(args, key, getattr(args, key).resolve())
    handlers = {
        "targets": run_targets,
        "level1": run_level1,
        "level2": run_level2,
        "level3": run_level3,
        "shipped": write_shipped,
        "options": run_options,
        "depscan": run_depscan,
        "preprocess": run_preprocess,
    }
    return handlers[args.command](args)


if __name__ == "__main__":
    sys.exit(main())
