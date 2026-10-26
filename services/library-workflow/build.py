#!/usr/bin/env python3
"""Stage the mod library website and API update over a deployed portal base.

    build.py stage --output DIR [--base DIR]
    build.py verify --output DIR
    build.py package --output DIR --archive ZIP

BASE is a copy of the deployed portal: index.html, assets/{app,api-store,
uploads-client,hosted-files}.js and PRIVATE-API/worker-single-file.mjs. It
defaults to baseline/ here, the deployed portal this update was reviewed
against. A BASE with a SHA256SUMS file is checked against it first. Every
splice (transforms.py) matches its anchor exactly once or the stage fails, so
a base that moved on fails loudly instead of deploying a half-applied update.

The stage holds FTP-PATCH/ (website files), PRIVATE-API/ (worker and schema),
ROLLBACK/ (the base files it replaces), REVIEW.patch and WORKFLOW-RECEIPT.json.
Nothing is uploaded or deployed; DEPLOYMENT.md is the owner's procedure.
"""

from __future__ import annotations

import argparse
import difflib
import hashlib
import json
from pathlib import Path
import shutil
import sys
import zipfile

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import transforms  # noqa: E402

REVISION = "library-20261022-workflow-1"
FRONTEND = ("app.js", "api-store.js", "uploads-client.js")
WORKER = "PRIVATE-API/worker-single-file.mjs"
AUTHORED = ("backend.inl", "ui.inl", "batch-client.js", "workflow.css", "workflow-schema.sql", "transforms.py")


def sha(raw: bytes) -> str:
    return hashlib.sha256(raw).hexdigest()


def once(text: str, before: str, after: str) -> str:
    if text.count(before) != 1:
        raise ValueError("Portal anchor changed: " + before[:100])
    return text.replace(before, after)


def check_base(base: Path) -> None:
    for name in (*(f"assets/{n}" for n in FRONTEND), "assets/hosted-files.js", "index.html", WORKER):
        if not (base / name).is_file():
            raise ValueError(f"base is missing {name}")
    sums = base / "SHA256SUMS"
    if sums.is_file():
        for line in sums.read_text(encoding="utf-8").splitlines():
            digest, name = line.split(maxsplit=1)
            if sha((base / name).read_bytes()) != digest:
                raise ValueError(f"base file differs from its SHA256SUMS: {name}")


def authors() -> dict[str, str]:
    return {name: sha((HERE / name).read_bytes()) for name in AUTHORED}


def stage(base: Path, output: Path) -> None:
    if output.exists():
        raise ValueError("Choose a NEW stage directory")
    check_base(base)
    read = lambda name: (base / name).read_text(encoding="utf-8")  # noqa: E731
    ftp = output / "FTP-PATCH"
    assets = ftp / "assets"
    assets.mkdir(parents=True)
    for name in FRONTEND:
        (assets / name).write_text(transforms.transform(name, read(f"assets/{name}")), encoding="utf-8", newline="\n")
    (assets / "hosted-files.js").write_text(
        transforms.hosted(read("assets/hosted-files.js")), encoding="utf-8", newline="\n"
    )
    for name in ("batch-client.js", "workflow.css"):
        shutil.copy2(HERE / name, assets / name)
    index = read("index.html").replace("library-20261018-uploads-1", REVISION)
    index = once(index, "characters, legacy mods, magic codes", "characters, vehicles, magic codes")
    index = once(
        index,
        '  <script src="assets/app.js',
        f'  <link rel="stylesheet" href="assets/workflow.css?v={REVISION}">\n'
        f'  <script src="assets/batch-client.js?v={REVISION}" defer></script>\n  <script src="assets/app.js',
    )
    (ftp / "index.html").write_text(index, encoding="utf-8", newline="\n")
    backend = output / "PRIVATE-API"
    backend.mkdir()
    (backend / "worker-single-file.mjs").write_text(transforms.worker(read(WORKER)), encoding="utf-8", newline="\n")
    shutil.copy2(HERE / "workflow-schema.sql", backend / "workflow-schema.sql")
    for name in ("DEPLOYMENT.md", "QA.md"):
        shutil.copy2(HERE / name, output / name)
    review: list[str] = []
    replaced = [(f"assets/{n}", f"FTP-PATCH/assets/{n}") for n in (*FRONTEND, "hosted-files.js")]
    replaced += [("index.html", "FTP-PATCH/index.html"), (WORKER, WORKER)]
    for before, after in replaced:
        review.extend(
            difflib.unified_diff(
                read(before).splitlines(True),
                (output / after).read_text(encoding="utf-8").splitlines(True),
                fromfile="before/" + after,
                tofile="after/" + after,
            )
        )
    for name in ("batch-client.js", "workflow.css"):
        review.extend(
            difflib.unified_diff(
                [],
                (assets / name).read_text(encoding="utf-8").splitlines(True),
                fromfile="/dev/null",
                tofile="after/FTP-PATCH/assets/" + name,
            )
        )
    (output / "REVIEW.patch").write_text("".join(review), encoding="utf-8", newline="\n")
    for before, after in replaced:
        target = output / "ROLLBACK" / after
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(base / before, target)
    files = {p.relative_to(output).as_posix(): sha(p.read_bytes()) for p in sorted(output.rglob("*")) if p.is_file()}
    inputs = {before: sha((base / before).read_bytes()) for before, _ in replaced}
    receipt = {
        "schema": 2,
        "revision": REVISION,
        "authored": authors(),
        "inputs": inputs,
        "policy": "Website/API only. Launcher, FTP transport and generated recomp files unchanged.",
        "files": files,
    }
    (output / "WORKFLOW-RECEIPT.json").write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")
    verify(output)


def verify(output: Path) -> None:
    receipt = json.loads((output / "WORKFLOW-RECEIPT.json").read_text(encoding="utf-8"))
    if receipt["authored"] != authors():
        raise ValueError("The service sources changed; stage again.")
    files = {
        p.relative_to(output).as_posix(): sha(p.read_bytes())
        for p in sorted(output.rglob("*"))
        if p.is_file() and p.name != "WORKFLOW-RECEIPT.json"
    }
    if files != receipt["files"]:
        raise ValueError("Staged membership or bytes changed.")
    print("Website/API stage verified:", output)


def package(output: Path, archive: Path) -> None:
    verify(output)
    if archive.exists():
        raise ValueError("Choose a new archive path.")
    archive.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(archive, "x", zipfile.ZIP_DEFLATED) as z:
        for p in sorted(output.rglob("*")):
            if p.is_file():
                z.write(p, p.relative_to(output).as_posix())
    print(archive, sha(archive.read_bytes()))


def main(argv: list[str] | None = None) -> int:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("action", choices=("stage", "verify", "package"))
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--base", type=Path, default=HERE / "baseline", help="the deployed portal (default: baseline/)")
    p.add_argument("--archive", type=Path)
    a = p.parse_args(argv)
    try:
        if a.action == "stage":
            stage(a.base.resolve(), a.output.resolve())
        elif a.action == "verify":
            verify(a.output.resolve())
        else:
            if a.archive is None:
                p.error("package needs --archive")
            package(a.output.resolve(), a.archive.resolve())
    except (ValueError, OSError) as error:
        print(f"library-workflow: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
