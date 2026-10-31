#!/usr/bin/env python3
"""Exercise a signed Windows package update in an isolated temporary install.

The parent process exits after acknowledgement, just like the real launcher.
The successor runs the actual runtime's ROM-free EEPROM tests, not a fake game.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import uuid
import zipfile


def parent(request):
    value = json.loads(Path(request).read_text())
    job = Path(request).parent
    helper = Path(value["target"]) / "libexec/dkr-r/DKR-R-Updater.exe"
    copied = job / helper.name
    copied.write_bytes(helper.read_bytes())
    value["parent"] = os.getpid()
    Path(request).write_text(json.dumps(value), encoding="utf-8")
    subprocess.Popen([str(copied), "--apply", str(request)], cwd=job)
    deadline = time.monotonic() + 10
    while time.monotonic() < deadline:
        if (job / "ready").is_file():
            return 0
        time.sleep(0.05)
    raise RuntimeError("Helper did not acknowledge the isolated parent")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--package")
    parser.add_argument("--manifest")
    parser.add_argument("--signature")
    parser.add_argument("--parent")
    parser.add_argument("--extractor", help="Built DKRUpdateCoreTests.exe; uses the production signed ZIP staging path")
    parser.add_argument(
        "--case", choices=("normal", "corrupt-stage", "interrupted", "migrate-layout"), default="normal"
    )
    parser.add_argument(
        "--previous", help="the preceding release's Windows ZIP to install first (default: --package itself)"
    )
    parser.add_argument("--previous-version", default="1.0.6", help="the installed version the update replaces")
    args = parser.parse_args()
    if args.parent:
        return parent(args.parent)
    manifest = json.loads(Path(args.manifest).read_text())
    artifact = next(a for a in manifest["artifacts"] if a["platform"] == "windows-x64")
    runtime_relative = (
        "libexec/dkr-r/DKR-R-Runtime.exe"
        if any(f["path"] == "libexec/dkr-r/DKR-R-Runtime.exe" for f in artifact["files"])
        else "DKR-R-Runtime.exe"
    )
    with tempfile.TemporaryDirectory(prefix="dkrr-package-update-") as folder:
        root = Path(folder) / "Install with spaces café"
        root.mkdir()
        with zipfile.ZipFile(args.previous or args.package) as z:
            prefix = (Path(args.previous).stem if args.previous else artifact["root"]) + "/"
            for entry in z.infolist():
                if entry.is_dir():
                    continue
                name = entry.filename.removeprefix(prefix)
                output = root / name
                output.parent.mkdir(parents=True, exist_ok=True)
                output.write_bytes(z.read(entry))
        # The initial receipt describes the preceding release; the actual old
        # DLL/exe set is complete, so replacement and retirement are exercised.
        receipt = root / "assets/updates/install.json"
        value = json.loads(receipt.read_text())
        value["version"] = args.previous_version
        receipt.write_text(json.dumps(value), encoding="utf-8")
        data = root / "dkr-runtime-data"
        (data / "saves").mkdir(parents=True)
        saved = data / "saves/offline.eep"
        saved.write_bytes(bytes(range(256)) * 2)
        (root / "portable.txt").write_text("portable\n")
        (root / "personal.txt").write_text("user-owned\n")
        before = hashlib.sha256(saved.read_bytes()).hexdigest()
        if runtime_relative.startswith("libexec/"):
            # Exercise native root discovery, assets/helpers and portable mode
            # from an unrelated working directory, including a direct runtime start.
            for executable in (root / "DKR-R.exe", root / runtime_relative):
                subprocess.run([str(executable), "--self-test-install-layout", str(root), "on"], cwd=folder, check=True)
            (root / "portable.txt").unlink()
            subprocess.run(
                [str(root / "DKR-R.exe"), "--self-test-install-layout", str(root), "off"], cwd=folder, check=True
            )
            (root / "portable.txt").write_text("portable\n")
        if args.case == "migrate-layout":
            for name in ("DKR-R-Runtime.exe", "SDL2.dll", "dxcompiler.dll", "dxil.dll"):
                source = root / "libexec/dkr-r" / name
                source.rename(root / name)
                value["files"].remove("libexec/dkr-r/" + name)
                value["files"].append(name)
            receipt.write_text(json.dumps(value), encoding="utf-8")
        if args.case == "interrupted":
            runtime = root / runtime_relative
            old = runtime.read_bytes()
            broken = b"interrupted replacement"
            transaction = root / ".dkr-update-transaction"
            (transaction / "backup").mkdir(parents=True)
            backup = transaction / "backup" / runtime_relative
            backup.parent.mkdir(parents=True, exist_ok=True)
            backup.write_bytes(old)
            (transaction / "journal.json").write_text(
                json.dumps(
                    {
                        "schema": 1,
                        "state": "prepared",
                        "files": [
                            {
                                "path": runtime_relative,
                                "old": True,
                                "new": True,
                                "old_hash": hashlib.sha256(old).hexdigest(),
                                "new_hash": hashlib.sha256(broken).hexdigest(),
                            }
                        ],
                    }
                ),
                encoding="utf-8",
            )
            runtime.write_bytes(broken)
            subprocess.run([str(root / "DKR-R.exe"), "--self-test-pak", str(Path(folder) / "recovery-pak")], check=True)
            assert runtime.read_bytes() == old
            assert hashlib.sha256(saved.read_bytes()).hexdigest() == before
            assert not transaction.exists()
            print(
                "PASS: bootstrap repaired an interrupted runtime replacement before DLL/runtime loading; offline save unchanged"
            )
            return 0
        identifier = uuid.uuid4().hex
        job = data / "updates" / ("handoff-" + identifier)
        job.mkdir(parents=True)
        (job / "manifest.json").write_bytes(Path(args.manifest).read_bytes())
        (job / "manifest.sig").write_bytes(Path(args.signature).read_bytes())
        stage = root / (".dkr-update-stage-" + identifier)
        if args.extractor:
            subprocess.run(
                [args.extractor, "--stage-signed", args.package, args.manifest, args.signature, str(stage)], check=True
            )
        else:
            stage.mkdir()
            with zipfile.ZipFile(args.package) as z:
                for entry in z.infolist():
                    if entry.is_dir():
                        continue
                    output = stage / entry.filename.removeprefix(artifact["root"] + "/")
                    output.parent.mkdir(parents=True, exist_ok=True)
                    output.write_bytes(z.read(entry))
        if args.case == "corrupt-stage":
            (stage / runtime_relative).write_bytes(b"corrupt stage")
        request = {
            "schema": 1,
            "id": identifier,
            "kind": "zip",
            "platform": "windows-x64",
            "parent": 0,
            "target": str(root),
            "stage": str(stage),
            "data": str(data),
            "executable": str(root / "DKR-R.exe"),
            "arguments": ["--self-test-pak", str(Path(folder) / "pak-tests")],
        }
        request_file = job / "request.json"
        request_file.write_text(json.dumps(request), encoding="utf-8")
        subprocess.run([sys.executable, __file__, "--parent", str(request_file)], check=True)
        deadline = time.monotonic() + 90
        while time.monotonic() < deadline and not (job / "result.json").is_file():
            time.sleep(0.1)
        result = json.loads((job / "result.json").read_text())
        expected = "failed" if args.case == "corrupt-stage" else "complete"
        if result["state"] != expected:
            raise RuntimeError(result)
        assert hashlib.sha256(saved.read_bytes()).hexdigest() == before
        assert (root / "portable.txt").read_text() == "portable\n"
        assert (root / "personal.txt").read_text() == "user-owned\n"
        assert json.loads(receipt.read_text())["version"] == (
            args.previous_version if args.case == "corrupt-stage" else manifest["version"]
        )
        assert not (root / ".dkr-update-transaction").exists()
        if args.case == "corrupt-stage":
            print(
                "PASS: corrupt staged runtime rejected; complete prior installation, offline save and unowned files retained"
            )
            # Failure recovery relaunches the preceding runtime asynchronously.
            # Let its bounded ROM-free test retire before deleting test files.
            time.sleep(3)
            return 0
        assert (root / ".dkr-update-backups").is_dir()
        if args.case == "migrate-layout":
            assert (root / runtime_relative).is_file()
            for name in ("DKR-R-Runtime.exe", "SDL2.dll", "dxcompiler.dll", "dxil.dll"):
                assert not (root / name).exists()
            print(
                "PASS: flat-to-private layout migration retired only previously owned root binaries; portable/offline saves retained"
            )
        print(
            "PASS: complete signed package, parent retirement, Unicode/space paths, actual runtime self-test, portable/offline-save preservation and committed backup"
        )
    return 0


if __name__ == "__main__":
    sys.exit(main())
