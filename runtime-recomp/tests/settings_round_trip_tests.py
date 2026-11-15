"""Round-trip the settings file through the real LoadSettings and SaveSettings.

Usage: settings_round_trip_tests.py <DKR-R executable> <fixture directory>

The executable's --self-test-settings <dir> loads <dir>/dkr-port-settings.ini
and writes it back. Each golden must come back byte for byte, and the file of
non-default values must carry exactly the keys the all-defaults file does (plus
the keys that are only written conditionally), so a key that is written but
never read, or the reverse, shows up as a diff here.
"""

import filecmp
import pathlib
import shutil
import subprocess
import sys
import tempfile

SETTINGS_FILE = "dkr-port-settings.ini"
# Written only when set (eq_custom, launcher_theme_animate) or in some builds
# (online_experimental_adventure).
CONDITIONAL_KEYS = {"eq_custom", "launcher_theme_animate", "online_experimental_adventure"}
# Written as a constant, or clamped to a single value, so no input can change them.
CONSTANT_KEYS = {
    "settings_version",
    "settings_complete",
    "modern_scenery_settings_version",
    "online_synchronization",
    "online_experimental_rollback",
    "online_automatic_backend",
    "online_save_policy",
    "lava_lighting_mode",
}


def run_round_trip(executable: str, source: pathlib.Path | None, workdir: pathlib.Path) -> pathlib.Path:
    workdir.mkdir()
    if source is not None:
        shutil.copyfile(source, workdir / SETTINGS_FILE)
    result = subprocess.run(
        [executable, "--self-test-settings", str(workdir)],
        capture_output=True,
        text=True,
        timeout=60,
        check=False,
    )
    if result.returncode != 0:
        sys.exit(f"--self-test-settings failed ({result.returncode}):\n{result.stderr}")
    return workdir / SETTINGS_FILE


def read(path: pathlib.Path) -> dict[str, str]:
    return dict(line.split("=", 1) for line in path.read_text().splitlines() if "=" in line)


def main() -> int:
    if len(sys.argv) != 3:
        # The dependency-free tooling suite has no game executable; CTest
        # (DKRSettingsRoundTrip) supplies it.
        print("The settings round trip needs the game executable; CTest runs it as DKRSettingsRoundTrip.")
        return 0
    executable, fixtures = sys.argv[1], pathlib.Path(sys.argv[2])
    failures = []
    with tempfile.TemporaryDirectory() as scratch:
        scratch_path = pathlib.Path(scratch)
        for name in ("defaults.ini", "non-default.ini"):
            golden = fixtures / name
            written = run_round_trip(executable, golden, scratch_path / name)
            if not filecmp.cmp(golden, written, shallow=False):
                diff = subprocess.run(["diff", str(golden), str(written)], capture_output=True, text=True, check=False)
                failures.append(f"{name}: save(load(golden)) differs from the golden\n{diff.stdout}")
        # No file at all: the second save of a fresh install is the defaults
        # golden. The first save is not a fixed point: the default analogue
        # bindings share a source with Z and Accelerate, and loading them in
        # file order unbinds the earlier action.
        first = run_round_trip(executable, None, scratch_path / "fresh")
        second = run_round_trip(executable, first, scratch_path / "fresh-again")
        if not filecmp.cmp(fixtures / "defaults.ini", second, shallow=False):
            failures.append("a fresh install no longer settles on defaults.ini after two saves")
    defaults = read(fixtures / "defaults.ini")
    other = read(fixtures / "non-default.ini")
    for key in sorted((defaults.keys() ^ other.keys()) - CONDITIONAL_KEYS):
        failures.append(f"key only in one golden: {key}")
    # Every key must really be exercised: a value that equals the default
    # proves nothing about whether the key is read.
    for key, value in other.items():
        if value == defaults.get(key) and key not in CONSTANT_KEYS and not key.endswith("_waveform"):
            failures.append(f"non-default.ini leaves {key} at its default")
    if failures:
        print("\n".join(failures), file=sys.stderr)
        return 1
    print(f"settings round trip OK ({len(defaults)} keys)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
