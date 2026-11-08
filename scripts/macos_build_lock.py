#!/usr/bin/env python3
"""Hold a checkout lock across the whole build and its nested prepare scripts."""

import fcntl
import os
from pathlib import Path
import sys


def main():
    root, script, *args = sys.argv[1:]
    path = Path(root).resolve() / "build" / ".macos-build.lock"
    path.parent.mkdir(parents=True, exist_ok=True)
    fd = None
    try:
        inherited = int(os.environ.get("DKR_MACOS_LOCK_FD", "-1"))
        if os.path.samestat(os.fstat(inherited), path.stat()):
            fd = inherited
    except (ValueError, OSError):
        pass
    if fd is None:
        fd = os.open(path, os.O_RDWR | os.O_CREAT, 0o600)
        try:
            fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            print(
                f"Another macOS build/prepare is using {root}. Wait for it to finish; shared extern/, runtime-tools and generated files are locked.",
                file=sys.stderr,
            )
            return 1
    os.set_inheritable(fd, True)
    env = dict(os.environ, DKR_MACOS_LOCK_FD=str(fd), DKR_MACOS_LOCK_ENTRY=script)
    os.execvpe("/bin/bash", ["/bin/bash", script, *args], env)


if __name__ == "__main__":
    sys.exit(main())
