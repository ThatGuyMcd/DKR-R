"""Run every test for Track Lab.

The suites split by what they need. Two run on any Python 3.8+ because the
format, catalogue, AI-graph and validation modules deliberately avoid ``bpy``.
Two need Blender, because what they check is exactly the part Blender is in the
middle of.

    python tools/blender/run_tests.py
    python tools/blender/run_tests.py --blender "C:/Program Files/.../blender.exe"
    python tools/blender/run_tests.py --all      # every retail map, not a sample

Without a Blender to run, the Blender suites are reported as skipped rather than
failed, and the exit code still reflects the suites that did run.
"""

from __future__ import annotations

import argparse
import glob
import os
import platform
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.abspath(os.path.join(HERE, ".."))
TESTS = os.path.join(HERE, "tests")

PLAIN_SUITES = [
    "test_roundtrip.py", "test_validate.py", "test_geometry.py", "test_assets.py",
    "test_binary_format.py", "test_encoder.py", "test_header.py",
    "test_level_model_roundtrip.py", "test_level_model_edit.py",
    "test_level_model_layout.py", "test_header_template.py",
    "test_textures.py", "test_custom_textures.py", "test_rice_identity.py",
    "test_level_types.py", "test_skyboxes.py", "test_race_ai.py",
    "test_transparency.py", "test_water.py", "test_texture_scroll.py",
    "test_minimap.py", "test_music.py", "test_music_sequence.py",
    "test_midi_import.py", "test_checkpoint_order.py", "test_rom_extract.py",
]
BLENDER_SUITES = ["test_blender_roundtrip.py", "test_blender_operators.py",
                  "test_blender_waterfalls.py", "test_blender_minimap.py",
                  "test_blender_music.py", "test_blender_rom_assets.py"]


def find_blender(explicit=None):
    if explicit:
        return explicit if os.path.isfile(explicit) else None
    candidates = []
    if platform.system() == "Windows":
        for base in (os.environ.get("PROGRAMFILES", r"C:\Program Files"),):
            candidates += sorted(
                glob.glob(os.path.join(base, "Blender Foundation", "Blender *", "blender.exe")),
                reverse=True,
            )
    elif platform.system() == "Darwin":
        candidates.append("/Applications/Blender.app/Contents/MacOS/Blender")
    else:
        candidates += ["/usr/bin/blender", "/usr/local/bin/blender", "/snap/bin/blender"]
    for candidate in candidates:
        if os.path.isfile(candidate):
            return candidate
    from shutil import which
    return which("blender")


def run(label, command):
    print("=" * 70)
    print(label)
    print("=" * 70)
    completed = subprocess.run(command, cwd=REPO_ROOT)
    print()
    return completed.returncode


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--blender", default=None, help="path to the Blender executable")
    parser.add_argument("--all", action="store_true",
                        help="test every retail map rather than a sample")
    parser.add_argument("--skip-blender", action="store_true")
    args = parser.parse_args(argv)

    results = {}

    for suite in PLAIN_SUITES:
        results[suite] = run(suite, [sys.executable, os.path.join(TESTS, suite)])

    if args.skip_blender:
        for suite in BLENDER_SUITES:
            results[suite] = None
    else:
        blender = find_blender(args.blender)
        if blender is None:
            print("no Blender found; skipping %s" % ", ".join(BLENDER_SUITES))
            print("pass --blender <path> to run them")
            print()
            for suite in BLENDER_SUITES:
                results[suite] = None
        else:
            print("using Blender: %s" % blender)
            print()
            for suite in BLENDER_SUITES:
                command = [
                    blender, "--background", "--factory-startup",
                    "--python", os.path.join(TESTS, suite),
                ]
                if args.all and suite == "test_blender_roundtrip.py":
                    command += ["--", "--all"]
                results[suite] = run(suite, command)

    print("=" * 70)
    print("summary")
    print("=" * 70)
    failed = 0
    for suite in PLAIN_SUITES + BLENDER_SUITES:
        code = results.get(suite)
        if code is None:
            print("  SKIP  %s" % suite)
        elif code == 0:
            print("  PASS  %s" % suite)
        else:
            print("  FAIL  %s (exit %d)" % (suite, code))
            failed += 1
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
