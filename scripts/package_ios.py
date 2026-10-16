#!/usr/bin/env python3
"""Wrap the iOS device build as an ad-hoc-signed, sideloadable IPA."""
from __future__ import annotations

import json
import plistlib
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import zipfile
import zlib
from pathlib import Path

import scan_for_game_assets as asset_scan

ROOT = Path(__file__).resolve().parents[1]
SKY = (9, 169, 231, 255)
LICENSE_SOURCES = [
    (ROOT / "LICENSE.md", "DKR-R-LICENSE.md"),
    (ROOT / "THIRD_PARTY.md", "DKR-R-THIRD_PARTY.md"),
    (ROOT / "runtime-recomp/COPYING-NOTICE.md", "DKR-R-COPYING-NOTICE.md"),
    (ROOT / "extern/rt64/LICENSE", "RT64-LICENSE.txt"),
    (ROOT / "extern/rt64/src/contrib/imgui/LICENSE.txt", "Dear-ImGui-LICENSE.txt"),
    (ROOT / "extern/n64-modern-runtime/COPYING", "N64ModernRuntime-COPYING.txt"),
    (ROOT / "extern/n64-modern-runtime/N64Recomp/LICENSE", "N64Recomp-LICENSE.txt"),
    (ROOT / "extern/sdl2/LICENSE.txt", "SDL2-LICENSE.txt"),
]


def run(args: list[str], cwd: Path | None = None) -> str:
    result = subprocess.run(args, cwd=cwd, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if result.returncode != 0:
        raise RuntimeError(
            f"{' '.join(args)} failed ({result.returncode})\n{result.stdout}\n{result.stderr}"
        )
    return result.stdout


def find_app(build: Path) -> Path:
    matches = sorted(build.rglob("DKR-R.app"))
    apps = [path for path in matches if (path / "DKR-R").is_file() and (path / "Info.plist").is_file()]
    if len(apps) != 1:
        found = ", ".join(str(path) for path in matches) or "none"
        raise RuntimeError(f"Expected one flat DKR-R.app under {build}, found: {found}")
    app = apps[0]
    if (app / "Contents").exists():
        raise RuntimeError(f"{app} has a macOS Contents layout. The iOS bundle must be flat.")
    return app


def png_pixel(path: Path, x: int, y: int) -> tuple[int, int, int, int]:
    data = path.read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise RuntimeError(f"{path} is not a PNG")
    pos = 8
    width = height = None
    idat = b""
    color_type = None
    while pos + 8 <= len(data):
        length = struct.unpack(">I", data[pos : pos + 4])[0]
        kind = data[pos + 4 : pos + 8]
        chunk = data[pos + 8 : pos + 8 + length]
        if kind == b"IHDR":
            width, height, _, color_type = struct.unpack(">IIBB", chunk[:10])
        elif kind == b"IDAT":
            idat += chunk
        elif kind == b"IEND":
            break
        pos += 12 + length
    if color_type not in (2, 6) or width is None or height is None:
        raise RuntimeError(f"{path} must be 8-bit RGB or RGBA")
    if not (0 <= x < width and 0 <= y < height):
        raise RuntimeError(f"pixel {(x, y)} is outside {width}x{height}")
    channels = 4 if color_type == 6 else 3
    raw = zlib.decompress(idat)
    stride = width * channels + 1
    # ImageIO writes Sub, Up, and Paeth. Reading the stored bytes as pixels
    # makes an opaque corner look transparent.

    def paeth(left: int, up: int, up_left: int) -> int:
        estimate = left + up - up_left
        if abs(estimate - left) <= abs(estimate - up) and abs(estimate - left) <= abs(estimate - up_left):
            return left
        if abs(estimate - up) <= abs(estimate - up_left):
            return up
        return up_left

    previous = bytearray(width * channels)
    row = bytearray()
    for row_index in range(y + 1):
        filter_type = raw[row_index * stride]
        row = bytearray(raw[row_index * stride + 1 : (row_index + 1) * stride])
        for index, value in enumerate(row):
            left = row[index - channels] if index >= channels else 0
            up = previous[index]
            up_left = previous[index - channels] if index >= channels else 0
            if filter_type == 0:
                decoded = value
            elif filter_type == 1:
                decoded = value + left
            elif filter_type == 2:
                decoded = value + up
            elif filter_type == 3:
                decoded = value + ((left + up) // 2)
            elif filter_type == 4:
                decoded = value + paeth(left, up, up_left)
            else:
                raise RuntimeError(f"{path} row {row_index} uses PNG filter {filter_type}")
            row[index] = decoded & 255
        previous = row
    pixel = tuple(row[x * channels : (x + 1) * channels])
    if channels == 3:
        pixel = (*pixel, 255)
    return pixel  # type: ignore[return-value]


def make_icon(logo: Path, destination: Path, pixels: int) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    script = ROOT / "packaging/ios/MakeIcon.swift"
    run(["swift", str(script), str(logo), str(destination), str(pixels)])
    for point in ((0, 0), (pixels - 1, 0), (0, pixels - 1), (pixels - 1, pixels - 1)):
        pixel = png_pixel(destination, *point)
        if pixel != SKY:
            raise RuntimeError(f"{destination.name} corner {point} is {pixel}, expected opaque sky {SKY}")


def install_icon(app: Path, scratch: Path) -> None:
    logo = ROOT / "assets/ui/Icons/DKR-R-Icon.png"
    icon = scratch / "AppIcon.png"
    make_icon(logo, icon, 1024)
    catalog = scratch / "Assets.xcassets" / "AppIcon.appiconset"
    catalog.mkdir(parents=True)
    shutil.copy2(icon, catalog / "AppIcon.png")
    (catalog / "Contents.json").write_text(
        """{
  "images": [
    {"filename": "AppIcon.png", "idiom": "universal", "platform": "ios", "size": "1024x1024"}
  ],
  "info": {"author": "xcode", "version": 1}
}
""",
        encoding="utf-8",
    )
    partial = scratch / "ActoolPartial.plist"
    try:
        run([
            "xcrun", "actool", str(scratch / "Assets.xcassets"),
            "--compile", str(app),
            "--platform", "iphoneos",
            "--minimum-deployment-target", "18.0",
            "--target-device", "iphone",
            "--target-device", "ipad",
            "--app-icon", "AppIcon",
            "--output-partial-info-plist", str(partial),
        ])
    except RuntimeError as exc:
        print(f"actool failed, using classic PNG icons ({exc})", file=sys.stderr)
        install_classic_icons(app, scratch, logo)
        return
    if not (app / "Assets.car").is_file():
        raise RuntimeError("actool did not write Assets.car")
    info_path = app / "Info.plist"
    info = plistlib.loads(info_path.read_bytes())
    extra = plistlib.loads(partial.read_bytes()) if partial.is_file() else {}
    for key, value in extra.items():
        if key.startswith("CFBundleIcon"):
            info[key] = value
    info_path.write_bytes(plistlib.dumps(info))


def install_classic_icons(app: Path, scratch: Path, logo: Path) -> None:
    names = {
        "AppIcon60x60@2x.png": 120,
        "AppIcon60x60@3x.png": 180,
        "AppIcon76x76~ipad.png": 76,
        "AppIcon76x76@2x~ipad.png": 152,
        "AppIcon83.5x83.5@2x~ipad.png": 167,
    }
    for name, pixels in names.items():
        make_icon(logo, app / name, pixels)
    info_path = app / "Info.plist"
    info = plistlib.loads(info_path.read_bytes())
    primary = {
        "CFBundleIconFiles": ["AppIcon60x60"],
        "UIPrerenderedIcon": True,
    }
    info["CFBundleIcons"] = {"CFBundlePrimaryIcon": primary}
    info["CFBundleIcons~ipad"] = {
        "CFBundlePrimaryIcon": {
            "CFBundleIconFiles": ["AppIcon60x60", "AppIcon76x76", "AppIcon83.5x83.5"],
            "UIPrerenderedIcon": True,
        }
    }
    info_path.write_bytes(plistlib.dumps(info))
    del scratch


def stage_assets(app: Path, build: Path) -> None:
    copies = [
        (ROOT / "assets/ui/Icons/DKR-R-Logo.bmp", app / "assets/ui/Icons/DKR-R-Logo.bmp"),
        (ROOT / "assets/ui/Icons/DKR-R-Spinning-Icon.png", app / "assets/ui/Icons/DKR-R-Spinning-Icon.png"),
        (ROOT / "assets/ui/Icons/DKR-R-Short-Logo.png", app / "assets/ui/Icons/DKR-R-Short-Logo.png"),
        (ROOT / "assets/ui/Backgrounds/DKR-R-Launcher-Background.png",
         app / "assets/ui/Backgrounds/DKR-R-Launcher-Background.png"),
        (ROOT / "assets/controllers/gamecontrollerdb.txt",
         app / "assets/controllers/gamecontrollerdb.txt"),
    ]
    for source, dest in copies:
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, dest)
    filters = app / "assets/filters"
    if filters.exists():
        shutil.rmtree(filters)
    shutil.copytree(ROOT / "assets/filters", filters)
    shutil.copy2(build / "dkr-release.json", app / "dkr-release.json")


def stage_licenses(app: Path) -> None:
    notices = app / "Licenses"
    if notices.exists():
        shutil.rmtree(notices)
    notices.mkdir()
    missing = []
    for source, label in LICENSE_SOURCES:
        if not source.is_file():
            missing.append(str(source))
            continue
        shutil.copy2(source, notices / label)
    extra = ROOT / "packaging/licenses"
    if extra.is_dir():
        shutil.copytree(extra, notices / "additional")
    if missing:
        raise RuntimeError("Missing license text for the IPA: " + ", ".join(missing))


def compile_launch_screen(app: Path) -> None:
    compiled = app / "LaunchScreen.storyboardc"
    if compiled.exists():
        shutil.rmtree(compiled)
    run([
        "xcrun", "ibtool",
        "--errors", "--warnings", "--notices",
        "--minimum-deployment-target", "18.0",
        "--target-device", "iphone",
        "--target-device", "ipad",
        "--compile", str(compiled),
        str(ROOT / "packaging/ios/LaunchScreen.storyboard"),
    ])


def require_plist(app: Path, version: str, bundle_version: str) -> None:
    info = plistlib.loads((app / "Info.plist").read_bytes())
    expected = {
        "CFBundleIdentifier": "io.github.thatguymcd.dkrr.ios",
        "CFBundleShortVersionString": version,
        "CFBundleVersion": bundle_version,
        "CFBundleExecutable": "DKR-R",
        "MinimumOSVersion": "18.0",
        "UILaunchStoryboardName": "LaunchScreen",
    }
    for key, value in expected.items():
        if info.get(key) != value:
            raise RuntimeError(f"Info.plist {key} is {info.get(key)!r}, expected {value!r}")
    if info.get("CADisableMinimumFrameDurationOnPhone") is not True:
        raise RuntimeError("ProMotion is still capped: CADisableMinimumFrameDurationOnPhone is missing")
    if info.get("UIFileSharingEnabled") is not True:
        raise RuntimeError("File sharing is off, so ROMs cannot be dropped in Files")
    orientations = info.get("UISupportedInterfaceOrientations")
    if orientations != [
        "UIInterfaceOrientationLandscapeLeft",
        "UIInterfaceOrientationLandscapeRight",
    ]:
        raise RuntimeError(f"Phone orientations are {orientations!r}")


def sign_app(app: Path) -> None:
    entitlements = ROOT / "packaging/ios/entitlements.plist"
    command = [
        "codesign", "--force", "--sign", "-",
        "--entitlements", str(entitlements),
        "--timestamp=none",
        str(app),
    ]
    der = command[:-1] + ["--generate-entitlement-der", str(app)]
    try:
        run(der)
    except RuntimeError:
        run(command)
    run(["codesign", "--verify", "--deep", "--strict", str(app)])
    blob = run(["codesign", "-d", "--entitlements", "-", "--xml", str(app)])
    try:
        signed_entitlements = plistlib.loads(blob.encode("utf-8"))
    except (ValueError, plistlib.InvalidFileException) as exc:
        raise RuntimeError("Could not inspect the signed entitlements") from exc
    if signed_entitlements != {}:
        raise RuntimeError(f"Ad-hoc signature must have no entitlements: {signed_entitlements!r}")


def check_binary(app: Path) -> None:
    executable = app / "DKR-R"
    info = run(["file", str(executable)])
    if "arm64" not in info or "Mach-O" not in info:
        raise RuntimeError(f"DKR-R is not an arm64 Mach-O: {info.strip()}")
    if "x86_64" in info:
        raise RuntimeError(f"DKR-R is not device-only: {info.strip()}")
    build = run(["vtool", "-show-build", str(executable)])
    platforms = re.findall(r"^\s*platform\s+(\S+)\s*$", build, re.MULTILINE)
    if not platforms or any(platform not in {"IOS", "2"} for platform in platforms):
        raise RuntimeError(f"DKR-R is not an iOS device binary:\n{build}")
    minimums = re.findall(r"^\s*minos\s+(\S+)\s*$", build, re.MULTILINE)
    if len(minimums) != len(platforms) or any(value not in {"18.0", "18.0.0"} for value in minimums):
        raise RuntimeError(f"DKR-R minimum OS is not 18.0:\n{build}")


def reject_symlinks(app: Path) -> None:
    if app.is_symlink() or any(path.is_symlink() for path in app.rglob("*")):
        raise RuntimeError("The iOS bundle must not contain symbolic links")


def scan_app(app: Path) -> None:
    # Repository exclusions do not apply to distributable payloads. There are no
    # ZIP resources in this app, so reject archives without unpacking untrusted data.
    reject_symlinks(app)
    inspected = 0
    for path in app.rglob("*"):
        if not path.is_file():
            continue
        inspected += 1
        relative = path.relative_to(app).as_posix()
        if path.suffix.lower() in asset_scan.DENIED_SUFFIXES:
            raise RuntimeError(f"Denied game-data extension: {relative}")
        with path.open("rb") as stream:
            header = stream.read(4)
        if header in asset_scan.N64_HEADERS:
            raise RuntimeError(f"Nintendo 64 ROM header: {relative}")
        if zipfile.is_zipfile(path):
            raise RuntimeError(f"Unexpected archive in IPA payload: {relative}")
        if path.stat().st_size > asset_scan.MAX_SOURCE_SIZE and relative not in {"DKR-R", "Assets.car"}:
            raise RuntimeError(f"Unexplained file larger than 5 MiB: {relative}")
    if not inspected:
        raise RuntimeError("IPA payload is empty")


def write_ipa(app: Path, destination: Path) -> None:
    reject_symlinks(app)
    destination.parent.mkdir(parents=True, exist_ok=True)
    temporary = destination.with_suffix(".ipa.partial")
    if temporary.exists():
        temporary.unlink()
    with zipfile.ZipFile(temporary, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        for path in sorted(app.rglob("*")):
            if path.is_symlink() or not path.is_file():
                continue
            info = zipfile.ZipInfo(f"Payload/DKR-R.app/{path.relative_to(app).as_posix()}")
            mode = path.stat().st_mode & 0xFFFF
            info.external_attr = mode << 16
            info.compress_type = zipfile.ZIP_DEFLATED
            archive.writestr(info, path.read_bytes())
    with zipfile.ZipFile(temporary) as archive:
        names = archive.namelist()
    required = ["Payload/DKR-R.app/DKR-R", "Payload/DKR-R.app/Info.plist"]
    missing = [name for name in required if name not in names]
    if missing or any(not name.startswith("Payload/DKR-R.app/") for name in names):
        temporary.unlink(missing_ok=True)
        raise RuntimeError(f"IPA layout is wrong. Missing {missing}")
    temporary.replace(destination)


def release_number(version: str) -> str:
    match = re.fullmatch(r"([1-9][0-9]{0,3})\.(0|[1-9][0-9]?)\.(0|[1-9][0-9]?)(?:-rc[1-9][0-9]*)?", version)
    if match is None:
        raise RuntimeError(f"Invalid iOS release version: {version!r}; expected major.minor.patch[-rcN] (4/2/2 digits)")
    return ".".join(match.groups())


def bundle_version(version: str) -> str:
    return release_number(version)


def require_metadata(app: Path, build: Path, version: str) -> None:
    # The configure output is authoritative; reject a stale bundle copy rather
    # than relabeling it with metadata from a different build configuration.
    for path in (build / "dkr-release.json", app / "dkr-release.json"):
        if path.parent == app and not path.exists():
            continue
        try:
            metadata = json.loads(path.read_text(encoding="utf-8"))
        except (OSError, ValueError) as exc:
            raise RuntimeError(f"Missing or invalid release metadata: {path}") from exc
        if not isinstance(metadata, dict) or metadata.get("version") != version:
            raise RuntimeError(f"Package version differs from configured build in {path}; rebuild first")


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: package_ios.py BUILD_DIR VERSION", file=sys.stderr)
        return 2
    build = Path(sys.argv[1]).resolve()
    version = sys.argv[2]
    short = release_number(version)
    app = find_app(build)
    reject_symlinks(app)
    require_metadata(app, build, version)
    require_plist(app, short, bundle_version(version))
    check_binary(app)
    stage_assets(app, build)
    stage_licenses(app)
    compile_launch_screen(app)
    with tempfile.TemporaryDirectory(prefix="dkr-ios-icon-") as scratch:
        install_icon(app, Path(scratch))
    require_plist(app, short, bundle_version(short))
    scan_app(app)
    sign_app(app)
    ipa = ROOT / "dist" / f"DKR-R-{version}-iOS-arm64.ipa"
    write_ipa(app, ipa)
    print(f"Wrote {ipa}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"iOS package failed: {exc}", file=sys.stderr)
        raise SystemExit(1)
