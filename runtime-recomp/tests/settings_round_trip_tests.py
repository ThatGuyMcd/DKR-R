"""Round-trip the settings file through the real LoadSettings and SaveSettings.

Usage: settings_round_trip_tests.py <DKR-R executable> <fixture directory>

The executable's --self-test-settings <dir> loads <dir>/dkr-port-settings.ini
and writes it back. Each golden must come back byte for byte, and the file of
non-default values must carry exactly the keys the all-defaults file does (plus
the keys that are only written conditionally), so a key that is written but
never read, or the reverse, shows up as a diff here.
"""

import difflib
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
    "controller_binding_version",
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


def compare(expected: pathlib.Path, actual: pathlib.Path) -> str | None:
    """Return None when the files match, else a description of the difference.

    The game writes through a text-mode stream, so Windows produces CRLF while
    the goldens are checked out LF (.gitattributes). Line endings are therefore
    compared after normalising, but each file must still use one ending
    throughout: a mix would mean the writer changed, not the platform.
    """
    texts = []
    for path in (expected, actual):
        raw = path.read_bytes()
        crlf = raw.count(b"\r\n")
        if crlf not in (0, raw.count(b"\n")):
            return f"{path.name}: mixed LF and CRLF line endings"
        texts.append(raw.replace(b"\r\n", b"\n").decode().splitlines(keepends=True))
    if texts[0] == texts[1]:
        return None
    return "".join(difflib.unified_diff(texts[0], texts[1], "golden", "written"))


def read(path: pathlib.Path) -> dict[str, str]:
    return dict(line.split("=", 1) for line in path.read_text().splitlines() if "=" in line)


# The earlier action of each pair that shares a default controller source. The
# v1.0.14 and v1.0.15 reload unbound all six for every player.
RELOAD_UNBOUND = ("z", "c_up", "c_down", "c_left", "c_right", "accelerate")


def rewrite(golden: pathlib.Path, target: pathlib.Path, changes: dict[str, str | None]) -> pathlib.Path:
    """Write `golden` to `target` with some values replaced, or dropped when None."""
    lines = []
    for line in golden.read_text().splitlines():
        key = line.split("=", 1)[0]
        if key not in changes:
            lines.append(line)
        elif changes[key] is not None:
            lines.append(f"{key}={changes[key]}")
    target.write_text("\n".join(lines) + "\n")
    return target


def reload_damage(players: range = range(1, 5)) -> dict[str, str | None]:
    return {f"player{player}.controller_binding.{action}": "-1" for player in players for action in RELOAD_UNBOUND}


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
            difference = compare(golden, written)
            if difference:
                failures.append(f"{name}: save(load(golden)) differs from the golden\n{difference}")
        # No file at all: a fresh install writes the defaults golden at once,
        # and reloading that settles on the same bytes. (The shared default
        # controller sources used to be unbound by the reload.)
        first = run_round_trip(executable, None, scratch_path / "fresh")
        if compare(fixtures / "defaults.ini", first):
            failures.append("a fresh install no longer saves defaults.ini on the first save")
        # A profile as v1.0.14 and v1.0.15 left it (no version key, the six
        # bindings unbound for every player) comes back as the defaults.
        damaged = reload_damage() | {"controller_binding_version": None}
        source = rewrite(fixtures / "defaults.ini", scratch_path / "damaged.ini", damaged)
        written = run_round_trip(executable, source, scratch_path / "damaged")
        difference = compare(fixtures / "defaults.ini", written)
        if difference:
            failures.append(f"a profile unbound by the old reload is not repaired\n{difference}")
        # Player 1 uses the analogue driving layout (Z on the left shoulder,
        # the pedals on the triggers): Z and Accelerate are left as they are
        # and only the C buttons come back. The other players get all six.
        layout = {
            "player1_analogue_enabled": "1",
            "player1.controller_binding.z": "9",
            "player1.controller_binding.l": "-1",
        }
        source = rewrite(fixtures / "defaults.ini", scratch_path / "analogue.ini", damaged | layout)
        written = run_round_trip(executable, source, scratch_path / "analogue")
        kept = layout | {"player1.controller_binding.accelerate": "-1"}
        expected = rewrite(fixtures / "defaults.ini", scratch_path / "analogue-expected.ini", kept)
        difference = compare(expected, written)
        if difference:
            failures.append(f"the analogue driving layout is not left alone by the repair\n{difference}")
        # The same bindings in a file that carries the key are the player's
        # own choice and are written back as they are.
        source = rewrite(fixtures / "defaults.ini", scratch_path / "chosen.ini", reload_damage())
        written = run_round_trip(executable, source, scratch_path / "chosen")
        difference = compare(source, written)
        if difference:
            failures.append(f"bindings unbound under the current key were changed\n{difference}")
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
