#!/usr/bin/env python3
"""Prepare, sign and verify a release's update assets with the trusted key.

    update_assets.py prepare --packages DIR --output DIR --commit SHA
    update_assets.py sign --packages DIR --output DIR --commit SHA --private-key KEY
    update_assets.py verify --packages DIR --output DIR
    update_assets.py verify-signature --manifest JSON --signature SIG

PACKAGES is finalize.py's handoff folder: the three platform packages, the
Steam Deck copy and Evidence/<PLATFORM>-BUILD-RECEIPT.json for each. The
manifest names the exact committed source (the full SHA of a clean tree at the
release VERSION) and every package's bytes and sha256. It uses the existing
updater format and signer (scripts/update_release.py) and the pinned public
key in packaging/update-public-key.json; it never generates a key, changes a
package, publishes anything or runs a target.
"""

from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
from types import ModuleType, SimpleNamespace
from typing import Any

sys.dont_write_bytecode = True
from release_common import ROOT, TRUSTED_KEY, packages, steam_deck, tag, version  # noqa: E402

RECEIPT_LABEL = {"windows-x64": "WINDOWS", "linux-x86_64": "LINUX", "linux-aarch64": "ARM64"}


def load_signer() -> ModuleType:
    spec = importlib.util.spec_from_file_location("dkrr_release_signer", ROOT / "scripts/update_release.py")
    assert spec and spec.loader
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def openssl() -> str:
    found = shutil.which("openssl")
    if not found and os.name == "nt":
        for name in ("C:/Program Files/Git/usr/bin/openssl.exe", "C:/Program Files/Git/mingw64/bin/openssl.exe"):
            if Path(name).is_file():
                found = name
                os.environ["PATH"] = str(Path(name).parent) + os.pathsep + os.environ.get("PATH", "")
                break
    if not found:
        raise ValueError("OpenSSL with Ed25519 support is required; no private key is generated.")
    return found


def trust() -> bytes:
    key = json.loads((ROOT / "packaging/update-public-key.json").read_text(encoding="utf-8"))
    if key.get("public_key") != TRUSTED_KEY or key.get("algorithm") != "Ed25519":
        raise ValueError("The trusted updater key changed; do not rotate it implicitly.")
    return bytes.fromhex(TRUSTED_KEY)


def verify_signature(manifest: Path, signature: Path) -> dict[str, Any]:
    raw = manifest.read_bytes()
    encoded = signature.read_text(encoding="ascii")
    if not re.fullmatch("[0-9a-fA-F]{128}", encoded):
        raise ValueError("The signature must be exactly 128 hexadecimal characters.")
    with tempfile.TemporaryDirectory(prefix="dkrr-public-verification-") as folder:
        base = Path(folder)
        # PUBLIC SPKI DER only; private key material is never copied.
        public = base / "public.der"
        public.write_bytes(bytes.fromhex("302a300506032b6570032100") + trust())
        detached = base / "signature.bin"
        detached.write_bytes(bytes.fromhex(encoded))
        command = [openssl(), "pkeyutl", "-verify", "-pubin", "-keyform", "DER", "-inkey", str(public), "-rawin"]
        run = subprocess.run([*command, "-in", str(manifest), "-sigfile", str(detached)], capture_output=True)
        if run.returncode:
            raise ValueError("Signature does not verify against the installed launcher trust key.")
    return json.loads(raw)


def committed_source(commit: str) -> str:
    if not re.fullmatch("[0-9a-f]{40}", commit):
        raise ValueError("Use the full committed release source SHA, not a branch name or placeholder.")

    def git(*args: str) -> bytes:
        return subprocess.check_output(["git", *args], cwd=ROOT)

    if git("show", commit + ":VERSION").decode().strip() != version():
        raise ValueError(f"The selected commit is not the {version()} tree.")
    untracked = [p.decode() for p in git("ls-files", "-z", "--others", "--exclude-standard").split(b"\0") if p]
    if untracked:
        raise ValueError("Commit the complete release source before signing; untracked: " + ", ".join(untracked[:5]))
    changed = git("diff", "--name-only", commit, "--").decode().splitlines()
    if changed:
        raise ValueError("Working source differs from the selected release commit: " + ", ".join(changed[:5]))
    return commit


def expected_packages(folder: Path) -> dict[str, tuple[str, int, str]]:
    """Platform -> (name, bytes, sha256) from each package's build receipt."""
    expected: dict[str, tuple[str, int, str]] = {}
    for platform, (system, arch, name) in packages().items():
        receipt = json.loads((folder / "Evidence" / f"{RECEIPT_LABEL[platform]}-BUILD-RECEIPT.json").read_text())
        artifact = receipt["artifact"]
        if (receipt["version"], receipt["platform"], receipt["arch"], artifact["name"]) != (
            version(),
            system,
            arch,
            name,
        ):
            raise ValueError(f"Wrong package receipt for {platform}")
        expected[platform] = (name, artifact["bytes"], artifact["sha256"])
    return expected


def manifest_value(folder: Path, commit: str) -> dict[str, Any]:
    trust()
    signer = load_signer()
    artifacts: list[dict[str, Any]] = []
    expected = expected_packages(folder)
    for platform, (name, size, checksum) in expected.items():
        path = folder / name
        if not path.is_file() or path.stat().st_size != size or signer.digest(path) != checksum:
            raise ValueError("Finished release package is missing or changed: " + name)
        kind = signer.PLATFORMS[platform]
        item: dict[str, Any] = {
            "platform": platform,
            "name": name,
            "kind": kind,
            "bytes": size,
            "sha256": checksum,
            "files": [],
        }
        if platform.startswith("linux-"):
            item["minimum_glibc"] = "2.38"
        else:
            item["minimum_os"] = "10.0"
        if kind == "zip":
            item["root"], item["files"] = signer.zip_table(path, platform)
        artifacts.append(item)
    deck = folder / steam_deck()
    _, size, checksum = expected["linux-x86_64"]
    if not deck.is_file() or deck.stat().st_size != size or signer.digest(deck) != checksum:
        raise ValueError("Steam Deck must be the exact Linux x86_64 package.")
    return {
        "schema": 1,
        "minimum_updater": 1,
        "version": version(),
        "tag": tag(),
        "channel": "stable",
        "commit": commit,
        "notes": "https://github.com/ThatGuyMcd/DKR-R/releases/tag/" + tag(),
        "artifacts": artifacts,
    }


def receipt(output: Path, manifest: Path, signature: Path, value: dict[str, Any]) -> dict[str, Any]:
    report = {
        "schema": 1,
        "version": version(),
        "tag": tag(),
        "commit": value["commit"],
        "signature": "verified-Ed25519-existing-pinned-key",
        "manifest_sha256": hashlib.sha256(manifest.read_bytes()).hexdigest(),
        "signature_file_sha256": hashlib.sha256(signature.read_bytes()).hexdigest(),
        "packages": [{"name": x["name"], "bytes": x["bytes"], "sha256": x["sha256"]} for x in value["artifacts"]],
        "steam_deck": "identical-to-linux-x86_64",
        "native_execution": False,
        "published": False,
    }
    with (output / "DKR-R-update-VERIFICATION.json").open("x", encoding="utf-8", newline="\n") as stream:
        stream.write(json.dumps(report, indent=2) + "\n")
    return report


def run(args: argparse.Namespace) -> None:
    if args.action == "verify-signature":
        value = verify_signature(args.manifest, args.signature)
        print("Trusted-key signature verified:", value.get("version"), value.get("tag"))
        return
    signer = load_signer()
    output, folder = args.output.resolve(), args.packages.resolve()
    manifest, signature = output / "DKR-R-update.json", output / "DKR-R-update.sig"
    if args.action == "verify":
        value = verify_signature(manifest, signature)
        if value.get("version") != version() or value.get("tag") != tag():
            raise ValueError("Wrong signed release identity")
        expected = manifest_value(folder, value["commit"])
        if value != expected or manifest.read_bytes() != signer.canonical(expected):
            raise ValueError("Signed manifest differs from the finished packages or the canonical format.")
        print("Verified signature, source identity, three platform entries and four package hashes.")
        return
    value = manifest_value(folder, committed_source(args.commit))
    output.mkdir(parents=True, exist_ok=True)
    if args.action == "prepare":
        with (output / "DKR-R-update.UNSIGNED.json").open("xb") as stream:
            stream.write(signer.canonical(value))
        print("Prepared the unsigned review manifest; signing needs the existing private Ed25519 key.")
        return
    if not args.private_key or not args.private_key.is_file():
        raise ValueError("Signing needs the existing private Ed25519 key path.")
    private = args.private_key.resolve()
    if any(private.is_relative_to(p) for p in (ROOT, output, folder)):
        raise ValueError("Keep the private key outside source and release/output folders.")
    openssl()
    # The signer checks public-key continuity before writing either asset.
    signer.create_manifest(
        SimpleNamespace(
            private_key=str(private),
            verification_key=str(ROOT / "packaging/update-public-key.json"),
            version=version(),
            tag=tag(),
            commit=value["commit"],
            channel="stable",
            output=str(output),
            artifact=[f"{p}={folder / row[2]}" for p, row in packages().items()],
            minimum_os=[],
        )
    )
    signed = verify_signature(manifest, signature)
    if signed != value or manifest.read_bytes() != signer.canonical(value):
        raise ValueError("Signed output did not match the preflighted manifest.")
    print(json.dumps(receipt(output, manifest, signature, signed), indent=2))


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("action", choices=("prepare", "sign", "verify", "verify-signature"))
    for name in ("packages", "output", "private-key", "manifest", "signature"):
        parser.add_argument("--" + name, type=Path)
    parser.add_argument("--commit")
    args = parser.parse_args(argv)
    required = ("manifest", "signature") if args.action == "verify-signature" else ("packages", "output")
    if args.action in ("prepare", "sign"):
        required += ("commit",)
    for name in required:
        if getattr(args, name.replace("-", "_")) is None:
            parser.error(f"--{name} is required for {args.action}")
    try:
        run(args)
    except (ValueError, OSError, KeyError, json.JSONDecodeError, subprocess.CalledProcessError) as error:
        print(f"update_assets: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
