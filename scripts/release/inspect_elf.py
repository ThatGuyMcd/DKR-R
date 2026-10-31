#!/usr/bin/env python3
"""Inspect an AppDir's ELF architecture, linkage and libc needs without running it.

    inspect_elf.py --tree APPDIR --arch x86_64|aarch64 --output JSON

Every ELF must be the named architecture, and every NEEDED library must be
bundled or a host library (linux-host-libraries.txt beside this script, from
the AppImage excludelist). The report gives the highest GLIBC and GLIBCXX
versions required; finalize.py checks them against the release floor.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import re
import struct
import subprocess
import sys
from typing import Any

SYSTEM = {
    "libc.so.6", "libm.so.6", "libdl.so.2", "libpthread.so.0", "librt.so.1", "libresolv.so.2",
    "ld-linux-x86-64.so.2", "ld-linux-aarch64.so.1", "libvulkan.so.1", "libGL.so.1", "libEGL.so.1", "libGLX.so.0",
}  # fmt: skip
HOST_LIBRARIES = Path(__file__).resolve().parent / "linux-host-libraries.txt"


def latest(values: set[str]) -> str | None:
    return max(values, key=lambda value: tuple(map(int, value.split(".")))) if values else None


def inspect(tree: Path, arch: str) -> dict[str, Any]:
    system = SYSTEM | {
        line.strip() for line in HOST_LIBRARIES.read_text().splitlines() if line.strip() and not line.startswith("#")
    }
    files: dict[str, Any] = {}
    available: set[str] = set()
    required: dict[str, list[str]] = {}
    glibc: set[str] = set()
    cpp: set[str] = set()
    for path in sorted(tree.rglob("*")):
        if not path.is_file():
            continue
        with path.open("rb") as stream:
            header = stream.read(64)
        if header[:4] != b"\x7fELF":
            continue
        if header[:6] != b"\x7fELF\x02\x01" or struct.unpack_from("<H", header, 18)[0] != (
            183 if arch == "aarch64" else 62
        ):
            raise ValueError("Foreign ELF: " + str(path))
        name = path.relative_to(tree).as_posix()
        available.add(path.name)
        dynamic = subprocess.check_output(["readelf", "-d", str(path)], text=True)
        versions = subprocess.check_output(["readelf", "--version-info", str(path)], text=True)
        needed = re.findall(r"\(NEEDED\).*?\[(.*?)\]", dynamic)
        available.update(re.findall(r"\(SONAME\).*?\[(.*?)\]", dynamic))
        needs = versions.split("Version needs section", 1)[-1] if "Version needs section" in versions else ""
        gv = re.findall(r"Name: GLIBC_([0-9.]+)", needs)
        cv = re.findall(r"Name: GLIBCXX_([0-9.]+)", needs)
        glibc.update(gv)
        cpp.update(cv)
        rpath = re.findall(r"\((?:RUNPATH|RPATH)\).*?\[(.*?)\]", dynamic)
        files[name] = {"needed": needed, "rpath": rpath, "glibc": gv, "glibcxx": cv}
        required[name] = needed
    missing = {
        name: [lib for lib in libs if lib not in available and lib not in system] for name, libs in required.items()
    }
    missing = {name: libs for name, libs in missing.items() if libs}
    host = sorted({lib for libs in required.values() for lib in libs if lib in system and lib not in available})
    return {
        "execution": "ELF metadata inspection only; no loader/target execution",
        "arch": arch,
        "max_glibc": latest(glibc),
        "max_glibcxx": latest(cpp),
        "host_library_policy": "https://github.com/AppImage/pkg2appimage/blob/master/excludelist",
        "required_host_libraries": host,
        "missing_non_system_dependencies": missing,
        "files": files,
    }


def main(argv: list[str] | None = None) -> int:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--tree", type=Path, required=True)
    p.add_argument("--arch", choices=("x86_64", "aarch64"), required=True)
    p.add_argument("--output", type=Path, required=True)
    a = p.parse_args(argv)
    report = inspect(a.tree, a.arch)
    a.output.write_text(json.dumps(report, indent=2) + "\n")
    missing = report["missing_non_system_dependencies"]
    print("ELF files:", len(report["files"]), "GLIBC:", report["max_glibc"], "GLIBCXX:", report["max_glibcxx"])
    if missing:
        print("inspect_elf: unbundled non-system dependencies: " + repr(missing), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
