"""Require the exact exit status of a historical negative-control fixture."""

import subprocess
import sys

expected = int(sys.argv[1])
result = subprocess.run(sys.argv[2:], timeout=30)
if result.returncode != expected:
    print(f"Expected exit {expected}, got {result.returncode}", file=sys.stderr)
    sys.exit(1)
