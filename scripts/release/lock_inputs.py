#!/usr/bin/env python3
"""Pin the ROM-derived build inputs a release compiles.

    lock_inputs.py write [--prepared DIR]   record build/prepared in the lock
    lock_inputs.py check [--prepared DIR]   compare build/prepared with it

runtime-recomp/build-inputs.lock.json records, for the current N64Recomp and
decomp pins, one digest per prepared input directory (scripts/prepare_payloads.py
output: the generated payloads, owned engines, model kernels and Camera++
kernels) and for the two N64Recomp configs (machine paths masked). A
directory's digest covers its compiled files (text ones with line endings
normalised; Windows writes some CRLF) and the manifest fields the configure
reads; the rest of the JSON and TOML provenance names machine paths and is
left out. Release builds verify the same digests at configure
(cmake/BuildInputs.cmake, DKR_VERIFY_BUILD_INPUTS), so a release can't compile inputs nobody pinned.

Exit status: 0 written or matching, 1 a mismatch, a missing input or stale
pins.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
from typing import cast

sys.dont_write_bytecode = True
from release_common import ROOT  # noqa: E402

LOCK = ROOT / "runtime-recomp/build-inputs.lock.json"
INPUTS = tuple(f"{kind}-v{rev}" for kind in ("generated", "owned", "model", "camera-kernel") for rev in (77, 80))
# The N64Recomp configs (v<rev>.toml beside the payloads): the code-mod
# protection staging reads their hooks. Their three machine paths are masked.
POLICIES = tuple(f"code-policy-v{rev}" for rev in (77, 80))
POLICY_PATHS = re.compile(r"^(elf_path|rom_file_path|output_func_path) = \"[^\n]*\"", re.M)
# Hashed with CRLF read as LF (Windows writes some of them CRLF); anything
# else is hashed as it is.
TEXT = (".c", ".cc", ".cpp", ".h", ".hpp", ".inl", ".inc")
# Provenance beside the compiled files names machine paths, so the files are
# left out whole. What the configure reads from them is locked instead:
# these manifest.json fields (cmake/ExperimentalOwnedGame.cmake,
# LocalModelDetail.cmake, CMakeLists.txt); "#name" is an array's length.
PROVENANCE = (".json", ".toml")
CONFIGURE_FIELDS = {
    "owned": (
        "revision",
        "symbol_prefix",
        "full_scenes",
        "local_model_observations",
        "online_view_observations",
        "local_world_observations",
        "owned_mod_services",
        "scene_cuts",
        "audio_services",
        "input_services",
        "authored_cpu",
        "audio_symbol_isolation",
        "runtime_symbol_isolation",
        "code_dispatch_schema",
        "#code_protected_policy",
    ),
    "model": ("schema", "revision"),
    "generated": (),
    "camera-kernel": (),
}


def kind(name: str) -> str:
    return name.rsplit("-v", 1)[0]


def field_value(manifest: dict[str, object], field: str) -> str:
    """A manifest field as CMake's string(JSON ...) yields it."""
    key = field.lstrip("#")
    if key not in manifest:
        return "<missing>"
    value = manifest[key]
    if field.startswith("#"):
        return str(len(cast("list[object]", value)))
    if isinstance(value, bool):
        return "ON" if value else "OFF"
    return str(value)


def digest(directory: Path, fields: tuple[str, ...] = ()) -> str:
    """sha256 over "relative-path sha256\n" of every compiled file, sorted,
    then "manifest.json:field value\n" for each configure field. Text files
    (TEXT) read CRLF as LF. cmake/BuildInputs.cmake computes the same."""
    lines: list[str] = []
    for path in sorted(directory.rglob("*"), key=lambda p: p.relative_to(directory).as_posix()):
        if not path.is_file() or path.suffix in PROVENANCE:
            continue
        data = path.read_bytes()
        if path.suffix in TEXT:
            data = data.replace(b"\r\n", b"\n")
        lines.append(f"{path.relative_to(directory).as_posix()} {hashlib.sha256(data).hexdigest()}\n")
    if fields:
        manifest = cast("dict[str, object]", json.loads((directory / "manifest.json").read_text(encoding="utf-8")))
        lines += [f"manifest.json:{field} {field_value(manifest, field)}\n" for field in fields]
    return hashlib.sha256("".join(lines).encode()).hexdigest()


def policy_digest(path: Path) -> str:
    """sha256 of an N64Recomp config, CRLF as LF, machine paths masked."""
    text = path.read_bytes().replace(b"\r\n", b"\n").decode("utf-8")
    return hashlib.sha256(POLICY_PATHS.sub(r"\1", text).encode("utf-8")).hexdigest()


def pins() -> dict[str, str]:
    """The pins the prepared inputs depend on: the N64Recomp and decomp commits."""

    def head(path: str) -> str:
        done = subprocess.run(["git", "-C", str(ROOT / path), "rev-parse", "HEAD"], capture_output=True, text=True)
        return done.stdout.strip() if done.returncode == 0 else "unavailable"

    return {"n64recomp": head("extern/n64-modern-runtime/N64Recomp"), "dkr-decomp": head("extern/dkr-decomp")}


def current(prepared: Path) -> dict[str, str]:
    missing = [name for name in INPUTS if not (prepared / name).is_dir()]
    if missing:
        raise SystemExit("lock_inputs: missing prepared inputs: " + ", ".join(missing))
    found = {name: digest(prepared / name, CONFIGURE_FIELDS[kind(name)]) for name in INPUTS}
    for name in POLICIES:
        path = prepared / (name.removeprefix("code-policy-") + ".toml")
        if not path.is_file():
            raise SystemExit(f"lock_inputs: missing prepared input {path.name}")
        found[name] = policy_digest(path)
    return found


def main(argv: list[str] | None = None) -> int:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("action", choices=("write", "check"))
    p.add_argument("--prepared", type=Path, default=ROOT / "build/prepared")
    a = p.parse_args(argv)
    found = current(cast(Path, a.prepared).resolve())
    if a.action == "write":
        record = {
            "schema": 1,
            "about": "Digests of the prepared build inputs (scripts/release/lock_inputs.py); "
            "cmake/BuildInputs.cmake verifies them when DKR_VERIFY_BUILD_INPUTS is on.",
            "pins": pins(),
            "inputs": found,
        }
        LOCK.write_text(json.dumps(record, indent=1) + "\n", encoding="utf-8")
        print(f"lock_inputs: {len(found)} inputs recorded")
        return 0
    lock = json.loads(LOCK.read_text(encoding="utf-8"))
    problems = [
        f"{name}: {found[name]}, locked {want}" for name, want in lock["inputs"].items() if found.get(name) != want
    ]
    if lock["pins"] != pins():
        problems.append(f"pins changed since the lock was written: {lock['pins']} -> {pins()}")
    for problem in problems:
        print(f"lock_inputs: {problem}", file=sys.stderr)
    print(f"lock_inputs: {len(INPUTS)} inputs checked, {len(problems)} problems")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
