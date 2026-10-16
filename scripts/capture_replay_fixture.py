#!/usr/bin/env python3
"""Capture a private, quiesced retail replay fixture with a qualification runtime."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import sys

from regression import execute

ROM_REVISIONS = {
    '0cb115d8716dbbc2922fda38e533b9fe63bb9670': 77,
    '6d96743d46f8c0cd0edb0ec5600b003c89b93755': 80,
}
MAPS = (3, 4, 5, 7, 8, 10, 14, 26, 30, 40, 53)
CAPTURE_RECORD = re.compile(
    r'^\[replay-probe\]\[quiesced\] revision=(77|80) bytes=([0-9]+) success=1$', re.MULTILINE)


def file_hash(path: Path, algorithm: str = 'sha256') -> str:
    digest = hashlib.new(algorithm)
    with path.open('rb') as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b''):
            digest.update(chunk)
    return digest.hexdigest()


def capture(runtime: Path, rom: Path, output: Path, *, players: int = 2,
            map_id: int = 3, timeout: int = 60) -> Path:
    """Return fixture.json only after exit, quiescence, revision and size agree.

    Failures retain their private output directory and logs for diagnosis.
    Input validation precedes directory creation; existing output is never reused.
    """
    runtime, rom = Path(runtime).resolve(), Path(rom).resolve()
    output = Path(output).absolute()
    if players not in (1, 2, 3, 4) or map_id not in MAPS or timeout <= 0:
        raise ValueError('Capture requires 1–4 players, a supported map and a positive timeout.')
    if not runtime.is_file() or not os.access(runtime, os.X_OK):
        raise ValueError(f'Runtime must be an existing executable: {runtime}')
    if not rom.is_file() or rom.suffix.lower() != '.z64':
        raise ValueError('ROM must be an existing canonical .z64 file.')
    rom_sha1 = file_hash(rom, 'sha1')
    revision = ROM_REVISIONS.get(rom_sha1)
    if revision is None:
        raise ValueError('ROM SHA-1 does not match either supported canonical US retail revision.')
    runtime_sha256 = file_hash(runtime)
    # mkdir is the exclusive claim: neither an existing directory nor a symlink
    # may cause a profile or snapshot from another run to be overwritten.
    output.mkdir(parents=True, exist_ok=False)
    fixture = output / 'race.dkr-component'
    profile = output / 'profile'
    stdout, stderr = output / 'stdout.log', output / 'stderr.log'
    env = {key: value for key, value in os.environ.items() if not key.startswith('DKR_')}
    env.update(DKR_REPLAY_CAPTURE_FILE=str(fixture), DKR_REPLAY_CAPTURE_MAP=str(map_id),
               DKR_REPLAY_CAPTURE_PLAYERS=str(players))
    command = [str(runtime), '--rom', str(rom), '--config', str(profile), '--timeout', str(timeout)]
    code, elapsed_ms, error, interrupted = execute(
        command, output, env, timeout + 30, stdout, stderr)
    logs = f'Private logs: {output} (including profile/logs/runtime.log).'
    if interrupted:
        raise KeyboardInterrupt
    if error or code != 0:
        raise RuntimeError(f'Capture failed: {error or f"runtime exited {code}"}. {logs}')
    runtime_log = profile / 'logs' / 'runtime.log'
    if not runtime_log.is_file():
        raise RuntimeError(f'Capture has no runtime quiescence log. {logs}')
    records = CAPTURE_RECORD.findall(runtime_log.read_text(encoding='utf-8', errors='replace'))
    if len(records) != 1 or int(records[0][0]) != revision:
        raise RuntimeError(f'Capture lacks one successful quiescence record for revision {revision}. {logs}')
    expected_bytes = int(records[0][1])
    if expected_bytes <= 0 or not fixture.is_file() or fixture.stat().st_size != expected_bytes:
        raise RuntimeError(f'Captured fixture size does not match its quiescence record. {logs}')
    manifest = output / 'fixture.json'
    metadata = {
        'schema_version': 1, 'revision': revision, 'map': map_id, 'players': players,
        'rom_sha1': rom_sha1, 'runtime_sha256': runtime_sha256,
        'fixture': fixture.name, 'fixture_sha256': file_hash(fixture),
        'fixture_bytes': expected_bytes, 'capture_elapsed_ms': elapsed_ms,
        'runtime_log': str(runtime_log.relative_to(output)),
    }
    with manifest.open('x', encoding='utf-8') as target:
        json.dump(metadata, target, indent=2)
        target.write('\n')
    return manifest


def positive_int(value: str) -> int:
    number = int(value)
    if number <= 0:
        raise argparse.ArgumentTypeError('must be positive')
    return number


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, epilog=(
        'Requires a DKR_REPLAY_QUALIFICATION runtime. Keep the entire output private; '
        'fixtures contain ROM-derived data. RT64 is optional for CPU-only capture.'))
    parser.add_argument('--runtime', type=Path, required=True)
    parser.add_argument('--rom', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True, help='New private output directory; never overwritten')
    parser.add_argument('--players', type=int, choices=(1, 2, 3, 4), default=2)
    parser.add_argument('--map', dest='map_id', type=int, choices=MAPS, default=3)
    parser.add_argument('--timeout', type=positive_int, default=60)
    args = parser.parse_args(argv)
    try:
        manifest = capture(**vars(args))
    except (OSError, ValueError, RuntimeError) as failure:
        print(f'Capture failed: {failure}', file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        print('Capture interrupted; private logs were retained.', file=sys.stderr)
        return 130
    print(f'Captured verified private fixture: {manifest}')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
