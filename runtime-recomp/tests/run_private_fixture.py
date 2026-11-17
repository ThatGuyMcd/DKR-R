#!/usr/bin/env python3
"""Run a fixture that requires a fresh private directory; remove its files afterward."""

import os
from pathlib import Path
import subprocess
import sys
import tempfile


def main():
    executable = Path(sys.argv[1]).resolve(strict=True)
    directory = str(Path(tempfile.gettempdir()).resolve())
    if os.name == "nt" and not directory.startswith("\\\\?\\"):
        # Content-addressed caches can exceed MAX_PATH even under a short TEMP.
        # Use the same extended path for creation, the fixture and cleanup.
        directory = "\\\\?\\" + directory
    with tempfile.TemporaryDirectory(prefix="dkr-private-fixture-", dir=directory) as parent:
        return subprocess.run([str(executable), str(Path(parent).resolve() / "fixture")]).returncode


if __name__ == "__main__":
    raise SystemExit(main())
