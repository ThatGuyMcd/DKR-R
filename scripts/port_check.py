#!/usr/bin/env python3
"""Does this target simulate the same bits as a known-good one?

Builds the replay probe with the toolchain you give it, runs the standard
workload (2, 3 and 4 players, both ROM revisions you supply), and compares the
full final checkpoint and every effect journal with a reference produced the
same way on a known-good machine (your x86_64 or arm64 box). It prints the
determinism contract the build reports and one verdict.

Run it twice, same arguments except the toolchain:

    # 1. on a machine that already races (writes the reference)
    port_check.py --out ref --fixture 77=race77.dkr-component \\
        --fixture 80=race80.dkr-component --rom 77=us77.z64 --rom 80=us80.z64

    # 2. on the target (native, a CMake toolchain file, or a Docker image)
    port_check.py --out target --reference ref --fixture ... --rom ... \\
        [--toolchain-file T.cmake --emulator qemu-riscv64]
        [--docker-image debian:trixie --docker-platform linux/riscv64]

The fixture comes from scripts/capture_replay_fixture.py; copy the same bytes
to the target. The reference is a full copy of guest RAM, so it is ROM-derived
and cannot be shared or checked in: every porter produces their own. Nothing
here uploads, prints or commits fixture, ROM or reference bytes; only hashes,
offsets and frame numbers are printed. Keep --out private.

Exit codes: 0 reference written or identical, 1 different, 2 could not run or
the runs are not comparable.
"""

from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
PLAYERS = (2, 3, 4)
GENERATOR_FLAGS = (
    "--scene-cuts",
    "--audio-services",
    "--input-services",
    "--authored-cpu",
    "--isolate-symbols",
    "--full-scenes",
)
UBSAN_FLAGS = "-fsanitize=signed-integer-overflow,shift,integer-divide-by-zero -g"
UBSAN_REPORT = re.compile(r"^(\S+?):(\d+):\d+: runtime error: (.*)$", re.M)
CONTRACT_LINE = re.compile(r"^\[contract\] architecture=(\S+) contract=(\S+)$", re.M)


def load_comparator():
    spec = importlib.util.spec_from_file_location(
        "compare_replay_references", ROOT / "scripts/compare_replay_references.py"
    )
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def revision_paths(pairs: list[str], label: str) -> dict[int, Path]:
    found: dict[int, Path] = {}
    for pair in pairs:
        revision, _, path = pair.partition("=")
        if revision not in ("77", "80") or not path:
            raise SystemExit(f"--{label} wants 77=PATH or 80=PATH, got {pair!r}")
        if not Path(path).is_file():
            raise SystemExit(f"--{label} {revision}: {path} is not a file")
        found[int(revision)] = Path(path).resolve()
    if not found:
        raise SystemExit(f"--{label} is required for at least one revision")
    return found


class Environment:
    """Where commands run: here, or in a throwaway container with the same paths."""

    def __init__(self, image=None, platform=None, cpus="3", mounts=(), writable=()):
        self.image, self.platform, self.cpus = image, platform, cpus
        self.mounts = [Path(path) for path in mounts]
        self.writable = [Path(path) for path in writable]

    def wrap(self, command: list[str], env: dict[str, str] | None = None, cwd: Path | None = None) -> list[str]:
        if self.image is None:
            return command
        prefix = ["docker", "run", "--rm", "--cpus", self.cpus, "--cpu-shares", "256"]
        if self.platform:
            prefix += ["--platform", self.platform]
        for path in self.mounts:
            prefix += ["-v", f"{path}:{path}:ro"]
        for path in self.writable:
            prefix += ["-v", f"{path}:{path}"]
        for key, value in sorted((env or {}).items()):
            prefix += ["-e", f"{key}={value}"]
        if cwd is not None:
            prefix += ["-w", str(cwd)]
        return prefix + [self.image, *command]


def probe_cmake_args(args, generated: Path, build: Path) -> list[str]:
    command = [
        "cmake",
        "-S",
        str(ROOT / "runtime-recomp/tests/replay_probe"),
        "-B",
        str(build),
        "-DCMAKE_BUILD_TYPE=Release",
        f"-DDKR_DEPENDENCIES_DIR={args.deps}",
        f"-DDKR_PROBE_GENERATED={generated}",
    ]
    if args.toolchain_file:
        command.append(f"-DCMAKE_TOOLCHAIN_FILE={args.toolchain_file}")
    if args.ubsan:
        command += [f"-DCMAKE_C_FLAGS={UBSAN_FLAGS}", f"-DCMAKE_CXX_FLAGS={UBSAN_FLAGS}"]
    return command + list(args.cmake_arg)


def workload_command(args, probe: Path, fixture: Path) -> list[str]:
    return [
        *shlex.split(args.emulator),
        str(probe),
        str(fixture),
        "mode_game",
        "--offline-services",
        "--authored-owned-driver",
    ]


def summarize_ubsan(log_text: str, limit: int = 12) -> list[tuple[str, int]]:
    """Distinct file:line sites with their count, most frequent first."""
    counts: dict[str, int] = {}
    for path, line, _message in UBSAN_REPORT.findall(log_text):
        counts[f"{path}:{line}"] = counts.get(f"{path}:{line}", 0) + 1
    return sorted(counts.items(), key=lambda item: -item[1])[:limit]


def describe_difference(result: dict) -> list[str]:
    """Offsets and frames only: never the differing byte values."""
    lines = [f"    {error.get('side', 'comparison')}: {error['message']}" for error in result.get("errors", [])]
    lines += [f"    identity {d['field']}: {d['left']} != {d['right']}" for d in result.get("identity_differences", [])]
    state = result.get("first_state_difference")
    if state:
        lines.append(f"    first checkpoint difference at byte {state['byte_offset']}")
    effect = result.get("first_effect_difference")
    if effect:
        lines.append(f"    first effect difference at frame {effect['frame']}, journal byte {effect['byte_offset']}")
    return lines


def run_logged(command: list[str], log: Path, env: dict[str, str] | None = None, cwd: Path | None = None) -> int:
    merged = {**os.environ, **(env or {})}
    with log.open("wb") as stream:
        return subprocess.run(command, stdout=stream, stderr=subprocess.STDOUT, env=merged, cwd=cwd).returncode


def build_probe(args, environment, revision: int, work: Path, logs: Path) -> tuple[Path, Path] | None:
    generated, build = work / f"gen-v{revision}", work / f"build-v{revision}"
    if not generated.exists():
        generator = [
            sys.executable,
            str(ROOT / "scripts/generate_replay_probe.py"),
            str(args.payload / f"generated-v{revision}"),
            str(args.deps / "n64-modern-runtime/N64Recomp/include/recomp.h"),
            str(generated),
            "--revision",
            str(revision),
            "--dependencies-dir",
            str(args.deps),
            "--rsp-source",
            str(ROOT / "runtime-recomp/RecompiledRSP"),
            "--code-policy-config",
            str(args.payload / f"v{revision}.toml"),
            *GENERATOR_FLAGS,
        ]
        if run_logged(generator, logs / f"generate-v{revision}.log") != 0:
            print(f"  v{revision}: probe generation failed, see {logs / f'generate-v{revision}.log'}")
            return None
    steps = [
        probe_cmake_args(args, generated, build),
        [
            "cmake",
            "--build",
            str(build),
            "--target",
            "DKRGuestReplayProbe",
            "DKRContractReport",
            "--parallel",
            args.jobs,
        ],
    ]
    for index, step in enumerate(steps):
        log = logs / f"build-v{revision}-{'configure' if index == 0 else 'build'}.log"
        if run_logged(environment.wrap(step, cwd=work), log, cwd=None if environment.image else work) != 0:
            print(f"  v{revision}: {'configure' if index == 0 else 'build'} failed, see {log}")
            return None
    suffix = ".exe" if (build / "Release/DKRGuestReplayProbe.exe").exists() else ""
    config_dir = build / "Release" if suffix else build
    return config_dir / f"DKRGuestReplayProbe{suffix}", config_dir / f"DKRContractReport{suffix}"


def check(args, environment) -> int:
    out = args.out.resolve()
    if out.exists() and any(out.iterdir()):
        print(f"{out} is not empty; choose a new --out", file=sys.stderr)
        return 2
    logs, work = out / "logs", out / "work"
    logs.mkdir(parents=True)
    work.mkdir()
    revisions = sorted(args.fixture)
    manifest = {
        "frames": args.frames,
        "ubsan": args.ubsan,
        "fixtures": {str(r): file_sha256(args.fixture[r]) for r in revisions},
        "contract": None,
    }
    contract_seen = None
    ubsan_text = ""
    print(f"port_check: {len(revisions)} revision(s), {args.frames} frames, players {PLAYERS}")
    for revision in revisions:
        built = build_probe(args, environment, revision, work, logs)
        if built is None:
            return 2
        probe, report = built
        reported = subprocess.run(
            environment.wrap([*shlex.split(args.emulator), str(report)]), capture_output=True, text=True
        )
        (logs / f"contract-v{revision}.log").write_text(reported.stdout + reported.stderr)
        match = CONTRACT_LINE.search(reported.stdout)
        if reported.returncode != 0 or not match:
            print(f"  v{revision}: could not run the contract report, see {logs / f'contract-v{revision}.log'}")
            return 2
        if revision == revisions[0]:
            contract_seen = match.group(0)
            print(contract_seen.replace("[contract] ", "  contract: "))
            print("\n".join(f"  {line}" for line in reported.stdout.splitlines()[1:]))
        for command, name in (([str(probe), "--self-test"], "self-test"), (None, "workload")):
            env = {}
            if command is None:
                env = {
                    "DKR_PROBE_ROM": str(args.rom[revision]),
                    "DKR_PROBE_TEST_FRAMES": str(args.frames),
                    "DKR_PROBE_REFERENCE_OUTPUT": str(out / f"v{revision}"),
                }
                command = workload_command(args, probe, args.fixture[revision])
            else:
                command = [*shlex.split(args.emulator), *command]
            log = logs / f"{name}-v{revision}.log"
            status = run_logged(environment.wrap(command, env=env, cwd=work), log, env=env)
            ubsan_text += log.read_text(errors="replace") if args.ubsan else ""
            print(f"  v{revision} {name}: {'ok' if status == 0 else f'FAILED (exit {status}), see {log}'}")
            if status != 0:
                return 2
    manifest["contract"] = contract_seen
    (out / "port-check.json").write_text(json.dumps(manifest, indent=2) + "\n")
    sites = summarize_ubsan(ubsan_text)
    if sites:
        print("  undefined behaviour reported (file:line, count):")
        for site, count in sites:
            print(f"    {site}  x{count}")
    if args.reference is None:
        print(f"RESULT: reference written to {out}. Keep it private; pass it as --reference on the target.")
        return 0
    return compare(args, out, revisions)


def compare(args, out: Path, revisions: list[int]) -> int:
    reference = args.reference.resolve()
    try:
        expected = json.loads((reference / "port-check.json").read_text())
    except (OSError, ValueError):
        print(f"RESULT: INCONCLUSIVE, {reference} has no port-check.json (is it a port_check output?)")
        return 2
    mine = json.loads((out / "port-check.json").read_text())
    for key in ("frames",):
        if expected.get(key) != mine[key]:
            print(f"RESULT: INCONCLUSIVE, {key} differs from the reference ({expected.get(key)} vs {mine[key]})")
            return 2
    for revision in revisions:
        if expected["fixtures"].get(str(revision)) != mine["fixtures"][str(revision)]:
            print(f"RESULT: INCONCLUSIVE, the v{revision} fixture is not the same bytes as the reference's")
            return 2
    comparator = load_comparator()
    failures: list[str] = []
    total = 0
    for revision in revisions:
        for players in PLAYERS:
            total += 1
            name = f"v{revision}-p{players}.dkr-component"
            result = comparator.compare_references(reference / name, out / name)
            if result["status"] != "equal":
                failures.append(f"  v{revision}, {players} players: {result['status']}")
                failures += describe_difference(result)
    print(f"  reference {(expected.get('contract') or '').replace('[contract] ', 'contract: ')}")
    if failures:
        print(f"RESULT: DIFFERENT, {total - sum(1 for f in failures if f.startswith('  v'))} of {total} identical")
        print("\n".join(failures))
        print("  Next: rerun with --ubsan (fresh --out) to list undefined behaviour in the build, and")
        print("  try -O1 to see whether it is optimisation dependent. docs/CROSS-ARCHITECTURE.md has the story.")
        return 1
    print(
        f"RESULT: IDENTICAL, {total} of {total} comparisons byte for byte (full checkpoint and every effect journal)."
    )
    return 0


def parse_args(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--out", type=Path, required=True, help="new, private directory for logs and results")
    parser.add_argument("--reference", type=Path, help="a previous --out from a known-good machine")
    parser.add_argument(
        "--fixture", action="append", default=[], metavar="REV=PATH", help="race.dkr-component per revision"
    )
    parser.add_argument("--rom", action="append", default=[], metavar="REV=PATH", help="canonical .z64 per revision")
    parser.add_argument("--frames", type=int, default=300, help="12 to 300 (default 300)")
    parser.add_argument(
        "--payload", type=Path, default=ROOT / "build/prepared", help="scripts/prepare_payloads.py output"
    )
    parser.add_argument("--deps", type=Path, default=ROOT / "extern", help="prepared extern directory")
    parser.add_argument("--toolchain-file", type=Path, help="CMake toolchain file for a cross build")
    parser.add_argument("--emulator", default="", help="prefix to run target binaries, e.g. 'qemu-riscv64'")
    parser.add_argument("--docker-image", help="build and run inside this image (needs cmake, a compiler, python3)")
    parser.add_argument("--docker-platform", help="e.g. linux/riscv64")
    parser.add_argument("--cpus", default="3", help="CPU limit for containers")
    parser.add_argument("--jobs", default="2", help="parallel build jobs")
    parser.add_argument("--ubsan", action="store_true", help="build with UBSan and list the sites it reports")
    parser.add_argument("--cmake-arg", action="append", default=[], help="extra CMake argument (repeatable)")
    args = parser.parse_args(argv)
    if not 12 <= args.frames <= 300:
        parser.error("--frames must be between 12 and 300")
    args.fixture = revision_paths(args.fixture, "fixture")
    args.rom = revision_paths(args.rom, "rom")
    if set(args.fixture) != set(args.rom):
        parser.error("--fixture and --rom must name the same revisions")
    args.deps, args.payload = args.deps.resolve(), args.payload.resolve()
    return args


def main(argv=None) -> int:
    args = parse_args(argv)
    out = args.out.resolve()
    mounts = [ROOT, args.deps, args.payload, *{p.parent for p in [*args.fixture.values(), *args.rom.values()]}]
    if args.toolchain_file:
        mounts.append(args.toolchain_file.resolve().parent)
    environment = Environment(args.docker_image, args.docker_platform, args.cpus, mounts, [out])
    return check(args, environment)


if __name__ == "__main__":
    sys.exit(main())
