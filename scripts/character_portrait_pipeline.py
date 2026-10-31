"""Portrait correction stage: read-only pinned inputs, NEW outputs only.

The guest stage hooks the in-world character flag so custom participants get
their portrait, and protects the function from code-mod replacement in the
code policy. A supported prepare step (scripts/prepare_payloads.py runs it).

The native half of the original stage edited seven first-party sources in a
staged copy of runtime-recomp/src; the fold (step B1) wrote those edits into
runtime-recomp/src itself. The receipt therefore records those seven files'
folded digests, and the owned payload checks them, instead of a native stage.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import shutil
from typing import Any, cast

from save_layout_pipeline import DEF

ROOT = Path(__file__).resolve().parents[1]
WORLD_PORTRAIT = ROOT / "runtime-recomp/guest/world_portrait.inc"
MODS = ROOT / "runtime-recomp/src/game/mods"
# The seven native portrait edits as v1.0.11 shipped them (runtime-recomp/src).
NATIVE_FILES = {
    "legacy_mod_format.cpp": "b3cdd9d7bcab6f54355592f90eed0e5c9ccf98e92d331746a8e186b06982cf4d",
    "legacy_character_materialize.hpp": "7fa418bb9fd4da9b79eab21dd98876f2b05b4496330b809e17c9359bf4eef4fb",
    "legacy_character_materialize.cpp": "2270d3efce0dcfcef2f5364e946fe70a5d5401f11b1960917ba5449678423563",
    "legacy_track_catalog.cpp": "ad6d37cb7196738dfe9d2a1c07af28e94e69040ebc9909ffbed30aae7055796f",
    "legacy_track_prepare_job.hpp": "810b3cfa3c62e43c557f9d3c5d134888b7b9e3b9ad4ac7643badfd3c1e75d14c",
    "legacy_track_prepare_job.cpp": "d1a0f6a906c261f9d8104d167da5ebc3fcf1aef686ec6142efd39c0da3f5d82d",
    "online_mod_prepare.cpp": "e0ec53916dfd0f75582835f2198b2934164b9e5080d3766e7a2423c24063c9bb",
}
GUEST_PINS = {
    77: "006cc572caf0097392aeccac974dc4574813eeea81c09b6eabc5b1b7a9199842",
    80: "9fc26cc8bb1762db7a78f5979cea65835ccd7c84e6ed325d411ad149f98df070",
}
ADDRESSES = {77: "80035FE4", 80: "80036024"}


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def new_output(path: Path, *inputs: Path) -> Path:
    path = path.resolve()
    if path.exists() or any(path == p.resolve() or p.resolve() in path.parents for p in inputs):
        raise ValueError("Portrait pipeline output must be NEW and outside every read-only input")
    if any(part.lower() in ("extern", "recompiledfuncs", "recompiledpatches") for part in path.parts):
        raise ValueError("Portrait output must not enter a protected dependency/generated tree")
    return path


def replace(text: str, old: str, new: str, count: int = 1) -> str:
    if text.count(old) != count:
        raise ValueError("Unreviewed portrait edit site: " + old[:100])
    return text.replace(old, new)


def verify_native_files() -> None:
    """The native half of the portrait fix is present in runtime-recomp/src."""
    for name, pin in NATIVE_FILES.items():
        path = MODS / name
        if path.is_symlink() or sha(path.read_bytes()) != pin:
            raise ValueError("Native portrait source changed: " + name)


def guest(source: Path, output: Path, revision: int, config: Path, output_config: Path) -> None:
    verify_native_files()
    source = source.resolve()
    output = new_output(output, source)
    output_config = new_output(output_config, source, config)
    for path in source.rglob("*"):
        if path.is_symlink():
            raise ValueError("Guest portrait input cannot contain links")
    edits: dict[str, str] = {}
    found: dict[str, str] | None = None
    for file in source.glob("*.c"):
        text = file.read_text()
        matches = list(DEF.finditer(text))
        for i, m in enumerate(matches):
            if m[1] != "obj_loop_characterflag":
                continue
            if found:
                raise ValueError("Duplicate world portrait function")
            end = matches[i + 1].start() if i + 1 < len(matches) else len(text)
            body = text[m.start() : end]
            if sha(body.encode()) != GUEST_PINS[revision]:
                raise ValueError("Unreviewed CharacterFlag guest instructions")
            replacement = replace(
                body,
                "    MEM_W(0X24, ctx->r3) = ctx->r11;",
                WORLD_PORTRAIT.read_text() + "\n    MEM_W(0X24, ctx->r3) = ctx->r11;",
            )
            edits[file.name] = text[: m.start()] + replacement + text[end:]
            found = dict(original_sha256=sha(body.encode()), patched_sha256=sha(replacement.encode()))
    if not found:
        raise ValueError("Missing in-world portrait guest function")
    shutil.copytree(source, output)
    for name, text in edits.items():
        (output / name).write_text(text, newline="\n")
    receipt = dict(
        schema=1,
        revision=revision,
        function=found,
        native_files=NATIVE_FILES,
        world_source_sha256=sha(WORLD_PORTRAIT.read_bytes()),
    )
    (output / "character-portrait-receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")
    content = config.read_text().replace(source.resolve().as_posix(), output.resolve().as_posix())
    # Protect the entire function from code-mod replacement, like other port hooks.
    content += (
        '\n# In-world custom participant portraits\n[[patches.hook]]\nfunc = "obj_loop_characterflag"\nbefore_vram = 0x'
        + ADDRESSES[revision]
        + '\ntext = "/* dkr_character_portrait_pipeline: protected source hook */"\n'
    )
    output_config.write_text(content, newline="\n")
    print("World portrait guest stage v" + str(revision) + ":", output)


def owned_receipt(source: Path, revision: int) -> dict[str, Any]:
    path = source / "character-portrait-receipt.json"
    if not path.exists():
        return {}
    receipt = cast("dict[str, Any]", json.loads(path.read_text()))
    function = cast("dict[str, str]", receipt["function"])
    if receipt["schema"] != 1 or receipt["revision"] != revision or function["original_sha256"] != GUEST_PINS[revision]:
        raise ValueError("Unreviewed portrait guest provenance")
    if receipt["world_source_sha256"] != sha(WORLD_PORTRAIT.read_bytes()):
        raise ValueError("World portrait source has changed")
    if receipt.get("native_files") != NATIVE_FILES:
        raise ValueError("Portrait guest stage and native sources disagree")
    verify_native_files()
    found: list[str] = []
    for file in source.glob("*.c"):
        text = file.read_text()
        matches = list(DEF.finditer(text))
        for i, m in enumerate(matches):
            if m[1] == "obj_loop_characterflag":
                end = matches[i + 1].start() if i + 1 < len(matches) else len(text)
                found.append(sha(text[m.start() : end].encode()))
    if found != [function["patched_sha256"]]:
        raise ValueError("World portrait guest body changed")
    return receipt


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--source", type=Path, required=True)
    p.add_argument("--revision", type=int, choices=(77, 80), required=True)
    p.add_argument("--config", type=Path, required=True)
    p.add_argument("--output-config", type=Path, required=True)
    a = p.parse_args()
    guest(a.source, a.output, a.revision, a.config, a.output_config)
