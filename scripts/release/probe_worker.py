#!/usr/bin/env python3
"""Check the packaged mod worker's online handshake without loading a ROM or mod.

    probe_worker.py --worker EXE --output DIR [--emulator QEMU --sysroot DIR]

Writes OUTPUT/PROBE-RECEIPT.json (finalize.py checks it against the package).
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import time

CAPABILITIES = {"schema": 1, "online_prepare": 1, "online_prepare_batch": 1}


def main(argv: list[str] | None = None) -> int:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--worker", type=Path, required=True)
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--emulator", type=Path, help="run the worker under this user-mode emulator (ARM64)")
    p.add_argument("--sysroot", type=Path, help="the emulator's QEMU_LD_PREFIX")
    a = p.parse_args(argv)
    worker, output = a.worker.resolve(), a.output.resolve()
    if not worker.is_file() or output.exists():
        p.error("need an existing packaged worker and a fresh probe folder")
    output.mkdir(parents=True)
    request = output / "request.json"
    request.write_text(
        json.dumps({"schema": 1, "operation": "online-capabilities", "source": output.as_posix(), "roms": []}) + "\n"
    )
    env = os.environ.copy()
    if a.sysroot:
        env["QEMU_LD_PREFIX"] = str(a.sysroot)
    prefix = [str(a.emulator)] if a.emulator else []
    started = time.monotonic()
    flags = subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0
    result = subprocess.run(
        [*prefix, str(worker), str(request)],
        cwd=output,
        env=env,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        timeout=60 if prefix else 12,
        creationflags=flags,
    )
    (output / "output.log").write_bytes(result.stdout[:16384])
    capabilities = output / "capabilities.json"
    if result.returncode != 0 or not capabilities.is_file() or json.loads(capabilities.read_text()) != CAPABILITIES:
        print("probe_worker: the capability probe failed; see output.log", file=sys.stderr)
        return 1
    with worker.open("rb") as stream:
        digest = hashlib.file_digest(stream, "sha256").hexdigest()
    receipt = {
        "worker_sha256": digest,
        "online_prepare": 1,
        "online_prepare_batch": 1,
        "elapsed_ms": round((time.monotonic() - started) * 1000),
        "emulated": bool(prefix),
        "roms_or_mods_loaded": False,
        "game_executed": False,
    }
    (output / "PROBE-RECEIPT.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print("Packaged mod worker online handshake passed; no ROM or mod loaded.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
