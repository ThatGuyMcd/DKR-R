#!/usr/bin/env python3
"""Run a fixture that requires a fresh private directory; remove its files afterward."""

from pathlib import Path
import subprocess
import sys
import tempfile


def main():
    executable = Path(sys.argv[1]).resolve(strict=True)
    with tempfile.TemporaryDirectory(prefix="dkr-private-fixture-") as parent:
        return subprocess.run([str(executable), str(Path(parent).resolve() / "fixture")]).returncode


if __name__ == "__main__":
    raise SystemExit(main())
