"""Capture the real shared Play UI using its authenticated disposable profile.

Requires a production runtime and its complete package, rather than host-only
tests. No existing profile is opened or changed. Output stays outside source.
"""

import argparse
import json
import os
from pathlib import Path
import re
import secrets
import signal
import struct
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runtime", type=Path, required=True)
    parser.add_argument("--package", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--rom", type=Path)
    parser.add_argument("--size", choices=("small", "short", "deck", "desktop", "wide"), default="deck")
    parser.add_argument("--surface", choices=("launcher", "overlay"), default="launcher")
    parser.add_argument("--language", default="en")
    parser.add_argument("--sidebar", choices=("shoulders", "dpad"), default="shoulders")
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    profile = output / ("dkr-ui-navigation-check-" + secrets.token_hex(8))
    profile.mkdir()
    token = secrets.token_hex(32)
    (profile / "navigation-check-token").write_text(token + "\n")
    (profile / "crash-reports").mkdir()
    (profile / "crash-reports/consent.json").write_text(
        json.dumps({"schema": 1, "choice": 0, "dumps": False, "since": 0})
    )
    (profile / "dkr-port-settings.ini").write_text(
        "settings_version=9\nsettings_complete=1\nskip_launcher=0\nsession_mods=1\n"
        "skip_intro=1\nskip_intro_destination=1\npresentation_profile=1\n"
        "launcher_theme_animate=0\nlauncher_language=" + args.language + "\n"
        "sidebar_navigation=" + ("1" if args.sidebar == "dpad" else "0") + "\n"
    )
    if args.rom:
        assert args.rom.is_file(), args.rom
        (profile / "last-rom.txt").write_text(str(args.rom.resolve()) + "\n")
    env = os.environ.copy()
    env.update(
        DKR_LAUNCHER_UI_CHECK_TOKEN=token,
        DKR_OWNED_CHECK_LAYOUT_UI=args.size,
        DKR_OWNED_CHECK_PLAY_LAUNCH="1",
        DKR_OWNED_CHECK_LAYOUT_SURFACE=args.surface,
        SDL_AUDIODRIVER="dummy",
        APPDATA=str(output / "appdata"),
        LOCALAPPDATA=str(output / "localappdata"),
        XDG_CONFIG_HOME=str(output / "xdg"),
    )
    if os.name == "nt":
        startup = subprocess.STARTUPINFO()
        startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
        startup.wShowWindow = 0
        env["PATH"] = (
            str(args.package.resolve()) + os.pathsep + str(args.runtime.resolve().parent) + os.pathsep + env["PATH"]
        )
    else:
        startup = None
        env.update(APPIMAGE_EXTRACT_AND_RUN="1", LP_NUM_THREADS="2", LIBGL_ALWAYS_SOFTWARE="1")
    with (output / "process.log").open("wb") as log:
        proc = subprocess.Popen(
            [str(args.runtime.resolve()), "--config", str(profile)],
            cwd=args.package,
            env=env,
            stdout=log,
            stderr=subprocess.STDOUT,
            startupinfo=startup,
            start_new_session=os.name != "nt",
        )
        try:
            code = proc.wait(timeout=60)
        except subprocess.TimeoutExpired:
            if os.name == "nt":
                proc.terminate()
            else:
                os.killpg(proc.pid, signal.SIGINT)
            proc.wait(timeout=15)
            raise AssertionError("The isolated layout check did not finish") from None
    text = (profile / "logs/runtime.log").read_text(errors="replace")
    rows = []
    for match in re.finditer(r"\[play-launch\]\[isolated-check\] ([^\r\n]+)", text):
        rows.append({key: float(value) for key, value in re.findall(r"([a-z-]+)=([0-9.-]+)", match[1])})
    result = {
        "exit_code": code,
        "size": args.size,
        "surface": args.surface,
        "language": args.language,
        "sidebar": args.sidebar,
        "rom_ready": bool(args.rom),
        "geometry": rows,
    }
    (output / "RESULT.json").write_text(json.dumps(result, indent=2) + "\n")
    assert code == 0 and len(rows) == 2, result
    assert [row["step"] for row in rows] == [0, 1], result
    assert all(row["visible"] == 1 and row["height"] == 48 and row["parent-scroll"] == 0 for row in rows), result
    assert rows[0]["top"] == rows[1]["top"] and rows[0]["bottom"] == rows[1]["bottom"], result
    assert rows[1]["body-scroll"] == rows[1]["body-max"], result
    for row in rows:
        for edge in ("left", "right", "bottom"):
            assert abs(row["bar-" + edge] - row["panel-" + edge]) <= 1, result
        button_width = row["button-right"] - row["button-left"]
        assert 0 < button_width < row["available"] * 0.9, result
        assert abs(row["button-left"] + row["button-right"] - row["bar-left"] - row["bar-right"]) <= 2, result
    assert all(row["enabled"] == bool(args.rom) for row in rows), result
    if args.rom:
        assert rows[0]["focused"] == 1, result
    dimensions = {
        "small": (1024, 600),
        "short": (1280, 720),
        "deck": (1280, 800),
        "desktop": (1920, 1080),
        "wide": (2560, 1440),
    }
    for step in range(2):
        data = (profile / f"layout-{step}.bmp").read_bytes()
        assert data[:2] == b"BM" and struct.unpack_from("<I", data, 2)[0] == len(data)
        width, height = struct.unpack_from("<ii", data, 18)
        assert (width, abs(height)) == dimensions[args.size]
        offset = struct.unpack_from("<I", data, 10)[0]
        depth = struct.unpack_from("<H", data, 28)[0]
        assert depth in (24, 32)
        stride = ((width * depth + 31) // 32) * 4
        row = rows[step]
        # Read actual pixels in the former side/bottom gaps, away from the
        # border, curved corners, caption and focus ring.
        probes = (
            (row["bar-left"] + 3, row["bar-top"] + 4),
            (row["bar-right"] - 3, row["bar-top"] + 4),
            ((row["bar-left"] + row["bar-right"]) / 2, row["bar-bottom"] - 3),
        )
        for x, y in probes:
            pixel_row = abs(height) - 1 - int(y) if height > 0 else int(y)
            pixel = offset + stride * pixel_row + int(x) * (depth // 8)
            assert tuple(data[pixel : pixel + 3]) == (52, 37, 9), (step, x, y, result)
    assert "[boot][crash]" not in text, result
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
