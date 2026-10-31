"""The prepared-inputs level of the fold gate (equivalence.py prepared).

scripts/prepare_payloads.py, run from the folded tree with no capsule and no
release chain, must reproduce the ROM-derived inputs the v1.0.11 reference
was configured with (the paths its configure-linux.sh names). Compiled files
compare byte for byte. The exceptions are named here, one per kind:

* line endings of the Camera++ kernel files: the reference took them from the
  release capsule, which stored them CRLF;
* the order of visual_model_addresses.h's lines: the shipped generator
  iterated a set (it sorts now);
* the three machine paths in the code policy (elf_path, rom_file_path,
  output_func_path), and the "file" paths in the owned manifest;
* provenance in the JSON records that names a pre-fold source rather than
  the folded tree: the portrait receipt's native stage digest, the owned
  manifest's digests of the native sources it audits (same files, folded
  bytes), and the kernel manifest's recipe, file and bridge digests
  (recomputed instead: files must match its own directory, and our bridge
  digests the bridge sources committed in runtime-recomp/src/game).

Everything CMake reads from a manifest (revision, full_scenes, the
observation schemas) is outside these exceptions and compares exactly.
"""

from __future__ import annotations

from collections.abc import Iterator
import hashlib
import json
from pathlib import Path
import re
from typing import cast

# The Camera++ kernel's native bridge, whose digests a kernel manifest records.
BRIDGE = Path(__file__).resolve().parents[2] / "runtime-recomp/src/game"
# Reference configure option -> prepared path, per revision.
INPUTS = {
    "DKR_GENERATED_SOURCE_V{rev}": "generated-v{rev}",
    "DKR_CODE_POLICY_V{rev}": "v{rev}.toml",
    "DKR_EXPERIMENTAL_OWNED_GENERATED{suffix}": "owned-v{rev}",
    "DKR_VISUAL_MODEL_GENERATED_V{rev}": "model-v{rev}",
    "DKR_CAMERA_KERNEL_V{rev}": "camera-kernel-v{rev}",
}
POLICY_PATHS = re.compile(r'^(elf_path|rom_file_path|output_func_path) = ".*"$', re.M)
# Owned-manifest keys whose values are digests of first-party native sources.
NATIVE_AUDITS = ("full_scene_native_inputs", "input_native_inputs")
Json = dict[str, object]


def expected_pairs(options: dict[str, str]) -> Iterator[tuple[str, Path, str]]:
    """(option, reference path, prepared name) for every prepared input."""
    for rev in (77, 80):
        for option, name in INPUTS.items():
            key = option.format(rev=rev, suffix="" if rev == 77 else "_V80")
            if key not in options:
                raise ValueError(f"the reference configure names no {key}")
            yield key, Path(options[key]), name.format(rev=rev)


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def load(path: Path) -> Json:
    return cast(Json, json.loads(path.read_text(encoding="utf-8")))


def json_diff(ours: object, theirs: object, path: str = "") -> Iterator[str]:
    if isinstance(ours, dict) and isinstance(theirs, dict):
        a, b = cast(Json, ours), cast(Json, theirs)
        for key in sorted(set(a) | set(b)):
            yield from json_diff(a.get(key), b.get(key), f"{path}.{key}")
    elif isinstance(ours, list) and isinstance(theirs, list):
        a_list, b_list = cast(list[object], ours), cast(list[object], theirs)
        if len(a_list) != len(b_list):
            yield path or "."
            return
        for i, (x, y) in enumerate(zip(a_list, b_list)):
            yield from json_diff(x, y, f"{path}[{i}]")
    elif ours != theirs:
        yield path or "."


def masked_owned(manifest: Json) -> Json:
    """The owned manifest without its machine paths and native digests (the
    keys of the native audits stay, so the audited file set still compares)."""

    def walk(value: object) -> object:
        if isinstance(value, dict):
            return {k: walk(v) for k, v in cast(Json, value).items() if k != "file"}
        if isinstance(value, list):
            return [walk(v) for v in cast(list[object], value)]
        return value

    masked = cast(Json, walk(manifest))
    for key in NATIVE_AUDITS:
        if key in masked:
            masked[key] = sorted(cast(Json, masked[key]))
    portrait = masked.get("character_portrait_pipeline")
    if isinstance(portrait, dict):
        masked["character_portrait_pipeline"] = {
            k: v for k, v in cast(Json, portrait).items() if k not in ("native_sha256", "native_files")
        }
    return masked


def masked_receipt(receipt: Json) -> Json:
    return {k: v for k, v in receipt.items() if k not in ("native_sha256", "native_files")}


def kernel_manifest(directory: Path, bridge: Path | None = None) -> Json:
    """A kernel manifest without recipe, files and bridge, once files is
    checked against the directory it describes and, given the bridge
    directory, bridge against the bridge sources committed there."""
    manifest = load(directory / "manifest.json")
    files = {p.name: sha(p.read_bytes()) for p in directory.iterdir() if p.is_file() and p.name != "manifest.json"}
    if manifest.get("files") != files:
        raise ValueError(f"{directory}/manifest.json: files do not describe the directory")
    if bridge is not None:
        recorded = cast(dict[str, str], manifest.get("bridge") or {})
        current = {name: sha((bridge / name).read_bytes()) for name in recorded}
        if not recorded or recorded != current:
            raise ValueError(f"{directory}/manifest.json: bridge does not match the committed {bridge}")
    return {k: v for k, v in manifest.items() if k not in ("recipe", "files", "bridge")}


def compare_file(ours: Path, theirs: Path, label: str) -> str | None:
    """None when equal, else the reason. Names the allowed kind in the
    reason it returns when only an allowed difference remains."""
    a, b = ours.read_bytes(), theirs.read_bytes()
    if a == b:
        return None
    name = ours.name
    if label.startswith("camera-kernel") and name != "manifest.json":
        return "line endings" if a.replace(b"\r\n", b"\n") == b.replace(b"\r\n", b"\n") else "bytes"
    if name == "visual_model_addresses.h":
        return "line order" if sorted(a.splitlines()) == sorted(b.splitlines()) else "bytes"
    if name.endswith(".toml"):
        same = POLICY_PATHS.sub(r"\1", a.decode()) == POLICY_PATHS.sub(r"\1", b.decode())
        return "machine paths" if same else "bytes"
    if name == "character-portrait-receipt.json":
        same = masked_receipt(load(ours)) == masked_receipt(load(theirs))
        return "provenance" if same else "fields " + ", ".join(json_diff(load(ours), load(theirs)))
    if name == "manifest.json" and label.startswith("owned"):
        a_json, b_json = masked_owned(load(ours)), masked_owned(load(theirs))
        return "provenance" if a_json == b_json else "fields " + ", ".join(list(json_diff(a_json, b_json))[:8])
    if name == "manifest.json" and label.startswith("camera-kernel"):
        try:
            a_json, b_json = kernel_manifest(ours.parent, BRIDGE), kernel_manifest(theirs.parent)
        except ValueError as error:
            return str(error)
        return "provenance" if a_json == b_json else "fields " + ", ".join(json_diff(a_json, b_json))
    return "bytes"


ALLOWED = {"line endings", "line order", "machine paths", "provenance"}


def compare(options: dict[str, str], prepared: Path) -> tuple[dict[str, int], list[str], list[str]]:
    """(counts, unexplained, allowed) for the prepared inputs against the
    reference's."""
    counts = {"identical": 0, "allowed": 0, "unexplained": 0}
    unexplained: list[str] = []
    allowed: list[str] = []
    for option, theirs, name in expected_pairs(options):
        ours = prepared / name
        if theirs.is_file():
            pairs = [(ours, theirs, name)]
        else:
            names = {p.relative_to(theirs).as_posix() for p in theirs.rglob("*") if p.is_file()}
            names |= (
                {p.relative_to(ours).as_posix() for p in ours.rglob("*") if p.is_file()} if ours.is_dir() else set()
            )
            pairs = [(ours / n, theirs / n, f"{name}/{n}") for n in sorted(names)]
        for a, b, label in pairs:
            if not a.is_file() or not b.is_file():
                reason = "missing from " + ("the prepared inputs" if not a.is_file() else f"{option}")
            else:
                reason = compare_file(a, b, label)
            if reason is None:
                counts["identical"] += 1
            elif reason in ALLOWED:
                counts["allowed"] += 1
                allowed.append(f"{label}: {reason}")
            else:
                counts["unexplained"] += 1
                unexplained.append(f"{label}: {reason}")
    return counts, unexplained, allowed
