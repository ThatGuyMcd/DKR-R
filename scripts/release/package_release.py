#!/usr/bin/env python3
"""Package an allowlisted desktop release. Never runs the game, a helper or an AppImage.

    package_release.py windows --compiled DIR --template DIR --trophy NRM --output DIR
    package_release.py linux --compiled DIR --template APPDIR --runtime-image APPIMAGE \\
        --trophy NRM --output DIR [--arch x86_64|aarch64]

COMPILED is the build's runtime output (bin/<config>); TEMPLATE supplies the
third-party licence trees (Windows) or the AppDir skeleton (Linux). Only
named binaries, runtime assets and legal files go in; every file is scanned
for ROMs, saves, keys and authoring data. The runtime must carry the release
VERSION, the accepted feature markers and both frozen rollback contracts, and
no binary may link an older beta or playtest label. OUTPUT gets the package,
BUILD-RECEIPT.json, ARCHIVE-REPORT.json (the package read back against the
staged tree) and SHA256SUMS.txt.
"""

from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
from typing import Any
import zipfile

sys.dont_write_bytecode = True
from release_common import ROOT, WATER_LICENSE, contracts, release_docs, sha, version  # noqa: E402

DENIED = frozenset(
    (
        ".z64",
        ".v64",
        ".n64",
        ".eep",
        ".mpk",
        ".sra",
        ".fla",
        ".o2r",
        ".otr",
        ".pdb",
        ".key",
        ".pem",
        ".jks",
        ".dmp",
        ".pyc",
    )
)
TROPHY_UPDATE = "6f900a1005c892e4b86a48a3e4bacb7a1e01a7364680ebdd61af954cd94cf154"
# What the accepted v1.0.11 runtime must still contain.
FEATURES = (
    b"complete-family selection",
    b"immutable workload families",
    b"personal EEPROM committed and read-back verified",
    b"one-personal-save",
    b"The mod installation registry needs recovery; it was not replaced.",
    b"The track package contains more than one course. Import each .dkrmap separately.",
    b"frozen offline EEPROM verified; no mutable route switch",
)
FIXTURES = (b"DKR_REPLAY_CAPTURE_FILE", b"DKR_UPDATE_TEST_VERIFIER", b"DKR_WATER_TEST_MAP")
# A pre-release label linked into a release binary: 1.0.7+beta.10,
# 1.0.10-vehicle-playtest.3 and the like.
PRERELEASE = re.compile(r"\d+\.\d+\.\d+(?:\+beta\.\d+|-[a-z]+-playtest\.)")


class Package:
    """A staged tree, its finished package and what the runtime proved."""

    def __init__(self, root: Path, artifact: Path, records: dict[str, Any], ids: dict[str, str], squash: int | None):
        self.root, self.artifact, self.records, self.ids, self.squash = root, artifact, records, ids, squash


def copy(source: Path, target: Path) -> None:
    if not source.is_file():
        raise ValueError("Missing explicit release input: " + str(source))
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, target)


def tree(source: Path, target: Path) -> None:
    for path in source.rglob("*"):
        if path.is_symlink() and not path.resolve(strict=True).is_relative_to(source.resolve()):
            raise ValueError("Input link escapes explicit source root: " + str(path))
    shutil.copytree(source, target, symlinks=True)


def prerelease_label(data: bytes) -> str | None:
    """A pre-release label in the binary, as narrow or UTF-16LE text."""
    views = (data.decode("latin-1"), data.decode("utf-16-le", "ignore"), data[1:].decode("utf-16-le", "ignore"))
    for text in views:
        label = PRERELEASE.search(text)
        if label:
            return label[0]
    return None


def image(path: Path, machine: str, release: bool = False) -> bytes:
    data = path.read_bytes()
    if machine == "windows":
        if data[:2] != b"MZ":
            raise ValueError("Not PE: " + str(path))
        at = struct.unpack_from("<I", data, 0x3C)[0]
        if data[at : at + 4] != b"PE\0\0" or struct.unpack_from("<H", data, at + 4)[0] != 0x8664:
            raise ValueError("Wrong PE architecture")
    elif data[:6] != b"\x7fELF\x02\x01" or struct.unpack_from("<H", data, 18)[0] != (
        183 if machine == "aarch64" else 62
    ):
        raise ValueError("Wrong ELF architecture: " + str(path))
    if release:
        v = version()
        if v.encode() not in data and v.encode("utf-16le") not in data:
            raise ValueError("Release version missing: " + str(path))
        label = prerelease_label(data)
        if label:
            raise ValueError(f"Pre-release label linked into {path.name}: {label}")
    return data


def features(data: bytes) -> dict[str, str]:
    for marker in FEATURES:
        if marker not in data:
            raise ValueError("Missing accepted feature: " + repr(marker))
    for marker in FIXTURES:
        if marker in data:
            raise ValueError("Private fixture found")
    ids = contracts()
    for revision, value in ids.items():
        if value.encode() not in data:
            raise ValueError(f"Rollback contract v{revision} missing from the runtime")
    return ids


def scan(root: Path) -> dict[str, Any]:
    records: dict[str, Any] = {}
    for path in sorted(root.rglob("*")):
        name = path.relative_to(root).as_posix()
        parts = {s.lower() for s in path.relative_to(root).parts}
        if path.is_symlink():
            if path.readlink().is_absolute() or not path.resolve(strict=True).is_relative_to(root.resolve()):
                raise ValueError("Unsafe package link")
            records[name] = {"link": path.readlink().as_posix()}
            continue
        if not path.is_file():
            continue
        if path.suffix.lower() in DENIED or path.name.lower() in ("imgui.ini", "device.bin"):
            raise ValueError("Private/non-runtime file: " + name)
        if "custom-magic-codes" in parts and (path.name != "dkr_custom_trophies.nrm" or sha(path) != TROPHY_UPDATE):
            raise ValueError("Only the pinned runtime trophy updater belongs here")
        if parts & {".git", "saves", "logs", "online-profile", "optional-mods", "__pycache__"}:
            raise ValueError("Private/authoring data in game package: " + name)
        if "player guide" in name.lower() or path.suffix.lower() in (".html", ".pdf"):
            raise ValueError("Offline guide must not ship")
        with path.open("rb") as stream:
            if stream.read(4).hex() in ("80371240", "37804012", "40123780"):
                raise ValueError("ROM header found")
        records[name] = {"sha256": sha(path), "bytes": path.stat().st_size}
    return records


def ownership(root: Path, platform: str) -> None:
    spec = importlib.util.spec_from_file_location("release_ownership", ROOT / "scripts/update_release.py")
    assert spec and spec.loader
    rules = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(rules)
    if platform == "windows-x64":
        command = [sys.executable, "-B", str(ROOT / "scripts/update_release.py"), "prepare-tree"]
        command += ["--directory", str(root), "--version", version(), "--platform", platform]
        subprocess.run(command, check=True)
        return
    names: list[str] = []
    for path in root.rglob("*"):
        if path.is_dir() and not path.is_symlink():
            continue
        name = path.relative_to(root).as_posix()
        if not rules.safe_path(name) or rules.protected(name):
            raise ValueError("Unsafe owned path")
        names.append(name)
    name = "assets/updates/install.json"
    if name not in names:
        names.append(name)
    path = root / name
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(rules.canonical({"schema": 1, "version": version(), "platform": platform, "files": sorted(names)}))


def notes(root: Path) -> None:
    copy(ROOT / "packaging/RELEASE-README.md", root / "README.txt")
    copy(release_docs() / "RELEASE-NOTES.txt", root / "RELEASE-NOTES.txt")


def trophy(source: Path | None, target: Path) -> None:
    if source is None or sha(source) != TROPHY_UPDATE:
        raise ValueError("Provide the exact admitted trophy updater, not a repackaged SDK sample")
    copy(source, target / "Custom-Magic-Codes/dkr_custom_trophies.nrm")


def legal() -> tuple[tuple[str, Path], ...]:
    return (
        ("LICENSE.md", ROOT / "LICENSE.md"),
        ("THIRD_PARTY.md", ROOT / "THIRD_PARTY.md"),
        ("COPYING-NOTICE.md", ROOT / "runtime-recomp/COPYING-NOTICE.md"),
    )


def windows(a: argparse.Namespace) -> Package:
    root = a.output / f"DKR-R-{version()}-Windows-x64"
    root.mkdir(parents=True)
    # No blanket template copy: only runtime art and required legal material.
    tree(a.compiled / "assets", root / "assets")
    tree(a.template / "ThirdPartyLicenses", root / "ThirdPartyLicenses")
    for name, source in legal():
        copy(source, root / name)
    helpers = ("DKR-R-Runtime.exe", "DKR-R-InputHost.exe", "DKR-R-ModWorker.exe", "DKR-R-Updater.exe")
    helpers += ("SDL2.dll", "SDL3.dll", "dxcompiler.dll", "dxil.dll")
    for name in ("DKR-R.exe", *("libexec/dkr-r/" + n for n in helpers)):
        image(a.compiled / name, "windows", name in ("DKR-R.exe", "libexec/dkr-r/DKR-R-Runtime.exe"))
        copy(a.compiled / name, root / name)
    ids = features((root / "libexec/dkr-r/DKR-R-Runtime.exe").read_bytes())
    trophy(a.trophy, root)
    notes(root)
    ownership(root, "windows-x64")
    records = scan(root)
    archive = a.output / (root.name + ".zip")
    with zipfile.ZipFile(archive, "x", zipfile.ZIP_DEFLATED, compresslevel=6) as z:
        for path in sorted(root.rglob("*")):
            if path.is_file():
                z.write(path, root.name + "/" + path.relative_to(root).as_posix())
    return Package(root, archive, records, ids, None)


def run(command: list[str]) -> subprocess.CompletedProcess[bytes]:
    return subprocess.run(command, check=True, capture_output=True)


def stamp_version(text: str) -> str:
    return re.sub(r"^X-AppImage-Version=.*$", "X-AppImage-Version=" + version(), text, flags=re.M)


def linux(a: argparse.Namespace) -> Package:
    v = version()
    root = a.output / f"DKR-R-{v}-Linux-{a.arch}.AppDir"
    root.mkdir(parents=True)
    tree(a.template / "usr/lib", root / "usr/lib")
    for name in ("applications", "icons", "metainfo"):
        if (a.template / "usr/share" / name).is_dir():
            tree(a.template / "usr/share" / name, root / "usr/share" / name)
    copy(ROOT / "packaging/linux/dkr-port.appdata.xml", root / "usr/share/metainfo/dkr-port.appdata.xml")
    for path in (root / "usr/share/applications").glob("*.desktop"):
        path.write_text(stamp_version(path.read_text()))
    # Dependency copyright/licence directories remain; player/SDK docs do not.
    for path in (a.template / "usr/share/doc").iterdir():
        if path.name != "dkr-port":
            tree(path, root / "usr/share/doc" / path.name)
    docs = root / "usr/share/doc/dkr-port"
    for name in ("licenses", "third-party"):
        if (a.template / "usr/share/doc/dkr-port" / name).is_dir():
            tree(a.template / "usr/share/doc/dkr-port" / name, docs / name)
    for name, source in (*legal(), ("WAVE-RACE-WATER-LICENSE.txt", WATER_LICENSE)):
        copy(source, docs / name)
    for name in ("AppRun", "dkr-r.png", ".DirIcon"):
        path = a.template / name
        if path.is_symlink():
            (root / name).symlink_to(path.readlink())
        elif path.exists():
            copy(path, root / name)
    for path in a.template.glob("*.desktop"):
        (root / path.name).write_text(stamp_version(path.read_text()))
    tree(a.compiled / "assets", root / "usr/bin/assets")
    pairs = [(a.compiled / "DKR-R", root / "usr/bin/DKR-R")]
    for name in ("DKR-R-ModWorker", "DKR-R-Updater", "DKR-R-InputHost", "libSDL3.so.0"):
        pairs.append((a.compiled / "libexec/dkr-r" / name, root / "usr/libexec/dkr-r" / name))
    for source, target in pairs:
        image(source, a.arch, target.name == "DKR-R")
        copy(source, target)
        target.chmod(0o755)
    libexec = root / "usr/libexec/dkr-r"
    run(["patchelf", "--set-rpath", "$ORIGIN/../lib", str(root / "usr/bin/DKR-R")])
    for name in ("DKR-R-ModWorker", "DKR-R-Updater"):
        run(["patchelf", "--set-rpath", "$ORIGIN/../../lib", str(libexec / name)])
    run(["patchelf", "--set-rpath", "$ORIGIN", str(libexec / "DKR-R-InputHost")])
    ids = features((root / "usr/bin/DKR-R").read_bytes())
    notes(root)
    trophy(a.trophy, root / "usr/bin")
    ownership(root, "linux-aarch64" if a.arch == "aarch64" else "linux-x86_64")
    records = scan(root)
    original = image(a.runtime_image, a.arch)
    if original[8:11] != b"AI\x02":
        raise ValueError("Not type-2 AppImage")
    offsets: list[int] = []
    for match in re.finditer(b"hsqs", original[: 4 * 1024 * 1024]):
        probe = ["unsquashfs", "-s", "-o", str(match.start()), str(a.runtime_image)]
        if subprocess.run(probe, capture_output=True).returncode == 0:
            offsets.append(match.start())
    if len(offsets) != 1:
        raise ValueError("Ambiguous AppImage runtime")
    squash = a.output / (root.stem + ".squashfs")
    options = ["-noappend", "-comp", "zstd", "-processors", "3", "-no-progress", "-root-owned"]
    run(["mksquashfs", str(root), str(squash), *options])
    artifact = a.output / f"DKR-R-{v}-Linux-{a.arch}.AppImage"
    with artifact.open("xb") as out, squash.open("rb") as payload:
        out.write(original[: offsets[0]])
        shutil.copyfileobj(payload, out)
    artifact.chmod(0o755)
    return Package(root, artifact, records, ids, offsets[0])


def read_back(package: Package) -> dict[str, Any]:
    """Every staged file, read back out of the finished package."""
    found: dict[str, Any] = {}
    if package.squash is None:
        with zipfile.ZipFile(package.artifact) as z:
            if z.testzip():
                raise ValueError("ZIP CRC failed")
            prefix = package.root.name + "/"
            for info in z.infolist():
                data = z.read(info)
                found[info.filename.removeprefix(prefix)] = {
                    "sha256": hashlib.sha256(data).hexdigest(),
                    "bytes": len(data),
                }
    else:
        extracted = package.artifact.parent / (package.artifact.stem + ".read-back")
        run(["unsquashfs", "-no-progress", "-d", str(extracted), "-o", str(package.squash), str(package.artifact)])
        found = scan(extracted)
        shutil.rmtree(extracted)
    files = {name: row for name, row in package.records.items() if "sha256" in row}
    missing = sorted(set(files) - set(found))
    differ = sorted(name for name in files if name in found and found[name] != files[name])
    return {
        "artifact_sha256": sha(package.artifact),
        "files": len(files),
        "missing": missing,
        "differ": differ,
        "exact_payload_match": not missing and not differ,
    }


def main(argv: list[str] | None = None) -> int:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("platform", choices=("windows", "linux"))
    for name in ("compiled", "template", "output"):
        p.add_argument("--" + name, type=Path, required=True)
    p.add_argument("--arch", choices=("x86_64", "aarch64"), default="x86_64")
    p.add_argument("--runtime-image", type=Path, help="the AppImage runtime to reuse (Linux)")
    p.add_argument("--trophy", type=Path, required=True)
    a = p.parse_args(argv)
    if a.platform == "windows" and a.arch != "x86_64":
        p.error("the Windows release is x64")
    if a.platform == "linux" and a.runtime_image is None:
        p.error("--runtime-image is required for Linux")
    if a.output.exists():
        p.error("choose a NEW output directory")
    try:
        a.output.mkdir(parents=True)
        package = windows(a) if a.platform == "windows" else linux(a)
        report = read_back(package)
        (a.output / "ARCHIVE-REPORT.json").write_text(json.dumps(report, indent=2) + "\n")
        if not report["exact_payload_match"]:
            raise ValueError("The package does not read back as the staged tree")
        artifact = package.artifact
        receipt = {
            "schema": 2,
            "version": version(),
            "label": f"v{version()} Release Build",
            "platform": a.platform,
            "arch": a.arch,
            "rollback_build_ids": package.ids,
            "artifact": {"name": artifact.name, "sha256": sha(artifact), "bytes": artifact.stat().st_size},
            "files": package.records,
            "offline_player_guide": False,
            "published": False,
            "automatic_updates_signed": False,
            "validation": "Compilation and static package inspection only. Gameplay/hardware acceptance is separate.",
        }
        (a.output / "BUILD-RECEIPT.json").write_text(json.dumps(receipt, indent=2) + "\n")
        (a.output / "SHA256SUMS.txt").write_text(sha(artifact) + "  " + artifact.name + "\n")
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        print(f"package_release: {error}", file=sys.stderr)
        return 1
    print("Release packaged without target execution:", package.artifact)
    return 0


if __name__ == "__main__":
    sys.exit(main())
