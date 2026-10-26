#!/usr/bin/env python3
"""Compose the Online directory owner bundle over the deployed library API.

    build.py --api-worker FILE --output DIR [--expect-sha256 HEX] [--archive ZIP]
    build.py --verify --output DIR

FILE is the library API Worker the directory gateway slots into
(worker-single-file.mjs as deployed, or PRIVATE-API/worker-single-file.mjs
from services/library-workflow/build.py stage). It lives outside this repo.
gateway.inl goes in front of its fetch handler and the handler asks the
directory first; each splice matches its anchor exactly once or the build
fails. --expect-sha256 pins FILE to the reviewed bytes.

The bundle holds PRIVATE-API/ (the spliced Worker and the bindings to merge),
ONLINE-WORKER/ (directory.mjs and wrangler-online.jsonc), ROLLBACK/ (FILE),
REVIEW.patch, the owner docs and BUNDLE-RECEIPT.json. Nothing is deployed;
DEPLOYMENT.md is the owner's procedure.
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
AUTHORED = ("gateway.inl", "directory.mjs", "wrangler-online.jsonc")
DOCS = ("DEPLOYMENT.md", "PLAYTEST.txt", "IMPLEMENTATION.txt", "QA.md")
FETCH = "export default {\n  async fetch(request, env, ctx) {"
DISPATCH = """  async fetch(request, env, ctx) {
    let response;
    try {
      const url = new URL(request.url);"""


def sha(raw: bytes) -> str:
    return hashlib.sha256(raw).hexdigest()


def once(text: str, old: str, new: str) -> str:
    if text.count(old) != 1:
        raise ValueError("API entry point changed: " + old[:100])
    return text.replace(old, new, 1)


def splice(worker: str) -> str:
    """The library API Worker with the directory gateway in front."""
    worker = once(worker, FETCH, (HERE / "gateway.inl").read_text(encoding="utf-8") + "\n" + FETCH)
    return once(
        worker,
        DISPATCH,
        DISPATCH
        + """
      const onlineResponse = await handleOnlineDirectory(request, env);
      if (onlineResponse) return onlineResponse;""",
    )


def compose(api: Path, output: Path, expected: str | None) -> None:
    if output.exists():
        raise ValueError("Choose a fresh owner bundle directory")
    original = api.read_bytes()
    if expected is not None and sha(original) != expected:
        raise ValueError(f"{api} is not the reviewed API Worker (sha256 {sha(original)})")
    worker = splice(original.decode("utf-8"))
    for name in ("PRIVATE-API", "ONLINE-WORKER", "ROLLBACK"):
        (output / name).mkdir(parents=True)
    (output / "PRIVATE-API/worker-single-file.mjs").write_text(worker, encoding="utf-8", newline="\n")
    (output / "ROLLBACK/worker-single-file.mjs").write_bytes(original)
    review = difflib.unified_diff(
        original.decode("utf-8").splitlines(True),
        worker.splitlines(True),
        fromfile="before/worker-single-file.mjs",
        tofile="after/worker-single-file.mjs",
    )
    (output / "REVIEW.patch").write_text("".join(review), encoding="utf-8")
    for name in ("directory.mjs", "wrangler-online.jsonc"):
        shutil.copy2(HERE / name, output / "ONLINE-WORKER" / name)
    for name in DOCS:
        shutil.copy2(HERE / name, output / name)
    bindings = {
        "services": [{"binding": "ONLINE_DIRECTORY", "service": "dkrr-online-directory"}],
        "vars": {"ONLINE_ENABLED": "false", "RUNNING_SPECTATORS_ENABLED": "false"},
    }
    (output / "PRIVATE-API/BINDINGS-TO-MERGE.jsonc").write_text(json.dumps(bindings, indent=2) + "\n", encoding="utf-8")
    receipt = {
        "schema": 2,
        "feature": "dkrr-online-directory-v1",
        "published": False,
        "source_api_sha256": sha(original),
        "authoring_sha256": {name: sha((HERE / name).read_bytes()) for name in (*AUTHORED, *DOCS)},
        "files": {
            p.relative_to(output).as_posix(): sha(p.read_bytes()) for p in sorted(output.rglob("*")) if p.is_file()
        },
    }
    (output / "BUNDLE-RECEIPT.json").write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")
    print("Owner API bundle composed; nothing deployed:", output)


def verify(output: Path) -> None:
    receipt = json.loads((output / "BUNDLE-RECEIPT.json").read_text(encoding="utf-8"))
    for name, digest in receipt["files"].items():
        if sha((output / name).read_bytes()) != digest:
            raise ValueError("Bundle file changed: " + name)
    if receipt["published"]:
        raise ValueError("Unexpected deployment claim")
    print("Owner bundle hashes verified.")


def archive_bundle(output: Path, archive: Path) -> None:
    if archive.exists():
        raise ValueError("Choose a fresh bundle archive")
    archive.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as z:
        for path in sorted(output.rglob("*")):
            if path.is_file():
                z.write(path, path.relative_to(output).as_posix())
    print("Owner bundle archive:", archive, sha(archive.read_bytes()))


def main(argv: list[str] | None = None) -> int:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--api-worker", type=Path, help="the library API Worker to splice into")
    p.add_argument("--expect-sha256", help="refuse an API Worker with other bytes")
    p.add_argument("--archive", type=Path)
    p.add_argument("--verify", action="store_true", help="only check an existing bundle")
    a = p.parse_args(argv)
    try:
        output = a.output.resolve()
        if not a.verify:
            if a.api_worker is None:
                p.error("--api-worker is required to compose a bundle")
            compose(a.api_worker.resolve(), output, a.expect_sha256)
        verify(output)
        if a.archive:
            archive_bundle(output, a.archive.resolve())
    except (ValueError, OSError) as error:
        print(f"online-directory: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
