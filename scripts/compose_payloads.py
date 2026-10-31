#!/usr/bin/env python3
"""Compose and recompile one revision's production CPU payload.

The step both platform prepare scripts (Prepare-DKR-Runtime-macOS.sh and
Prepare-DKR-Runtime.ps1) share between building the decomp and running
scripts/prepare_payloads.py:

    compose_payloads.py rom --revision REV SOURCE TARGET
        normalise a user's ROM to big-endian and check it is the retail dump
    compose_payloads.py payload --revision REV --elf ELF --rom Z64
            --recompiler N64RECOMP --output DIR
        compose the production policy (the versioned base policy plus the
        legacy mod, track menu, character and character menu fragments, with
        the scene runtime), write the N64Recomp config and run N64Recomp:
        DIR/policy.json, DIR/dkr.us.v<rev>.toml, DIR/RecompiledFuncs and
        DIR/n64recomp.log

prepare_payloads.py reads DIR/RecompiledFuncs and DIR/dkr.us.v<rev>.toml.
Exit status: 0 on success, 1 when a step fails, 2 on usage errors.
"""

from __future__ import annotations

import argparse
from collections.abc import Sequence
import hashlib
from pathlib import Path
import subprocess
import sys
from typing import cast

ROOT = Path(__file__).resolve().parents[1]
RUNTIME = ROOT / "runtime-recomp"
# Retail SHA-1 of each supported US dump, and its entry point (mainproc).
RETAIL_SHA1 = {
    77: "0cb115d8716dbbc2922fda38e533b9fe63bb9670",
    80: "6d96743d46f8c0cd0edb0ec5600b003c89b93755",
}
ENTRYPOINT = {77: "0x80065D40", 80: "0x80065F80"}


class ComposeError(Exception):
    pass


def normalise_rom(source: Path, target: Path, revision: int) -> str:
    """Write SOURCE to TARGET in z64 byte order once its SHA-1 is the retail
    dump's; returns the SHA-1. TARGET is only rewritten when it differs."""
    data = bytearray(source.read_bytes())
    magic = bytes(data[:4]).hex()
    if magic == "37804012":
        data[0::2], data[1::2] = data[1::2], data[0::2]
    elif magic == "40123780":
        for i in range(0, len(data) - 3, 4):
            data[i : i + 4] = data[i : i + 4][::-1]
    elif magic != "80371240":
        raise ComposeError(f"{source}: not an N64 ROM (magic {magic})")
    digest = hashlib.sha1(data).hexdigest()
    if digest != RETAIL_SHA1[revision]:
        raise ComposeError(f"{source}: SHA-1 {digest}, expected {RETAIL_SHA1[revision]} (US v{revision})")
    target.parent.mkdir(parents=True, exist_ok=True)
    if not target.exists() or target.read_bytes() != bytes(data):
        target.write_bytes(data)
    return digest


def run(command: Sequence[str], what: str, log: Path | None = None) -> None:
    done = subprocess.run(list(command), cwd=RUNTIME, capture_output=True, text=True)
    if log is not None:
        log.write_text(done.stdout + done.stderr, encoding="utf-8")
    if done.returncode:
        detail = (done.stderr or done.stdout).strip().splitlines()[-20:]
        raise ComposeError(f"{what} failed ({done.returncode}):\n" + "\n".join(detail))


def compose(revision: int, elf: Path, rom: Path, recompiler: Path, output: Path) -> None:
    for needed in (elf, rom, recompiler):
        if not needed.is_file():
            raise ComposeError(f"missing {needed}")
    functions = output / "RecompiledFuncs"
    functions.mkdir(parents=True, exist_ok=True)
    policy, config = output / "policy.json", output / f"dkr.us.v{revision}.toml"
    fragments = (
        ("--fragment", "legacy-mods"),
        ("--track-menu", "legacy-track-menu"),
        ("--characters", "legacy-characters"),
        ("--character-menu", "legacy-character-menu"),
    )
    command = [
        sys.executable,
        str(ROOT / "scripts/compose_legacy_mod_policy.py"),
        "--policy",
        str(RUNTIME / f"dkr.us.v{revision}.recomp-policy.json"),
    ]
    for flag, name in fragments:
        command += [flag, str(RUNTIME / f"{name}.v{revision}.recomp-fragment.json")]
    command += ["--scene-runtime", "--elf", str(elf), "--output", str(policy)]
    run(command, f"v{revision} policy composition")
    run(
        [
            sys.executable,
            str(ROOT / "scripts/generate_recomp_config.py"),
            "--policy",
            str(policy),
            "--elf",
            str(elf),
            "--rom",
            str(rom),
            "--output-functions",
            str(functions),
            "--entrypoint",
            ENTRYPOINT[revision],
            "--output",
            str(config),
        ],
        f"v{revision} N64Recomp config",
    )
    run([str(recompiler), str(config)], f"v{revision} N64Recomp", output / "n64recomp.log")


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    rom = sub.add_parser("rom", help="normalise and check a retail ROM")
    rom.add_argument("--revision", type=int, choices=(77, 80), required=True)
    rom.add_argument("source", type=Path)
    rom.add_argument("target", type=Path)
    payload = sub.add_parser("payload", help="compose the policy and run N64Recomp")
    payload.add_argument("--revision", type=int, choices=(77, 80), required=True)
    payload.add_argument("--elf", type=Path, required=True)
    payload.add_argument("--rom", type=Path, required=True, help="the decomp's built z64")
    payload.add_argument("--recompiler", type=Path, required=True)
    payload.add_argument("--output", type=Path, required=True)
    args = parser.parse_args(argv)
    revision = cast(int, args.revision)
    try:
        if args.command == "rom":
            source = cast(Path, args.source)
            digest = normalise_rom(source, cast(Path, args.target), revision)
            print(f"[OK] {source.name} verified ({digest})")
        else:
            output = cast(Path, args.output).resolve()
            compose(
                revision,
                cast(Path, args.elf).resolve(),
                cast(Path, args.rom).resolve(),
                cast(Path, args.recompiler).resolve(),
                output,
            )
            print(f"[OK] {output / 'RecompiledFuncs'}")
    except (ComposeError, OSError) as error:
        print(f"compose_payloads: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
