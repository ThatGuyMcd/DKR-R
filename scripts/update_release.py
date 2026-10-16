#!/usr/bin/env python3
"""Build signed DKR-R update assets. Never put the private key in a release.

Uses OpenSSL's standard Ed25519, compatible with the unmodified Monocypher
optional verifier. Package creation and signing are distinct from publishing.
"""
import argparse
import hashlib
import json
import os
import plistlib
from pathlib import Path, PurePosixPath
import re
import shutil
import stat
import subprocess
import tempfile
import zipfile

MAX_PACKAGE = 512 * 1024 * 1024
MAX_EXTRACTED = 1024 * 1024 * 1024
PROTECTED = {"portable.txt", "dkr-runtime-data", "mods", "online-mods", "texture-packs",
             "saves", "logs", "crash-dumps"}
GAME_EXTENSIONS = {".z64", ".v64", ".n64", ".eep", ".mpk", ".sra", ".fla"}
PLATFORMS = {"windows-x64": "zip", "windows-arm64": "zip", "linux-x86_64": "appimage",
             "linux-aarch64": "appimage", "macos-arm64": "zip", "macos-x86_64": "zip",
             "android-arm64": "apk", "ios-arm64": "ipa"}

def safe_path(name):
    if not name or len(name) > 1024 or name.startswith("/") or name.endswith("/"):
        return False
    if any(ord(c) < 32 or ord(c) == 127 or c in "\\:" for c in name):
        return False
    for part in name.split("/"):
        if part in ("", ".", "..") or part.endswith((".", " ")):
            return False
        stem = part.split(".", 1)[0].lower()
        if stem in ("con", "prn", "aux", "nul") or re.fullmatch(r"(?:com|lpt)[0-9]", stem):
            return False
    return True

def protected(name):
    top = name.split("/", 1)[0].lower()
    return top in PROTECTED or top.startswith(".dkr-update") or PurePosixPath(name).suffix.lower() in GAME_EXTENSIONS

def canonical(value):
    return (json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=True) + "\n").encode()

def digest(path):
    with open(path, "rb") as source:
        return hashlib.file_digest(source, "sha256").hexdigest()

def run_ssl(*args):
    openssl = shutil.which("openssl")
    if not openssl:
        raise ValueError("OpenSSL with Ed25519 support is required for release signing.")
    return subprocess.check_output([openssl, *map(str, args)], stderr=subprocess.PIPE)

def public_key(private):
    der = run_ssl("pkey", "-in", private, "-pubout", "-outform", "DER")
    prefix = bytes.fromhex("302a300506032b6570032100")
    if len(der) != len(prefix) + 32 or not der.startswith(prefix):
        raise ValueError("Release key is not Ed25519.")
    return der[len(prefix):].hex()

def generate_key(path):
    path = Path(path).absolute()
    if path.exists():
        raise ValueError("Refusing to replace an existing signing key.")
    path.parent.mkdir(parents=True, exist_ok=True)
    run_ssl("genpkey", "-algorithm", "ED25519", "-out", path)
    if os.name == "nt":
        owner = subprocess.check_output(["whoami"], text=True).strip()
        subprocess.run(["icacls", str(path), "/inheritance:r", "/grant:r", f"{owner}:(F)"],
                       check=True, stdout=subprocess.DEVNULL)
    else:
        path.chmod(0o600)
    return public_key(path)

def prepare_tree(directory, version, platform):
    """Call after staging all program files, before building the archive."""
    # Match native Version::parse's build-metadata support. Candidate bundles
    # may carry +metadata without changing their minimum SDK release ordering.
    Version = re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+(?:-[0-9A-Za-z.-]+)?(?:\+[0-9A-Za-z.-]+)?", version)
    if not Version or platform not in PLATFORMS:
        raise ValueError("Invalid staged update version/platform.")
    root = Path(directory).resolve(strict=True)
    receipt = root / "assets/updates/install.json"
    owned = []
    for path in sorted(root.rglob("*")):
        name = path.relative_to(root).as_posix()
        if path.is_symlink():
            raise ValueError("Use the bundle packager for application-bundle symlinks.")
        if path.is_dir():
            continue
        if not safe_path(name) or protected(name):
            raise ValueError(f"User/game data may not be included in update ownership: {name}")
        owned.append(name)
    if "assets/updates/install.json" not in owned:
        owned.append("assets/updates/install.json")
    receipt.parent.mkdir(parents=True, exist_ok=True)
    receipt.write_bytes(canonical({"schema": 1, "version": version, "platform": platform,
                                  "files": sorted(owned)}))

def zip_table(path, platform):
    table = []
    names = set()
    root = None
    extracted = 0
    with zipfile.ZipFile(path) as archive:
        if len(archive.infolist()) > 8192:
            raise ValueError("Too many update ZIP entries.")
        for entry in archive.infolist():
            name = entry.filename.rstrip("/") if entry.is_dir() else entry.filename
            if not safe_path(name) or name.lower() in names or entry.flag_bits & 1:
                raise ValueError(f"Unsafe, encrypted or duplicate ZIP entry: {name}")
            names.add(name.lower())
            parts = name.split("/", 1)
            if root is None:
                root = parts[0]
            if parts[0] != root:
                raise ValueError("ZIP must contain exactly one application root.")
            if entry.is_dir():
                continue
            if len(parts) != 2 or protected(parts[1]):
                raise ValueError(f"ZIP contains unowned user/game data: {name}")
            extracted += entry.file_size
            if entry.file_size > MAX_PACKAGE or extracted > MAX_EXTRACTED or len(table) >= 4096:
                raise ValueError("Update ZIP exceeds extraction limits.")
            mode = entry.external_attr >> 16
            linked = stat.S_ISLNK(mode)
            if linked and platform.startswith("windows"):
                raise ValueError("Windows update archives may not contain links.")
            with archive.open(entry) as source:
                sha = hashlib.file_digest(source, "sha256").hexdigest()
            item = {"path": parts[1], "bytes": entry.file_size, "sha256": sha,
                    "mode": (mode & 0o777) or 0o644}
            if linked:
                link = archive.read(entry).decode("utf-8")
                if len(link) > 1024 or link.startswith("/") or "\\" in link:
                    raise ValueError("Unsafe bundle link.")
                # Link resolution/escape is checked again before extraction.
                segments = list(PurePosixPath(parts[1]).parent.parts)
                for segment in PurePosixPath(link).parts:
                    if segment == "..":
                        if not segments:
                            raise ValueError("Bundle link escapes its root.")
                        segments.pop()
                    elif segment != ".":
                        segments.append(segment)
                item["link"] = link
            table.append(item)
    if not table:
        raise ValueError("Empty update archive.")
    if platform.startswith("windows"):
        required = {"DKR-R.exe", "libexec/dkr-r/DKR-R-Updater.exe",
                    "libexec/dkr-r/DKR-R-ModWorker.exe", "libexec/dkr-r/DKR-R-InputHost.exe",
                    "assets/updates/install.json"}
        paths = {f["path"] for f in table}
        prefix = "libexec/dkr-r/" if "libexec/dkr-r/DKR-R-Runtime.exe" in paths else ""
        required |= {prefix + name for name in ("DKR-R-Runtime.exe", "SDL2.dll", "dxcompiler.dll", "dxil.dll")}
        if not required.issubset(paths):
            raise ValueError("Windows package lacks updater bootstrap/runtime/helper or ownership receipt.")
    return root, table

def create_manifest(args):
    expected = json.loads(Path(args.verification_key).read_text(encoding="utf-8"))["public_key"]
    if public_key(args.private_key) != expected:
        raise ValueError("Signing key does not match the selected trusted public key. Use an explicit previous verification key only for a deliberate, old-key-signed bridge release.")
    if not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+(?:-[0-9A-Za-z.-]+)?", args.version):
        raise ValueError("Invalid release version.")
    if args.channel == "stable" and "-" in args.version:
        raise ValueError("Stable releases may not use a prerelease version.")
    if not re.fullmatch(r"[A-Za-z0-9.-]+", args.tag) or not re.fullmatch(r"[0-9a-f]{40}", args.commit):
        raise ValueError("Invalid release tag/commit.")
    artifacts = []
    platforms = set()
    for argument in args.artifact:
        platform, filename = argument.split("=", 1)
        if platform not in PLATFORMS or platform in platforms:
            raise ValueError("Unsupported or repeated release platform.")
        platforms.add(platform)
        path = Path(filename).resolve(strict=True)
        size = path.stat().st_size
        if not safe_path(path.name) or any(c in path.name for c in "%?#") or not 0 < size <= MAX_PACKAGE:
            raise ValueError("Invalid update package name/size.")
        kind = PLATFORMS[platform]
        if path.suffix.lower() != {"zip": ".zip", "appimage": ".appimage", "apk": ".apk", "ipa": ".ipa"}[kind]:
            raise ValueError("Package kind and platform disagree.")
        item = {"platform": platform, "name": path.name, "kind": kind, "bytes": size,
                "sha256": digest(path), "files": []}
        minimums = {"windows-x64": "10.0", "windows-arm64": "10.0", "android-arm64": "28", "ios-arm64": "18.0"}
        if platform in minimums:
            item["minimum_os"] = minimums[platform]
        if platform.startswith("linux-"):
            item["minimum_glibc"] = "2.38"
        for requirement in args.minimum_os:
            target, value = requirement.split("=", 1)
            if target == platform:
                if not re.fullmatch(r"[0-9]+(?:\.[0-9]+){0,2}", value):
                    raise ValueError("Invalid minimum OS requirement.")
                item["minimum_os"] = value
        if kind == "zip":
            item["root"], item["files"] = zip_table(path, platform)
            if platform.startswith("macos-") and "minimum_os" not in item:
                with zipfile.ZipFile(path) as archive:
                    plist = plistlib.loads(archive.read(item["root"] + "/Contents/Info.plist"))
                    minimum = plist.get("LSMinimumSystemVersion", "")
                    if not re.fullmatch(r"[0-9]+(?:\.[0-9]+){0,2}", minimum):
                        raise ValueError("macOS packages must declare their minimum OS in Info.plist or --minimum-os.")
                    item["minimum_os"] = minimum
        artifacts.append(item)
    if not artifacts:
        raise ValueError("At least one qualified platform package is required.")
    output = Path(args.output).absolute()
    output.mkdir(parents=True, exist_ok=True)
    manifest = output / "DKR-R-update.json"
    signature = output / "DKR-R-update.sig"
    if manifest.exists() or signature.exists():
        raise ValueError("Refusing to overwrite already signed update assets.")
    value = {"schema": 1, "minimum_updater": 1, "version": args.version, "tag": args.tag,
             "channel": args.channel, "commit": args.commit,
             "notes": f"https://github.com/ThatGuyMcd/DKR-R/releases/tag/{args.tag}",
             "artifacts": artifacts}
    encoded = canonical(value)
    if len(encoded) > 2 * 1024 * 1024:
        raise ValueError("Update manifest is too large.")
    # Sign exactly these bytes. No key material is copied to the output folder.
    with tempfile.TemporaryDirectory(prefix="dkrr-release-sign-") as scratch:
        input_file, sig_file = Path(scratch) / "manifest.json", Path(scratch) / "signature.bin"
        input_file.write_bytes(encoded)
        run_ssl("pkeyutl", "-sign", "-rawin", "-inkey", args.private_key,
                "-in", input_file, "-out", sig_file)
        sig = sig_file.read_bytes()
        if len(sig) != 64:
            raise ValueError("Invalid Ed25519 signature length.")
        manifest.write_bytes(encoded)
        signature.write_text(sig.hex(), encoding="ascii")
    return public_key(args.private_key)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="action", required=True)
    key = sub.add_parser("keygen"); key.add_argument("--private-key", required=True)
    pub = sub.add_parser("public-key"); pub.add_argument("--private-key", required=True)
    stage = sub.add_parser("prepare-tree")
    stage.add_argument("--directory", required=True); stage.add_argument("--version", required=True)
    stage.add_argument("--platform", choices=PLATFORMS, required=True)
    sign = sub.add_parser("sign")
    for name in ("version", "tag", "commit", "private-key", "output"):
        sign.add_argument("--" + name, required=True)
    sign.add_argument("--channel", choices=("stable", "beta"), default="stable")
    sign.add_argument("--verification-key", default=str(Path(__file__).resolve().parent.parent / "packaging/update-public-key.json"))
    sign.add_argument("--artifact", action="append", required=True, metavar="PLATFORM=FILE")
    sign.add_argument("--minimum-os", action="append", default=[], metavar="PLATFORM=VERSION")
    args = parser.parse_args()
    try:
        if args.action == "keygen": print("Public Ed25519 key:", generate_key(args.private_key))
        elif args.action == "public-key": print("Public Ed25519 key:", public_key(args.private_key))
        elif args.action == "prepare-tree": prepare_tree(args.directory, args.version, args.platform)
        else: print("Signed update assets; public Ed25519 key:", create_manifest(args))
    except (ValueError, OSError, subprocess.CalledProcessError, zipfile.BadZipFile) as error:
        parser.exit(1, f"Update release preparation failed: {error}\n")

if __name__ == "__main__":
    main()
