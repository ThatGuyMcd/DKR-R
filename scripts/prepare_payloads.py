#!/usr/bin/env python3
"""Prepare the build inputs the runtime derives from the recompiled guest code.

Each revision's inputs are the N64Recomp output of the composed production
policy, the N64Recomp config that produced it, and the decomp ELF. By default
they are where scripts/Prepare-DKR-Runtime-macOS.sh puts them (build/generated/
v<rev>/RecompiledFuncs, build/generated/v<rev>/dkr.us.v<rev>.toml, build/decomp/
v<rev>/build/dkr.us.v<rev>.elf); --input names them for one revision instead
(the Windows composition, docs/BUILDING.md). From those, this prepares, under
OUT (default build/prepared):

* generated-v<rev>/ and v<rev>.toml: the CPU payload and code policy the game
  compiles, with the save-layout and in-world portrait stages applied
  (scripts/save_layout_pipeline.py, scripts/character_portrait_pipeline.py);
* owned-v<rev>/: the owned rollback engine (scripts/generate_replay_probe.py),
  regenerated after each stage with the roots of the one before, as the
  release process did;
* camera-kernel-v<rev>/: the Camera++ draw-only scene kernel
  (scripts/camera_scene_kernel.py);
* model-v<rev>/: the local model detail kernel
  (scripts/generate_visual_model_kernel.py);
* fixtures-v<rev>/: when the payload has a Fixtures directory beside it
  (compose_payloads.py writes one), the same policy's N64Recomp output with
  one function per file, unstaged. Host tests compile a few guest functions
  from it (the allocator) that no stage here changes.

runtime-recomp's CMake reads these paths by default (cmake/PreparedInputs.cmake).
Intermediate stages stay in OUT/stages/. OUT is rebuilt from scratch on every
run; it must lie under the work tree's build/ (the kernels refuse anything
else) and is only removed when this script made it.

    prepare_payloads.py [--output DIR] [--input REV FUNCS CONFIG ELF ...]

Exit status: 0 on success, 1 when a stage refuses its input.
"""

from __future__ import annotations

import argparse
from collections.abc import Sequence
import json
from pathlib import Path
import shutil
import subprocess
import sys
from typing import cast

import camera_scene_kernel
import character_portrait_pipeline
import generate_visual_model_kernel
import save_layout_pipeline

ROOT = Path(__file__).resolve().parents[1]
HEADER = ROOT / "extern/n64-modern-runtime/N64Recomp/include/recomp.h"
MARKER = "prepared.json"
# Present from the first write until the bundle is complete.
IN_PROGRESS = "preparing"
REVISIONS = (77, 80)
# The owned engine's options, read back from a manifest for the next stage.
OWNED_OPTIONS = {
    "scene_cuts": "--scene-cuts",
    "full_scenes": "--full-scenes",
    "audio_services": "--audio-services",
    "input_services": "--input-services",
    "authored_cpu": "--authored-cpu",
    "owned_mod_services": "--mod-services",
    "checked_inline_memory": "--inline-memory",
    "boss_finish_diagnostics": "--boss-finish-diagnostics",
}
BASELINE_OPTIONS = (
    "--scene-cuts",
    "--audio-services",
    "--input-services",
    "--authored-cpu",
    "--isolate-symbols",
    "--full-scenes",
    "--mod-services",
    "--inline-memory",
)


class PrepareError(Exception):
    pass


def owned(source: Path, config: Path, output: Path, revision: int, options: Sequence[str]) -> None:
    """Run the owned-engine generator the way the release process did."""
    command = [
        sys.executable,
        "-B",
        str(ROOT / "scripts/generate_replay_probe.py"),
        str(source),
        str(HEADER),
        str(output),
        "--revision",
        str(revision),
        *options,
        "--code-policy-config",
        str(config),
    ]
    done = subprocess.run(command, cwd=ROOT, capture_output=True, text=True)
    if done.returncode:
        raise PrepareError(f"owned engine v{revision} ({output.name}): {done.stderr.strip()[-600:]}")


def options_from(manifest: Path, revision: int) -> list[str]:
    """The previous owned engine's options and roots, for the next stage."""
    data = cast("dict[str, object]", json.loads(manifest.read_text(encoding="utf-8")))
    if data.get("revision") != revision or not data.get("runtime_symbol_isolation"):
        raise PrepareError(f"{manifest}: not an isolated v{revision} owned engine")
    options = ["--isolate-symbols", *(flag for key, flag in OWNED_OPTIONS.items() if data.get(key))]
    return [*options, "--roots", *cast("list[str]", data["roots"])]


def policy_copy(config: Path, source: Path, target: Path, output: Path) -> None:
    """The N64Recomp code policy, naming the copied CPU payload."""
    text = config.read_text(encoding="utf-8")
    if text.count(source.as_posix()) != 1:
        raise PrepareError(f"{config}: does not name {source} exactly once")
    output.write_text(text.replace(source.as_posix(), target.as_posix()), encoding="utf-8", newline="\n")


def default_inputs(revision: int) -> tuple[Path, Path, Path]:
    generated = ROOT / f"build/generated/v{revision}"
    elf = ROOT / f"build/decomp/v{revision}/build/dkr.us.v{revision}.elf"
    return generated / "RecompiledFuncs", generated / f"dkr.us.v{revision}.toml", elf


def prepare_revision(out: Path, revision: int, raw: Path, config: Path, elf: Path) -> None:
    for needed in (raw, config, elf):
        if not needed.exists():
            raise PrepareError(f"missing {needed}; run scripts/Prepare-DKR-Runtime-macOS.sh first")
    raw, config = raw.resolve(), config.resolve()
    baseline, save = out / "stages/baseline", out / "stages/save-layout"
    for stage in (baseline, save):
        stage.mkdir(parents=True, exist_ok=True)

    # Baseline: the N64Recomp payload as it is, and its full owned engine.
    shutil.copytree(raw, baseline / f"generated-v{revision}")
    policy_copy(config, raw, (baseline / f"generated-v{revision}").resolve(), baseline / f"v{revision}.toml")
    owned(
        baseline / f"generated-v{revision}",
        baseline / f"v{revision}.toml",
        baseline / f"owned-v{revision}",
        revision,
        BASELINE_OPTIONS,
    )

    # Save layout, then the in-world portrait; each reruns the owned engine.
    save_layout_pipeline.stage(
        baseline / f"generated-v{revision}",
        save / f"generated-v{revision}",
        revision,
        baseline / f"v{revision}.toml",
        save / f"v{revision}.toml",
    )
    owned(
        save / f"generated-v{revision}",
        save / f"v{revision}.toml",
        save / f"owned-v{revision}",
        revision,
        options_from(baseline / f"owned-v{revision}/manifest.json", revision),
    )
    character_portrait_pipeline.guest(
        save / f"generated-v{revision}",
        out / f"generated-v{revision}",
        revision,
        save / f"v{revision}.toml",
        out / f"v{revision}.toml",
    )
    owned(
        out / f"generated-v{revision}",
        out / f"v{revision}.toml",
        out / f"owned-v{revision}",
        revision,
        options_from(save / f"owned-v{revision}/manifest.json", revision),
    )

    fixtures = raw.parent / "Fixtures"
    if fixtures.is_dir():
        shutil.copytree(fixtures, out / f"fixtures-v{revision}")

    # The kernels read the N64Recomp payload, not the staged one.
    camera_scene_kernel.generate(raw, HEADER, elf, revision, out / f"camera-kernel-v{revision}")
    generate_visual_model_kernel.generate(raw, HEADER, elf, revision, out / f"model-v{revision}")


def previous_output(out: Path) -> Path:
    return out.with_name(out.name + ".previous")


def ours(directory: Path) -> bool:
    return (directory / MARKER).is_file() or (directory / IN_PROGRESS).is_file()


def fresh_output(out: Path) -> None:
    """Start an empty OUT, keeping the last complete bundle beside it.

    The bundle is generated in place because its files record their own
    absolute paths. The previous one waits in OUT.previous until publish()
    or restore() settles which of the two survives.
    """
    if (ROOT / "build").resolve() not in out.parents:
        raise PrepareError(f"{out} must lie inside {ROOT / 'build'}")
    previous = previous_output(out)
    if out.exists() and not ours(out):
        raise PrepareError(f"{out} exists and was not made by prepare_payloads.py; remove it yourself")
    if previous.exists() and not ours(previous):
        raise PrepareError(f"{previous} exists and was not made by prepare_payloads.py; remove it yourself")
    if out.exists() and (out / MARKER).is_file():
        # A complete bundle is the one worth keeping; an older one set aside
        # by an interrupted run is not.
        if previous.exists():
            shutil.rmtree(previous)
        out.rename(previous)
    elif out.exists():
        shutil.rmtree(out)
    out.mkdir(parents=True)
    (out / IN_PROGRESS).write_text("prepare_payloads.py is writing this directory\n", encoding="utf-8")


def restore(out: Path) -> None:
    """Drop a failed OUT and put the last complete bundle back."""
    previous = previous_output(out)
    if out.exists() and ours(out):
        shutil.rmtree(out)
    if previous.exists() and not out.exists():
        previous.rename(out)


def publish(out: Path, revisions: Sequence[int]) -> None:
    (out / MARKER).write_text(json.dumps({"schema": 1, "revisions": list(revisions)}) + "\n", encoding="utf-8")
    (out / IN_PROGRESS).unlink()
    previous = previous_output(out)
    if previous.exists():
        shutil.rmtree(previous)


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--output", type=Path, default=ROOT / "build/prepared", help="default: build/prepared")
    parser.add_argument(
        "--input",
        nargs=4,
        action="append",
        metavar=("REV", "FUNCS", "CONFIG", "ELF"),
        help="one revision's N64Recomp output, its config and decomp ELF (repeat per revision)",
    )
    args = parser.parse_args(argv)
    out = cast(Path, args.output).resolve()
    inputs: dict[int, tuple[Path, Path, Path]] = {}
    for rev, funcs, config, elf in cast("list[list[str]] | None", args.input) or []:
        if not rev.isdigit() or int(rev) not in REVISIONS or int(rev) in inputs:
            parser.error(f"--input REV must be 77 or 80, once each (got {rev})")
        inputs[int(rev)] = (Path(funcs), Path(config), Path(elf))
    if not inputs:
        inputs = {revision: default_inputs(revision) for revision in REVISIONS}
    revisions = sorted(inputs)
    try:
        fresh_output(out)
    except PrepareError as error:
        print(f"prepare_payloads: {error}", file=sys.stderr)
        return 1
    try:
        for revision in revisions:
            prepare_revision(out, revision, *inputs[revision])
            print(f"prepare_payloads: US v{revision} prepared in {out}")
        publish(out, revisions)
    except (PrepareError, ValueError) as error:
        restore(out)
        print(f"prepare_payloads: {error}", file=sys.stderr)
        return 1
    except BaseException:
        # Interrupts and stage crashes must not cost the last good bundle.
        restore(out)
        raise
    return 0


if __name__ == "__main__":
    sys.exit(main())
