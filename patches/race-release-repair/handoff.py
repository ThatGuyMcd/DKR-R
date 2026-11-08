"""Collect already audited matching playtests and their synthetic test evidence."""
from pathlib import Path
import argparse
import hashlib
import json
import shutil
import zipfile

AUTH = Path(__file__).resolve().parent
VERSION = '1.0.9-stability-playtest.2'


def sha(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--work', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    if a.output.exists():
        raise ValueError('Choose a fresh handoff folder')
    packages = {name: a.work / 'packages' / name for name in ('windows', 'linux')}
    receipts = {name: json.loads((path / 'BUILD-RECEIPT.json').read_text()) for name, path in packages.items()}
    windows, linux = receipts.values()
    if any(r['version'] != VERSION for r in receipts.values()):
        raise ValueError('Package versions differ')
    ids = windows['audit']['rollback_build_ids']
    if ids != linux['audit']['rollback_build_ids'] or windows['audit']['release_contract'] != linux['audit']['release_contract']:
        raise ValueError('Windows/Linux release contracts differ')
    checks = {name: json.loads((a.work / name / 'checks/REGRESSION-RECEIPT.json').read_text()) for name in packages}
    if any(r['before']['exit_code'] != 1 or r['after']['exit_code'] != 0 for r in checks.values()):
        raise ValueError('Both regression positive/negative controls must pass')
    if checks['windows']['passed_checks'] != checks['linux']['passed_checks']:
        raise ValueError('Regression counts differ')
    elves = json.loads((a.work / 'linux/elf-inspection.json').read_text())
    if elves['missing_non_system_dependencies']:
        raise ValueError('Linux dependency audit failed')
    artifacts = []
    for name, receipt in receipts.items():
        artifact = packages[name] / receipt['artifact']['name']
        if artifact.stat().st_size != receipt['artifact']['bytes'] or sha(artifact) != receipt['artifact']['sha256']:
            raise ValueError('Package changed after audit: ' + name)
        probe = json.loads((a.work / name / 'packaged-worker-probe/PROBE-RECEIPT.json').read_text())
        if probe.get('roms_or_mods_loaded') is not False or probe.get('online_prepare_batch') != 1:
            raise ValueError('Packaged worker handshake is missing')
        artifacts.append(artifact)
    a.output.mkdir(parents=True)
    evidence = a.output / 'Evidence'
    evidence.mkdir()
    for artifact in artifacts:
        shutil.copy2(artifact, a.output / artifact.name)
    for name, path in packages.items():
        shutil.copy2(path / 'BUILD-RECEIPT.json', evidence / (name.upper() + '-BUILD-RECEIPT.json'))
        for filename in ('REGRESSION-RECEIPT.json', 'regression-before.log', 'regression-after.log'):
            shutil.copy2(a.work / name / 'checks' / filename, evidence / (name.upper() + '-' + filename))
        shutil.copy2(a.work / name / 'packaged-worker-probe/PROBE-RECEIPT.json', evidence / (name.upper() + '-WORKER-PROBE.json'))
        shutil.copy2(a.work / name / 'build-final.log', evidence / (name.upper() + '-build.log'))
    shutil.copy2(a.work / 'linux/elf-inspection.json', evidence / 'LINUX-ELF-INSPECTION.json')
    shutil.copy2(AUTH / 'PLAYTEST.txt', a.output / 'README.txt')
    shutil.copy2(AUTH / 'IMPLEMENTATION.txt', a.output / 'IMPLEMENTATION.txt')
    source_zip = a.output / 'PATCH-PIPELINE-SOURCES.zip'
    with zipfile.ZipFile(source_zip, 'x', zipfile.ZIP_DEFLATED) as archive:
        for directory in ('music-preview-repair', 'lobby-stability', AUTH.name):
            for path in sorted((AUTH.parent / directory).rglob('*')):
                if path.is_file() and '__pycache__' not in path.parts:
                    archive.write(path, path.relative_to(AUTH.parents[1]).as_posix())
    with zipfile.ZipFile(source_zip) as archive:
        if archive.testzip():
            raise ValueError('Source ZIP verification failed')
    count = checks['windows']['passed_checks']
    unchanged = windows['audit']['unchanged_authored_files']
    if unchanged != linux['audit']['unchanged_authored_files']:
        raise ValueError('Authored source validation counts differ')
    validation = f'''DKR-R {VERSION} - Validation

PASS: Windows x64 native compile/link and complete ZIP assembly.
PASS: Linux x86_64 native compile/link and complete AppImage assembly.
PASS: Identical revision 77/80 peer identifiers and release contract on both OSes.
PASS: {count} host-only regression assertions on EACH OS using the exact
      production party-runtime handler and a synthetic 16-byte OwnedGame.
PASS: The SAME fixture against the unchanged prior runtime reproduces
      FIRST POST-LOAD RELEASE WAS DISCARDED on BOTH OSes (expected exit 1).
PASS: Actual first-frame confirmation after scene release; repeated scene
      changes; duplicates; older epochs/frames/controls; host/roster rejection;
      both spectator checkpoint/release orders; same-cursor controls change;
      release without a spectator vote and with only racers connected.
PASS: Exact source recomposition, actual compiler binding, linked feature/ID
      checks and all retained music/directory/scoreboard/online/security audits.
PASS: All {unchanged} pre-existing authored files unchanged. The prior SDK
      task adapter and its unchanged delegated source hash remain validated.
PASS: Both PACKAGED ModWorkers complete online/batch capability handshakes
      without loading a ROM or mod.
PASS: Windows ZIP CRC; AppImage SquashFS metadata; {len(elves['files'])} ELF files;
      no unresolved non-system dependencies. GLIBC {elves['max_glibc']}, GLIBCXX {elves['max_glibcxx']}.

Not performed: game/ROM/RSP execution, AppImage launch, physical multiplayer,
network/controller/GPU acceptance or the 20-transition owner test in README.
The host-only fixture executables and worker capability operation WERE run.
The inherited queue/retry/cache/bootstrap policies are unchanged and audited;
this delivery does not claim their earlier test suites were rerun.

Host/client evidence from match 5271837563691785035 shows host release of
epoch 92 after both racers loaded; the client never advanced and the host
subsequently hit the existing 90-second no-progress watchdog. The source
defect and negative control agree with that observation. User logs are not
included in this distributable bundle.

All participants MUST use these matching playtest.2 packages.
No website/API/FTP deployment, release publication or updater signing.

Revision 77: {ids['77']}
Revision 80: {ids['80']}
Release contract: {windows['audit']['release_contract']}
'''
    (a.output / 'VALIDATION.txt').write_text(validation)
    files = [*artifacts, source_zip]
    (a.output / 'SHA256SUMS.txt').write_text(''.join(sha(a.output / path.name) + '  ' + path.name + '\n' for path in files))
    print('Verified matching playtest handoff:', a.output)
