#!/usr/bin/env python3
"""Create and validate a deterministic source ZIP from the current source tree."""
from __future__ import annotations

import argparse
import pathlib
import os
import tempfile
import subprocess
import sys
import zipfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
RELEASE_VERSION = (ROOT / "VERSION").read_text(encoding="utf-8").strip()
DEFAULT_OUTPUT = ROOT / "dist" / f"DKR-R-{RELEASE_VERSION}-Source.zip"
DENIED_SUFFIXES = {
    ".z64",
    ".v64",
    ".n64",
    ".o2r",
    ".otr",
    ".eep",
    ".mpk",
    ".sra",
    ".fla",
}
N64_HEADERS = {b"\x80\x37\x12\x40", b"\x37\x80\x40\x12", b"\x40\x12\x37\x80"}
SOURCE_PREFIX = "DKR-R/"


def git_paths(*arguments: str) -> list[pathlib.Path]:
    result = subprocess.run(
        ["git", *arguments, "-z"], cwd=ROOT, check=True,
        capture_output=True,
    ).stdout
    return [pathlib.Path(item.decode("utf-8"))
            for item in result.split(b"\0") if item]


def source_files() -> list[pathlib.Path]:
    """Return tracked files plus narrowly-scoped, new source files.

    Local release builds may be packaged before their final commit, so
    git-archive would silently omit new source and documentation files.
    Untracked artwork, local downloads and generated output remain excluded.
    """
    tracked = git_paths("ls-files")
    untracked = git_paths("ls-files", "--others", "--exclude-standard")
    allowed_untracked_roots = (
        pathlib.PurePosixPath(".github"),
        pathlib.PurePosixPath("assets/filters"),
        pathlib.PurePosixPath("assets/controllers"),
        pathlib.PurePosixPath("assets/ui/Icons"),
        pathlib.PurePosixPath("assets/ui/Backgrounds"),
        pathlib.PurePosixPath("assets/ui/CustomTrackBackgrounds"),
        pathlib.PurePosixPath("docs"),
        pathlib.PurePosixPath("regression"),
        pathlib.PurePosixPath("packaging"),
        pathlib.PurePosixPath("runtime-recomp/cmake"),
        pathlib.PurePosixPath("runtime-recomp/src"),
        pathlib.PurePosixPath("runtime-recomp/include"),
        pathlib.PurePosixPath("tools/blender"),
        pathlib.PurePosixPath("runtime-recomp/tests"),
        pathlib.PurePosixPath("patches"),
        pathlib.PurePosixPath("modding"),
        pathlib.PurePosixPath("scripts"),
    )
    allowed_untracked_suffixes = {
        ".c", ".cc", ".cpp", ".h", ".hpp", ".json", ".md", ".png",
        ".toml", ".hlsl", ".hlsli", ".in", ".inc", ".inl", ".ld", ".ps1", ".py", ".sh", ".txt", ".patch", ".yaml", ".yml", ".cmake",
        ".java", ".kt", ".xml", ".gradle",
    }
    selected = set(tracked)
    allowed_exact_untracked = {
        pathlib.PurePosixPath("runtime-recomp/tests/live_lod_probe.hlsl"),
        pathlib.PurePosixPath("runtime-recomp/tests/generated_mip_readback.hlsl"),
        pathlib.PurePosixPath("runtime-recomp/tests/generated_mip_sampling.hlsl"),
        pathlib.PurePosixPath("RELEASE-VALIDATION.md"),
        pathlib.PurePosixPath("VERSION"),
        pathlib.PurePosixPath("runtime-recomp/dkr.us.v80.recomp-policy.json"),
        pathlib.PurePosixPath("runtime-recomp/legacy-mods.v77.recomp-fragment.json"),
        pathlib.PurePosixPath("runtime-recomp/legacy-mods.v80.recomp-fragment.json"),
        pathlib.PurePosixPath("assets/ui/Icons/DKR-R-Logo.bmp"),
        pathlib.PurePosixPath("assets/ui/Icons/DKR-R-Icon.png"),
        pathlib.PurePosixPath("assets/ui/Icons/DKR-R-Spinning-Icon.png"),
        pathlib.PurePosixPath("assets/ui/Icons/DKR-R-Short-Logo.png"),
    }
    for path in untracked:
        pure = pathlib.PurePosixPath(path.as_posix())
        if (
            pure in allowed_exact_untracked
            or (
                path.suffix.lower() in allowed_untracked_suffixes
                and any(pure.is_relative_to(root)
                        for root in allowed_untracked_roots)
            )
        ):
            selected.add(path)
        elif path.suffix.lower() in allowed_untracked_suffixes:
            print(f"Excluded untracked possible source file (outside supported roots): {path}", file=sys.stderr)
    return sorted(path for path in selected if (ROOT / path).is_file())


def create_archive(output: pathlib.Path) -> None:
    files = source_files()
    if not files:
        raise RuntimeError("No source files were selected")
    with zipfile.ZipFile(output, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for relative in files:
            source = ROOT / relative
            info = zipfile.ZipInfo(
                SOURCE_PREFIX + relative.as_posix(),
                date_time=(1980, 1, 1, 0, 0, 0),
            )
            info.create_system = 3
            info.compress_type = zipfile.ZIP_DEFLATED
            executable = relative.suffix.lower() == ".sh"
            info.external_attr = ((0o100755 if executable else 0o100644) << 16)
            archive.writestr(info, source.read_bytes())


def validate_archive(path: pathlib.Path) -> None:
    inspected = 0
    with zipfile.ZipFile(path, "r") as archive:
        for entry in archive.infolist():
            if entry.is_dir():
                continue
            inspected += 1
            member = pathlib.PurePosixPath(entry.filename)
            if member.is_absolute() or ".." in member.parts:
                raise RuntimeError(f"Unsafe source archive path: {entry.filename}")
            if member.suffix.lower() in DENIED_SUFFIXES:
                raise RuntimeError(f"Prohibited game-data entry: {entry.filename}")
            with archive.open(entry, "r") as stream:
                if stream.read(4) in N64_HEADERS:
                    raise RuntimeError(f"N64 ROM header in source archive: {entry.filename}")
    if inspected == 0:
        raise RuntimeError("Source archive is empty")
    print(f"Source archive scan passed: {inspected} files inspected")


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", nargs="?", default=str(DEFAULT_OUTPUT))
    args = parser.parse_args(argv)
    output = pathlib.Path(args.output).resolve()
    if output.exists():
        print(f"Refusing to overwrite existing source archive: {output}", file=sys.stderr)
        return 2
    output.parent.mkdir(parents=True, exist_ok=True)

    temporary = None
    try:
        with tempfile.NamedTemporaryFile(dir=output.parent, prefix=".source-", suffix=".zip", delete=False) as file:
            temporary = pathlib.Path(file.name)
        create_archive(temporary)
        validate_archive(temporary)
        # A hard link publishes the complete archive atomically and fails if
        # another invocation has claimed the destination since our preflight.
        temporary.chmod(0o644)
        os.link(temporary, output)
    except (OSError, RuntimeError, subprocess.CalledProcessError, zipfile.BadZipFile) as error:
        print(f"Source packaging failed: {error}", file=sys.stderr)
        return 2
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)
    print(output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
