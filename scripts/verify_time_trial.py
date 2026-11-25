#!/usr/bin/env python3
"""Verify a DKR-R replay with an isolated profile and an exact replay package."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import sys

from regression import digest as file_digest
from regression import execute

MAX_REPLAY_BYTES = 1024 * 1024
MAX_ADVENTURE_BYTES = 3 * 1024 * 1024
ADVENTURE_SPLIT_RULES = {"adventure-us80-5", "adventure-us80-6", "adventure-us80-7"}
ADVENTURE_RULES = {
    "adventure-us80-1",
    "adventure-us80-2",
    "adventure-us80-3",
    "adventure-us80-4",
} | ADVENTURE_SPLIT_RULES
CUSTOM_LAP_RULES = "custom-time-trial-us80-2"
CUSTOM_RULES = {"custom-time-trial-us80-1", CUSTOM_LAP_RULES}
ADVENTURE_PASS = re.compile(
    r"^\[time-trial\] PASS update=(\d+) Adventure objective and gameplay checkpoints verified\.$", re.M
)
ADVENTURE_START = re.compile(r"^\[adventure\] category=([a-z0-9-]+)$", re.M)
ADVENTURE_CHECKPOINT = re.compile(r"^\[adventure\] tick=(\d+) clock=(\d+) hash=([0-9a-f]{64})$", re.M)
ADVENTURE_SPLITS = re.compile(r"^\[adventure\] splits=(\[.*\])$", re.M)
CUSTOM_TARGET = re.compile(
    r"^\[time-trial\] track=([A-Za-z0-9_-]+) hash=([0-9a-f]{64}) vehicle=(car|hovercraft|plane)$", re.M
)
PASS = re.compile(r"^\[time-trial\] PASS update=(\d+) Finish, laps and gameplay checkpoints verified\.$", re.M)
SPLITS = re.compile(r"^\[time-trial\] clock=(\d+) splits=(\[\d+(?:,\d+){0,4}\])$", re.M)
REJECT = re.compile(r"^\[time-trial\] FAIL update=\d+ ", re.M)
COURSE = re.compile(r"^\[time-trial\] course=([a-z0-9-]+) vehicle=(car|hovercraft|plane)$", re.M)
COURSE_LABEL = re.compile(r"^\[time-trial\] ([^/\r\n]+) / [^/\r\n]+ / (Car|Hovercraft|Plane) / US v1\.[01]$", re.M)
ROM_REVISION = re.compile(r"^\[time-trial\] rom=(77|80)$", re.M)
CHECKPOINT = re.compile(r"^\[time-trial\] tick=(\d+) clock=(\d+) lap=(\d+) checkpoint=\d+ hash=([0-9a-f]{64})$", re.M)


def replay_course(log: str) -> dict[str, str]:
    targets = COURSE.findall(log)
    labels = [(name.lower().replace(" ", "-"), vehicle.lower()) for name, vehicle in COURSE_LABEL.findall(log)]
    if len(targets) > 1 or len(labels) > 1 or (targets and labels and targets != labels):
        raise ValueError("The verifier reported inconsistent race targets.")
    route = targets or labels
    return {"course": route[0][0], "vehicle": route[0][1]} if route else {}


class InvalidReplay(ValueError):
    """The game deterministically rejected the submitted replay."""


def replay_metadata(evidence: bytes) -> dict:
    if len(evidence) < 12 or not evidence.startswith(b"DKRREPL2"):
        raise InvalidReplay("Expected a supported DKR-R replay.")
    length = int.from_bytes(evidence[8:12], "little")
    if not 0 < length <= 65536 or len(evidence) < 12 + length:
        raise InvalidReplay("Invalid replay metadata length.")
    try:
        metadata = json.loads(evidence[12 : 12 + length])
    except (ValueError, RecursionError) as failure:
        raise InvalidReplay("Invalid replay metadata.") from failure
    if not isinstance(metadata, dict):
        raise InvalidReplay("Invalid replay metadata.")
    return metadata


def replay_rom_for(metadata: dict, rom_v11: Path, rom_v10: Path | None) -> Path:
    revision = metadata.get("rom")
    if revision == 80:
        return rom_v11
    if revision == 77 and rom_v10 is not None:
        return rom_v10
    if revision == 77:
        raise RuntimeError("The verifier needs its US v1.0 ROM to process this replay.")
    raise InvalidReplay("The replay's US ROM revision is unsupported.")


def adventure_result(log: str, metadata: dict) -> dict:
    starts = ADVENTURE_START.findall(log)
    passes = ADVENTURE_PASS.findall(log)
    rows = ADVENTURE_CHECKPOINT.findall(log)
    if starts != [metadata["category"]] or len(passes) != 1 or not rows or rows[-1][0] != passes[0]:
        raise ValueError("The verifier did not reproduce the Adventure objective.")
    update, clock, digest = rows[-1]
    proof = {
        "category": starts[0],
        "updates": int(update),
        "clockTicks": int(clock),
        "checkpoints": [[int(frame), checkpoint] for frame, _, checkpoint in rows],
        "finalCheckpoint": digest,
    }
    if metadata.get("ruleset") in ADVENTURE_SPLIT_RULES:
        splits = ADVENTURE_SPLITS.findall(log)
        if len(splits) != 1 or json.loads(splits[0]) != metadata.get("splits"):
            raise ValueError("The verifier did not reproduce the automatic Adventure splits.")
        proof["splits"] = json.loads(splits[0])
    return proof


def runtime_binary(runtime: Path) -> Path:
    inner = runtime.parent / "libexec" / "dkr-r" / "DKR-R-Runtime.exe"
    return inner if runtime.name == "DKR-R.exe" and inner.is_file() else runtime


def verification_environment(output: Path, xvfb: bool) -> dict[str, str]:
    env = {key: value for key, value in os.environ.items() if not key.startswith("DKR_")}
    if os.name == "posix":
        home = output / "home"
        home.mkdir(mode=0o700)
        # The service masks /home with ProtectHome. Libraries may still probe
        # the account home even when --config points to a different directory.
        env["HOME"] = str(home)
        for name, directory in (
            ("XDG_CONFIG_HOME", ".config"),
            ("XDG_CACHE_HOME", ".cache"),
            ("XDG_DATA_HOME", ".local/share"),
            ("XDG_STATE_HOME", ".local/state"),
            ("XDG_RUNTIME_DIR", ".runtime"),
        ):
            location = home / directory
            location.mkdir(mode=0o700, parents=True)
            env[name] = str(location)
    env["SDL_AUDIODRIVER"] = "dummy"
    if xvfb:
        env["LIBGL_ALWAYS_SOFTWARE"] = "1"
        # Mesa otherwise sizes its software renderer pools from the host CPU
        # count, which can exhaust the verifier service's TasksMax limit.
        env["LP_NUM_THREADS"] = "4"
    return env


def verify(
    runtime: Path,
    rom: Path,
    replay: Path,
    output: Path,
    timeout: int,
    xvfb: bool,
    isolate_network: bool = False,
    track_package: Path | None = None,
    emit_ghost: bool = False,
) -> dict:
    runtime, rom = runtime.resolve(strict=True), rom.resolve(strict=True)
    binary = runtime_binary(runtime)
    runtime_sha256 = file_digest(binary)
    with replay.open("rb") as source:
        evidence = source.read(MAX_ADVENTURE_BYTES + 1)
    metadata = replay_metadata(evidence)
    rules = metadata.get("ruleset")
    if not isinstance(rules, str):
        raise InvalidReplay("Invalid replay ruleset.")
    if len(evidence) > (MAX_ADVENTURE_BYTES if rules in ADVENTURE_RULES else MAX_REPLAY_BYTES):
        raise InvalidReplay("The replay exceeded its recording limit.")
    if (rules in CUSTOM_RULES) != (track_package is not None):
        raise InvalidReplay("Custom Time Trial verification requires the exact .dkrmap directory.")
    output = output.resolve()
    output.mkdir(mode=0o700, parents=True, exist_ok=False)
    private_replay = output / "attempt.dkrreplay"
    private_replay.write_bytes(evidence)
    profile = output / "profile"
    command = [
        str(runtime),
        "--rom",
        str(rom),
        "--config",
        str(profile),
        "--verify-time-trial",
        str(private_replay),
        "--timeout",
        str(timeout),
    ]
    if track_package is not None:
        command.extend(("--time-trial-map", str(track_package.resolve(strict=True))))
    ghost = output / "verified.dkrghost"
    if emit_ghost and rules not in ADVENTURE_RULES:
        command.extend(("--export-time-trial-ghost", str(ghost)))
    if xvfb:
        command = ["xvfb-run", "-a", "-s", "-screen 0 1280x800x24", *command]
    if isolate_network:
        command = ["unshare", "-Urn", "--", *command]
    env = verification_environment(output, xvfb)
    code, _, error, interrupted = execute(
        command, output, env, timeout + 30, output / "stdout.log", output / "stderr.log"
    )
    if interrupted:
        raise KeyboardInterrupt()
    logfile = profile / "logs" / "runtime.log"
    log = logfile.read_text(encoding="utf-8", errors="replace") if logfile.is_file() else ""
    if not error and code == 6 and REJECT.search(log):
        raise InvalidReplay("The engine rejected the replay inputs or gameplay checkpoints.")
    if not error and code == 2 and rules in CUSTOM_RULES:
        stderr = (output / "stderr.log").read_text(encoding="utf-8", errors="replace")
        if "track does not match the replay package" in log + stderr:
            raise InvalidReplay("The track content does not match this recording.")
    if error or code != 0:
        raise ValueError(error or f"Engine verification failed. See the local logs in {output}.")
    if runtime_sha256 != file_digest(binary):
        raise ValueError("The verifier binary changed during playback.")
    identity = {"replaySha256": hashlib.sha256(evidence).hexdigest(), "runtimeSha256": runtime_sha256}
    result = verification_result(log, metadata, identity)
    if emit_ghost and rules not in ADVENTURE_RULES:
        if not ghost.is_file() or ghost.stat().st_size != 136 + result["updates"] * 20:
            raise ValueError("The verifier did not export a complete ghost.")
    return result


def verification_result(log: str, metadata: dict, identity: dict) -> dict:
    revisions = ROM_REVISION.findall(log)
    if revisions:
        if revisions != [str(metadata.get("rom"))]:
            raise ValueError("The engine did not reproduce the recorded US ROM revision.")
        identity = {**identity, "rom": int(revisions[0])}
    elif metadata.get("rom") == 77:
        raise ValueError("The engine did not attest the US v1.0 ROM revision.")
    rules = metadata["ruleset"]
    if rules in ADVENTURE_RULES:
        return {**identity, **adventure_result(log, metadata)}
    pass_rows, checks, scores = PASS.findall(log), CHECKPOINT.findall(log), SPLITS.findall(log)
    if len(pass_rows) != 1 or not checks or pass_rows[0] != checks[-1][0]:
        raise ValueError("The engine did not reproduce the winning checkpoint.")
    final_update, final_clock, laps, final_hash = checks[-1]
    expected_laps = metadata.get("laps") if rules == CUSTOM_LAP_RULES else 3
    if int(final_clock) <= 0 or int(laps) != expected_laps or len(scores) != 1 or scores[0][0] != final_clock:
        raise ValueError("The verifier did not reproduce the full race and its lap count.")
    splits = json.loads(scores[0][1])
    if len(splits) != expected_laps or splits[-1] != int(final_clock):
        raise ValueError("The verifier returned an invalid lap sequence.")
    best_lap = min(end - start for start, end in zip([0, *splits[:-1]], splits, strict=True))
    if rules in CUSTOM_RULES:
        targets = CUSTOM_TARGET.findall(log)
        if targets != [(metadata.get("trackId"), metadata.get("trackHash"), metadata.get("vehicle"))]:
            raise ValueError("The verifier reported a different custom track or vehicle.")
        track, digest, vehicle = targets[0]
        route = {"trackId": track, "trackHash": digest, "vehicle": vehicle}
    else:
        route = replay_course(log)
    return {
        **identity,
        **route,
        "splits": splits,
        "bestLapTicks": best_lap,
        "clockTicks": int(final_clock),
        "updates": int(final_update),
        "laps": int(laps),
        "checkpoints": [[int(tick), digest] for tick, _, _, digest in checks],
        "finalCheckpoint": final_hash,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    for path in ("runtime", "rom", "replay", "output"):
        parser.add_argument(f"--{path}", type=Path, required=True)
    parser.add_argument("--timeout", type=int, default=240)
    parser.add_argument("--emit-ghost", action="store_true", help="Export a portable ghost for a verified Time Trial.")
    parser.add_argument("--track-package", type=Path, help="Exact .dkrmap directory for a custom track recording.")
    parser.add_argument("--xvfb", action="store_true", help="Use the local Linux Xvfb display and software Vulkan.")
    parser.add_argument(
        "--isolate-network", action="store_true", help="Run the game in a separate Linux network namespace."
    )
    options = parser.parse_args()
    if options.timeout < 1:
        parser.error("--timeout must be positive")
    try:
        print(json.dumps(verify(**vars(options)), indent=2))
        return 0
    except InvalidReplay as error:
        print(error, file=sys.stderr)
        return 2
    except (OSError, ValueError) as error:
        print(error, file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
