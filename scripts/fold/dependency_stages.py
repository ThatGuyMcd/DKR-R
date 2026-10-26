#!/usr/bin/env python3
"""Stage patches for the dependency transforms (fold step B3).

v1.0.11 changed eleven third-party files (RT64, ultramodern) through release
layers: a pipeline rewrote the file the configure-time stage chain had already
produced and swapped its copy into the target. The fold keeps those files as
ordinary staged patches, applied by ``dkr_stage_patch`` after the chain:

* STAGES names each patch, the chain output it applies to and the reference
  files it reproduces;
* ``--write`` diffs each chain output against the shipped bytes and writes the
  patch; ``--check`` regenerates the patches and fails if a committed one
  differs or if a staged copy in the configured build is not the shipped
  file (includes respelled).

Both read a configured folded build (the chain output, at the input stage) and
the materialized v1.0.11 reference (docs/fold-in/REFERENCE.md). The shipped
files named staged headers by absolute path on the configuring machine; those
directives are respelled to the name the staged copy reaches through its own
directory or the src/game include path, which the equivalence gate's level 1
(header closure) and level 2 (preprocessed tokens) then verify.

The equivalence gate reads STAGES to pair each reference file with its folded
staged copy, ``generated/<stage>/<file>``.

    dependency_stages.py --reference DIR [--build DIR] --write
    dependency_stages.py --reference DIR [--build DIR] --check

Exit status: 0 on success, 1 when a patch is stale or a directive cannot be
respelled, 2 on usage errors.
"""

from __future__ import annotations

import argparse
import dataclasses
from pathlib import Path
import re
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts" / "fold"))
import apply_closure  # noqa: E402


@dataclasses.dataclass(frozen=True)
class Stage:
    patch: str  # repo-relative patch path
    name: str  # dkr_stage_patch stage name: generated/<name>/
    source: str  # chain output the patch applies to: generated/<stage> or extern/<dep>
    files: dict[str, str]  # path inside the stage -> reference original_path (sources)
    headers: dict[str, str] = dataclasses.field(default_factory=dict)  # same, staged headers


STAGES = (
    # Liquid family gate (render-stability), with the liquid-streaming
    # generated-mips helpers rt64_texture_cache.cpp is their only reader of.
    Stage(
        "patches/rt64/staged/0001-render-stability.patch",
        "rt64-render-stability",
        "generated/hle-command-guard",
        {
            "src/render/rt64_texture_cache.cpp": "rt64/src/render/rt64_texture_cache.cpp",
            "src/render/rt64_framebuffer_renderer.cpp": "rt64/src/render/rt64_framebuffer_renderer.cpp",
            "src/hle/rt64_state.cpp": "rt64/src/hle/rt64_state.cpp",
            "src/hle/rt64_workload_queue.cpp": "rt64/src/hle/rt64_workload_queue.cpp",
        },
        {"src/render/rt64_generated_mips.h": "rt64/src/render/rt64_generated_mips.h"},
    ),
    # Wheel interpolation (custom-vehicles).
    Stage(
        "patches/rt64/staged/0002-vehicle-transform-processor.patch",
        "rt64-vehicle-transform",
        "generated/hle-command-guard",
        {"src/render/rt64_transform_processor.cpp": "rt64/src/render/rt64_transform_processor.cpp"},
    ),
    # Present-queue pacer (surgical-performance).
    Stage(
        "patches/rt64/staged/0003-present-queue-pacer.patch",
        "rt64-present-pacer",
        "generated/hle-command-guard",
        {"src/hle/rt64_present_queue.cpp": "rt64/src/hle/rt64_present_queue.cpp"},
    ),
    # Texture-capture observer (texture-dumping).
    Stage(
        "patches/rt64/staged/0004-texture-capture-observer.patch",
        "rt64-texture-capture",
        "generated/hle-command-guard",
        {"src/hle/rt64_rdp_tmem.cpp": "rt64/src/hle/rt64_rdp_tmem.cpp"},
    ),
    # Review editor text widgets (review-editor).
    Stage(
        "patches/rt64/staged/0005-review-editor-imgui.patch",
        "rt64-review-editor",
        "extern/rt64",
        {"src/contrib/imgui/imgui_widgets.cpp": "rt64/src/contrib/imgui/imgui_widgets.cpp"},
        {"src/contrib/imgui/imstb_textedit.h": "rt64/src/contrib/imgui/imstb_textedit.h"},
    ),
    # Producer snapshot handoff (graphics-handoff).
    Stage(
        "patches/n64-modern-runtime/staged/0001-graphics-handoff-events.patch",
        "ultramodern-graphics-handoff",
        "generated/vi-bootstrap",
        {"ultramodern/src/events.cpp": "n64-modern-runtime/ultramodern/src/events.cpp"},
    ),
)


def entries(stage: Stage) -> dict[str, str]:
    return {**stage.files, **stage.headers}


def folded_paths(build: Path) -> dict[str, Path]:
    """Reference original_path -> the folded staged copy."""
    return {
        original: build / "generated" / stage.name / relative
        for stage in STAGES
        for relative, original in entries(stage).items()
    }


def reference_rows(reference: apply_closure.Reference) -> dict[str, apply_closure.Row]:
    rows = {row.original: row for row in reference.rows if row.kind == "dependency-transform"}
    covered = {original for stage in STAGES for original in entries(stage).values()}
    if set(rows) != covered:
        raise SystemExit(
            f"dependency_stages: STAGES covers {sorted(covered ^ set(rows))} differently from the reference"
        )
    return rows


def input_path(stage: Stage, build: Path) -> Path:
    base = ROOT if stage.source.startswith("extern/") else build
    return base / stage.source


def respelled(
    row: apply_closure.Row,
    reference: apply_closure.Reference,
    folder: apply_closure.Folder,
    by_staged: dict[Path, str],
    stage: Stage,
    relative: str,
    chain: bytes,
) -> tuple[bytes, list[str]]:
    """Shipped bytes with every path-like include respelled for the folded stage.

    A directive that names a path (absolute on the configuring machine, or
    ``../`` from a staged directory) is resolved the way a reference TU
    resolved it and respelled to reach the folded counterpart: a header staged
    alongside, the chain output's own header, or first-party code through the
    src/game include path. Bare names, and spellings the chain output already
    uses (its source directory stays on the staged copy's include path), are
    left alone.
    """
    data = row.staged.read_bytes()
    problems: list[str] = []
    parent = Path(relative).parent
    resolver = apply_closure.Resolver()
    searches = [search for tu, search in reference.compdb.items() if row.staged in reference.deps.get(tu, ())]
    if row.staged in reference.compdb:
        searches.insert(0, reference.compdb[row.staged])
    kept = {spelling for _, _, spelling, _ in apply_closure.directives(chain)}
    pieces: list[bytes] = []
    cursor = 0
    for line, quoted, spelling, (start, end) in apply_closure.directives(data):
        if "/" not in spelling or spelling in kept:
            continue
        resolved = {resolver(spelling, quoted, row.staged.parent, search) for search in searches} - {None}
        if len(resolved) != 1:
            problems.append(f"{stage.patch}: {relative}:{line}: {spelling} resolves to {sorted(map(str, resolved))}")
            continue
        (target,) = resolved
        if not spelling.startswith(("/", "../")) and folder.file(target) is not None:
            continue  # a dependency-relative path (common/rt64_thread.h) the chain keeps
        new: str | None = None
        if target in by_staged and Path(by_staged[target]).parent == parent:
            new = Path(by_staged[target]).name  # a header staged next to this file
        elif target.is_relative_to(reference.build / "generated" / Path(stage.source).name):
            inner = target.relative_to(reference.build / "generated" / Path(stage.source).name)
            if inner.parent == parent:
                new = inner.name  # the chain output's own header, via the source's include path
        else:
            folded = folder.file(target)
            game = ROOT / "runtime-recomp" / "src" / "game"
            if folded is not None and folded.is_relative_to(game):
                new = folded.relative_to(game).as_posix()  # first-party, via src/game
        if new is None or not quoted:
            problems.append(f"{stage.patch}: {relative}:{line}: cannot respell {spelling}")
            continue
        if new == spelling:
            continue
        pieces += [data[cursor:start], new.encode()]
        cursor = end
    pieces.append(data[cursor:])
    return b"".join(pieces), problems


def diff(stage: Stage, contents: dict[str, tuple[bytes, bytes]]) -> bytes:
    """A git-apply patch from each file's chain bytes to its shipped bytes."""
    with tempfile.TemporaryDirectory() as scratch:
        work = Path(scratch)
        for relative, (old, new) in contents.items():
            for side, data in (("a", old), ("b", new)):
                path = work / side / relative
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(data)
        done = subprocess.run(
            ["git", "diff", "--no-index", "--no-prefix", "--no-color", "--full-index", "a", "b"],
            cwd=work,
            capture_output=True,
        )
    if done.returncode not in (0, 1):
        raise SystemExit(done.stderr.decode())
    # The side directories are named a/ and b/, so --no-prefix yields the usual
    # a/<path> b/<path> headers. Blob ids say nothing about the chain input.
    return re.sub(rb"(?m)^index [0-9a-f]+\.\.[0-9a-f]+( \d+)?\n", b"", done.stdout)


def build_patches(reference_dir: Path, build: Path) -> tuple[dict[str, bytes], dict[Path, bytes], list[str]]:
    """Each patch's bytes, each staged copy's expected bytes, and problems."""
    reference = apply_closure.load_reference(reference_dir, ROOT)
    rows = reference_rows(reference)
    folder = apply_closure.Folder(reference, ROOT, apply_closure.plan(reference, ROOT).destinations)
    by_staged = {rows[original].staged: relative for stage in STAGES for relative, original in entries(stage).items()}
    patches: dict[str, bytes] = {}
    expected: dict[Path, bytes] = {}
    problems: list[str] = []
    for stage in STAGES:
        contents: dict[str, tuple[bytes, bytes]] = {}
        for relative, original in entries(stage).items():
            chain = input_path(stage, build) / relative
            if not chain.is_file():
                problems.append(f"{stage.patch}: chain output {chain} is missing (configure the folded build first)")
                continue
            old = chain.read_bytes()
            shipped, issues = respelled(rows[original], reference, folder, by_staged, stage, relative, old)
            problems += issues
            contents[relative] = (old, shipped)
            expected[build / "generated" / stage.name / relative] = shipped
        patches[stage.patch] = diff(stage, contents)
    return patches, expected, problems


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--reference", type=Path, required=True)
    parser.add_argument("--build", type=Path, default=ROOT / "build" / "fold-equivalence")
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--write", action="store_true")
    mode.add_argument("--check", action="store_true")
    args = parser.parse_args(argv)
    patches, expected, problems = build_patches(args.reference.resolve(), args.build.resolve())
    for path, data in patches.items():
        current = ROOT / path
        if args.write and not problems:
            current.parent.mkdir(parents=True, exist_ok=True)
            current.write_bytes(data)
        elif args.check and (not current.is_file() or current.read_bytes() != data):
            problems.append(f"{path}: stale; rerun with --write")
    if args.check:
        # What the configure staged must be the shipped bytes (includes respelled).
        for staged, data in expected.items():
            if not staged.is_file() or staged.read_bytes() != data:
                problems.append(f"{staged}: staged copy differs from the shipped file")
    for problem in problems:
        print(problem, file=sys.stderr)
    if not problems:
        print(f"dependency_stages: {len(patches)} patches {'written' if args.write else 'current'}")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
