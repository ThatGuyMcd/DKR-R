#!/usr/bin/env python3
"""Run the isolated Windows updater checks from the exact preceding release.

    qualify_updates.py --previous ZIP --previous-receipt JSON -- <test_update_package.py arguments>

The previous release's Windows ZIP and its BUILD-RECEIPT.json must agree
(name and sha256). scripts/test_update_package.py then installs that release
into a scratch folder and updates it to the new package, for one --case.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import subprocess
import sys

sys.dont_write_bytecode = True
from release_common import ROOT, sha  # noqa: E402


def main(argv: list[str] | None = None) -> int:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--previous", type=Path, required=True)
    p.add_argument("--previous-receipt", type=Path, required=True)
    a, rest = p.parse_known_args(argv)
    receipt = json.loads(a.previous_receipt.read_text())
    artifact = receipt["artifact"]
    if artifact["sha256"] != sha(a.previous) or artifact["name"] != a.previous.name or receipt["platform"] != "windows":
        print("qualify_updates: the previous package differs from its receipt", file=sys.stderr)
        return 1
    rest = [arg for arg in rest if arg != "--"]
    command = [sys.executable, "-B", str(ROOT / "scripts/test_update_package.py"), *rest]
    command += ["--previous", str(a.previous), "--previous-version", receipt["version"]]
    return subprocess.run(command).returncode


if __name__ == "__main__":
    sys.exit(main())
