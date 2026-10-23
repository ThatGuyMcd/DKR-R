#!/usr/bin/env python3
"""Package the tested build tree as a macOS app and ZIP.

Ad-hoc signed by default. --notarize signs with a Developer ID
(DKR_MAC_SIGN_IDENTITY), notarizes with a notarytool keychain profile
(DKR_MAC_NOTARY_PROFILE), staples, and re-zips.
"""
import argparse
import hashlib
import json
import time
import os
from pathlib import Path
import plistlib
import re
import shutil
import subprocess
import tempfile
import zipfile
import scan_for_game_assets as asset_scan


ROOT = Path(__file__).resolve().parents[1]


def run(*args):
    return subprocess.check_output([str(a) for a in args], text=True).strip()


def system_library(name):
    return name.startswith(("/System/Library/", "/usr/lib/"))


def load_commands(path):
    return run("otool", "-l", path)


def rpaths(path):
    return re.findall(r"cmd LC_RPATH\s+cmdsize \d+\s+path (.*?) \(offset", load_commands(path))


def dependencies(path):
    names = [line.strip().split(" (compatibility", 1)[0]
             for line in run("otool", "-L", path).splitlines()[1:]]
    if path.suffix == ".dylib":
        names = names[1:]  # LC_ID_DYLIB, not a dependency
    return names


def resolve(name, source, executable):
    def expand(value):
        return Path(value.replace("@loader_path", str(source.parent))
                    .replace("@executable_path", str(executable.parent)))
    candidates = ([expand(p) / name[len("@rpath/"):]
                   for p in rpaths(source) + rpaths(executable)]
                  if name.startswith("@rpath/") else [expand(name)])
    for candidate in candidates:
        if candidate.is_absolute() and candidate.is_file():
            return candidate.resolve()
    raise RuntimeError(f"Cannot resolve {name} required by {source}")


def sign(app, images, identity, scratch):
    """Sign nested images, then seal the bundle. identity "-" is ad hoc."""
    options = ["--force", "--sign", identity]
    if identity != "-":
        # The hardened runtime is required for notarization; the recompiled
        # game still needs to map JIT pages, the helpers and dylibs do not.
        entitlements = scratch / "DKR-R.entitlements"
        with entitlements.open("wb") as handle:
            plistlib.dump({"com.apple.security.cs.allow-jit": True}, handle)
        options += ["--options", "runtime", "--timestamp"]
    main = app / "Contents/MacOS/DKR-R"
    for destination in images:
        if destination != main:
            run("codesign", *options, destination)
    if identity != "-":
        options += ["--entitlements", entitlements]
    run("codesign", *options, app)
    run("codesign", "--verify", "--deep", "--strict", app)


def archive(stage, destination):
    # Plain ditto stores com.apple.provenance as ._ entries. Archive Utility
    # extracts those as files next to the bundle's symlinks, and Gatekeeper
    # then reports the app as damaged.
    destination.unlink(missing_ok=True)
    run("ditto", "-c", "-k", "--norsrc", "--noextattr", "--noacl", "--keepParent", stage, destination)
    with zipfile.ZipFile(destination) as handle:
        junk = [n for n in handle.namelist()
                if re.search(r"(^|/)(\._|\.DS_Store$|__MACOSX/)", n)]
    if junk:
        raise RuntimeError(f"Archive contains metadata files: {junk[:5]}")


def notarize(submission, app, profile):
    # notarytool can exit non-zero for a rejected upload; read its JSON either way.
    result = subprocess.run(["xcrun", "notarytool", "submit", str(submission), "--keychain-profile",
                             profile, "--wait", "--timeout", "1h", "--output-format", "json"],
                            capture_output=True, text=True)
    try:
        status = json.loads(result.stdout)
    except json.JSONDecodeError:
        raise RuntimeError(f"notarytool submit failed: {result.stderr.strip() or result.stdout.strip()}")
    print(f"Notarization {status.get('id')}: {status.get('status')}")
    if status.get("status") != "Accepted":
        if status.get("id"):
            print(subprocess.run(["xcrun", "notarytool", "log", status["id"], "--keychain-profile", profile],
                                 capture_output=True, text=True).stdout)
        raise RuntimeError("Notarization was not accepted")
    run("xcrun", "stapler", "staple", app)
    run("xcrun", "stapler", "validate", app)
    assessment = subprocess.run(["spctl", "-a", "-vv", "-t", "exec", str(app)],
                                capture_output=True, text=True)
    print(assessment.stderr.strip())
    if assessment.returncode != 0 or "source=Notarized Developer ID" not in assessment.stderr:
        raise RuntimeError("Gatekeeper did not accept the notarized app")


def release_number(version):
    """major.minor.patch for CFBundleShortVersionString, or None."""
    match = re.match(r"\d+\.\d+\.\d+", version)
    return match[0] if match else None


def bundle_version(build, configured_build, override=None):
    """Reserve a monotonically increasing bundle version, even within a minute.

    Encode minutes since Unix epoch as Apple's four/two/two digit components.
    The counter is locked independently because packaging can run standalone.
    Explicit release/CI versions bypass allocation.
    """
    if override is not None:
        if not re.fullmatch(r"[1-9][0-9]{0,3}(?:\.[0-9]{1,2}){0,2}", override):
            raise ValueError("bundle version must be positive N[.N[.N]] (4/2/2 digits)")
        return override
    import fcntl  # macOS-only; keep pure metadata helpers importable on Windows.
    with (build / ".macos-package-sequence").open("a+") as counter:
        fcntl.flock(counter, fcntl.LOCK_EX)
        counter.seek(0)
        previous = int(counter.read() or "0")
        sequence = max(int(time.time() // 60), previous + 1, configured_build)
        if sequence >= 100000000:
            raise ValueError("automatic bundle version exhausted; specify --build-number")
        counter.seek(0)
        counter.truncate()
        counter.write(str(sequence))
        counter.flush()
    return f"{sequence // 10000}.{sequence // 100 % 100}.{sequence % 100}"


def package(build, version, build_number=None, identity="-", notary_profile=None):
    metadata = json.loads((build / "bin/Release/dkr-release.json").read_text())
    if metadata["version"] != version:
        raise RuntimeError("Package version differs from configured build; rebuild with DKR_RELEASE_VERSION first")
    bundle_build = bundle_version(build, metadata["build_number"], build_number)
    executable = build / "bin/Release/DKR-R"
    helpers = executable.parent / "libexec/dkr-r"
    required = [executable, helpers / "DKR-R-ModWorker", helpers / "DKR-R-InputHost"]
    for path in required:
        if not path.is_file():
            raise RuntimeError(f"Missing executable: {path}; complete the macOS build first")
    # Single-architecture only: Homebrew bottles are thin and otool's fat
    # listing is not parsed.
    architectures = run("lipo", "-archs", executable).split()
    if len(architectures) != 1 or architectures[0] not in ("arm64", "x86_64"):
        raise RuntimeError(f"Unsupported architectures: {architectures}")
    architecture = architectures[0]
    name = f"DKR-R-{version.replace('+', '-')}-macOS-{architecture}"
    dist = ROOT / "dist"
    dist.mkdir(exist_ok=True)
    # Assemble in a private temporary directory; leave existing releases intact.
    with tempfile.TemporaryDirectory(prefix="dkr-package-", dir=dist) as temporary:
        stage = Path(temporary) / name
        app = stage / "DKR-R.app"
        contents = app / "Contents"
        macos = contents / "MacOS"
        resources = contents / "Resources"
        frameworks = contents / "Frameworks"
        for directory in (macos, resources, frameworks):
            directory.mkdir(parents=True, exist_ok=True)
        shutil.copytree(executable.parent / "assets", resources / "assets")
        (macos / "libexec/dkr-r").mkdir(parents=True)
        # SDL uses Resources as the base path of a bundle. Keep helper code in
        # MacOS, with the same trusted libexec lookup used by development builds.
        (resources / "libexec").symlink_to("../MacOS/libexec")
        images = {}
        contexts = {}
        for source in required:
            destination = macos / source.relative_to(executable.parent)
            shutil.copy2(source, destination)
            images[source.resolve()] = destination
            contexts[source.resolve()] = source.resolve()

        def library(source, owner):
            source = source.resolve()
            if source not in images:
                # Separate pinned and system SDL3 builds even if names match.
                tag = hashlib.sha256(source.read_bytes()).hexdigest()[:12]
                destination = frameworks / f"{tag}-{source.name}"
                shutil.copy2(source, destination)
                os.chmod(destination, 0o755)  # Homebrew installs dylibs read-only
                images[source] = destination
                contexts[source] = owner
            return images[source]

        processed = set()
        minimum = (12, 0)
        while pending := [p for p in images if p not in processed]:
            for source in pending:
                destination, owner = images[source], contexts[source]
                if run("lipo", "-archs", source).split() != [architecture]:
                    raise RuntimeError(f"{source} is not a thin {architecture} image")
                commands = load_commands(source)
                versions = re.findall(r"\bminos (\d+\.\d+(?:\.\d+)?)", commands)
                versions += re.findall(r"cmd LC_VERSION_MIN_MACOSX\s+cmdsize \d+\s+version (\d+\.\d+(?:\.\d+)?)", commands)
                for value in versions:
                    minimum = max(minimum, tuple(int(n) for n in value.split('.')))
                for dependency in dependencies(source):
                    if system_library(dependency):
                        continue
                    target = library(resolve(dependency, source, owner), owner)
                    run("install_name_tool", "-change", dependency,
                        f"@rpath/{target.name}", destination)
                # SDL2-compat loads SDL3 with dlopen, so otool cannot discover it.
                if b"@loader_path/libSDL3.dylib" in source.read_bytes():
                    sdl3 = Path(run("brew", "--prefix", "sdl3")) / "lib/libSDL3.dylib"
                    target = library(sdl3, owner)
                    alias = frameworks / "libSDL3.dylib"
                    if not alias.exists():
                        alias.symlink_to(target.name)
                for old in set(rpaths(source)):
                    run("install_name_tool", "-delete_rpath", old, destination)
                relative = os.path.relpath(frameworks, destination.parent)
                run("install_name_tool", "-add_rpath", f"@loader_path/{relative}", destination)
                if destination.suffix == ".dylib":
                    run("install_name_tool", "-id", f"@rpath/{destination.name}", destination)
                processed.add(source)

        # Include the full 1024 px Retina representation from the icon artwork.
        iconset = Path(temporary) / "DKR-R.iconset"
        iconset.mkdir()
        for size in (16, 32, 128, 256, 512):
            for scale, suffix in ((1, ""), (2, "@2x")):
                run("sips", "-z", size * scale, size * scale, ROOT / "assets/ui/Icons/DKR-R-Icon.png",
                    "--out", iconset / f"icon_{size}x{size}{suffix}.png")
        run("iconutil", "-c", "icns", iconset, "-o", resources / "DKR-R.icns")
        plist = dict(CFBundleExecutable="DKR-R", CFBundleName="DKR-R",
                     CFBundleIdentifier="io.github.thatguymcd.dkr-r",
                     CFBundlePackageType="APPL", CFBundleVersion=bundle_build,
                     CFBundleShortVersionString=release_number(version),
                     CFBundleIconFile="DKR-R",
                     LSMinimumSystemVersion=".".join(map(str, minimum)),
                     NSHighResolutionCapable=True)
        with (contents / "Info.plist").open("wb") as handle:
            plistlib.dump(plist, handle)
        notices = resources / "Licenses"
        notices.mkdir()
        for source, label in [(ROOT / "LICENSE.md", "DKR-R-LICENSE.md"),
                             (ROOT / "THIRD_PARTY.md", "DKR-R-THIRD_PARTY.md"),
                             (ROOT / "runtime-recomp/COPYING-NOTICE.md", "DKR-R-COPYING-NOTICE.md"),
                             (ROOT / "extern/rt64/LICENSE", "RT64-LICENSE.txt"),
                             (ROOT / "extern/rt64/src/contrib/imgui/LICENSE.txt", "Dear-ImGui-LICENSE.txt"),
                             (ROOT / "extern/n64-modern-runtime/COPYING", "N64ModernRuntime-COPYING.txt"),
                             (ROOT / "extern/n64-modern-runtime/N64Recomp/LICENSE", "N64Recomp-LICENSE.txt"),
                             (ROOT / "extern/sdl3/LICENSE.txt", "SDL3-LICENSE.txt")]:
            shutil.copy2(source, notices / label)
        shutil.copytree(ROOT / "packaging/licenses", notices / "additional")
        # Include licences for the Homebrew SDL libraries, where used.
        for formula in ("sdl2", "sdl3"):
            prefix = Path(run("brew", "--prefix", formula))
            for source in prefix.glob("*LICENSE*"):
                shutil.copy2(source, notices / f"{formula}-{source.name}")
        asset_scan.ROOT = stage
        asset_scan.ALLOW_LARGE = {p.relative_to(stage).as_posix() for p in images.values()}
        if asset_scan.main():
            raise RuntimeError("Package contains prohibited game data")
        for destination in images.values():
            for dependency in dependencies(destination):
                if system_library(dependency):
                    continue
                if not (dependency.startswith("@rpath/") and (frameworks / dependency[len("@rpath/"):]).is_file()):
                    raise RuntimeError(f"Unbundled dependency: {dependency}")
        run("xattr", "-crs", stage)
        sign(app, images.values(), identity, Path(temporary))
        with tempfile.TemporaryDirectory(prefix="dkr-package-pak-") as profile:
            run(macos / "DKR-R", "--self-test-pak", profile)
        notarized = notary_profile is not None
        output = dist / f"{name}{'-notarized' if notarized else ''}.zip"
        if output.exists():
            raise RuntimeError(f"Release already exists: {output}; move it aside or use DKR_RELEASE_VERSION")
        zipped = Path(temporary) / output.name
        if notarized:
            submission = Path(temporary) / "notary-submission.zip"
            archive(stage, submission)
            notarize(submission, app, notary_profile)
        archive(stage, zipped)
        zipped.rename(output)
        if notarized:
            print(f"Created {output}\nDeveloper ID signed, notarized and stapled.")
        else:
            print(f"Created {output}\nAd-hoc signed for testers; not Developer ID signed or notarized.")
        # Homebrew bottles are built for the host's macOS, so the bundled SDL
        # libraries set the floor; the app will not open on anything older.
        print(f"Requires macOS {plist['LSMinimumSystemVersion']} or newer (set by the bundled Homebrew SDL libraries).")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("build", type=Path)
    parser.add_argument("version")
    parser.add_argument("--build-number", default=os.environ.get("DKR_MAC_BUNDLE_VERSION"),
                        help="Explicit CFBundleVersion; otherwise reserve a distinct increasing build")
    parser.add_argument("--notarize", action="store_true",
                        help="Developer ID sign (DKR_MAC_SIGN_IDENTITY), notarize with a notarytool "
                             "keychain profile (DKR_MAC_NOTARY_PROFILE) and staple")
    args = parser.parse_args()
    if release_number(args.version) is None or not re.fullmatch(r"[A-Za-z0-9._+-]+", args.version):
        parser.error("version must start with a numeric major.minor.patch")
    identity, profile = "-", None
    if args.notarize:
        identity = os.environ.get("DKR_MAC_SIGN_IDENTITY", "")
        profile = os.environ.get("DKR_MAC_NOTARY_PROFILE", "")
        if not identity or identity == "-" or not profile:
            parser.error("--notarize needs DKR_MAC_SIGN_IDENTITY and DKR_MAC_NOTARY_PROFILE")
    package(args.build.resolve(), args.version, args.build_number, identity, profile)
