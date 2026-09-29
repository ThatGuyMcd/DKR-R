"""CMake cross-compiling emulator for the pinned renderer's file_to_c target.

Run the separately built host tool, never the Android target executable.
"""
import pathlib
import subprocess
import sys

if len(sys.argv) < 3:
    raise SystemExit("Expected host tool, target tool and arguments")
host = pathlib.Path(sys.argv[1]).resolve(strict=True)
target = pathlib.Path(sys.argv[2]).stem
if target != "file_to_c":
    raise SystemExit("Only the renderer file_to_c build tool is supported")
raise SystemExit(subprocess.call([str(host), *sys.argv[3:]]))
