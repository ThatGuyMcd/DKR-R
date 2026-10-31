#!/usr/bin/env python3
"""Seal the signed handoff and write the manual GitHub upload instructions.

    seal.py --work DIR --output DIR

OUTPUT is finalize.py's handoff folder after update_assets.py sign. The signed
manifest must verify and name this checkout's HEAD; WORK/windows must hold the
four updater qualification logs (qualify_updates.py), each passing. They are
copied into Evidence/, then UPLOAD.txt and SHA256SUMS.txt are written.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import subprocess
import sys

sys.dont_write_bytecode = True
from release_common import ROOT, packages, sha, steam_deck, tag, version  # noqa: E402

CASES = ("normal", "corrupt-stage", "interrupted", "migrate-layout")


def main(argv: list[str] | None = None) -> int:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    for name in ("work", "output"):
        p.add_argument("--" + name, type=Path, required=True)
    a = p.parse_args(argv)
    here = Path(__file__).resolve().parent
    verify = [sys.executable, "-B", str(here / "update_assets.py"), "verify", "--packages", str(a.output)]
    if subprocess.run([*verify, "--output", str(a.output)]).returncode:
        return 1
    manifest = json.loads((a.output / "DKR-R-update.json").read_text())
    head = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
    if manifest["commit"] != head:
        print("seal: the signed commit differs from this checkout's HEAD", file=sys.stderr)
        return 1
    for case in CASES:
        path = a.work / "windows" / f"updater-{case}.log"
        if not path.is_file() or b"PASS:" not in path.read_bytes() or b"Traceback" in path.read_bytes():
            print(f"seal: updater qualification failed or missing: {case}", file=sys.stderr)
            return 1
        (a.output / "Evidence" / path.name).write_bytes(path.read_bytes())
    v = version()
    names = [row[2] for row in packages().values()] + [steam_deck()]
    (a.output / "UPLOAD.txt").write_text(
        f"DKR-R v{v} - GitHub upload\n\nRelease title: DKR-R v{v} Release Build\nTag: {tag()}\n"
        f"Source commit: {head}\n\nCreate a draft release at that exact commit. Upload:\n"
        + "".join(name + "\n" for name in names)
        + "DKR-R-update.json\nDKR-R-update.sig\nSHA256SUMS.txt\n\n"
        "Paste RELEASE-NOTES.txt as the description. The changelog TXT can also\n"
        "be attached. Evidence is for review; do not upload keys or source backups.\n"
        "Finish the final gameplay/device/update checks, verify uploaded hashes,\n"
        "then publish as a stable release. Publishing makes auto updates available.\n"
        "Do not edit signed JSON or rename/recompress the application packages.\n"
    )
    with (a.output / "VALIDATION.txt").open("a") as f:
        f.write(
            "\nPASS: Existing trusted Ed25519 key signs the exact packages and source commit.\n"
            "PASS: Isolated Windows update, corrupt-stage rejection, interrupted recovery\n"
            "      and layout migration preserve synthetic user data; ROM-free self-tests only.\n"
            f"Source commit: {head}\n"
        )
    paths = [p for p in sorted(a.output.rglob("*")) if p.is_file() and p.name != "SHA256SUMS.txt"]
    hashes = {p.relative_to(a.output).as_posix(): sha(p) for p in paths}
    (a.output / "SHA256SUMS.txt").write_text("".join(f"{value}  {name}\n" for name, value in hashes.items()))
    print("Signed stable handoff sealed:", len(paths), "files;", a.output)
    return 0


if __name__ == "__main__":
    sys.exit(main())
