"""Explicit v1.0.7 release orchestration. Never executes DKR-R or tests."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
import zipfile

sys.dont_write_bytecode = True
AUTH = Path(__file__).resolve().parent
ROOT = AUTH.parents[2]
VERSION = '1.0.7'
STAGES = {
    'PORTRAIT': 'character-portraits-beta5-20261017-r2/native',
    'CAMERA': 'camera-plus-20261017-r14/native',
    'CAMERA_SHADOW': 'camera-shadow-beta6-20261017-r3/stage',
    'LAUNCHER_UI': 'launcher-polish-20261017-r2/native',
    'SDK2': 'sdk2-dev-menu-startup-20261018-r1/stage',
    'MOD_STORE': 'mod-store-ui-polish-20261019-r4/stage',
    'SHARED_SAVES': 'shared-saves-beta10-20261019-r13/stage',
    'VANILLA_WATER': 'vanilla-water-animation-beta10-20261019-r3/stage',
    'VIEWPORT_FLOW': 'viewport-flow-beta10-20261019-r6/stage',
    'TITLE_TEXT': 'title-text-beta10-20261019-r3/stage',
    'CREDITS_VIEWPORT': 'credits-viewport-beta10-20261019-r2/stage',
    'PRESENTATION_POLISH': 'presentation-polish-beta10-20261019-r5/stage',
    'SURGICAL_PERFORMANCE': 'performance-surgical-beta10-20261019-r4/stage',
    'LIQUID_STREAMING': 'liquid-streaming-beta10-20261020-r3/stage',
}

def sha(path):
    h = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(1024*1024), b''):
            h.update(block)
    return h.hexdigest()

def git(*args):
    return subprocess.check_output(['git', '-C', str(ROOT), *args])

def backup(out):
    if out.exists():
        raise ValueError('Choose a NEW backup directory')
    out.mkdir(parents=True)
    names = git('ls-files', '--cached', '--others', '--exclude-standard', '-z').decode().split('\0')
    records = []
    protected = ('extern/', 'runtime-recomp/RecompiledFuncs/', 'runtime-recomp/RecompiledPatches/')
    with zipfile.ZipFile(out/'AUTHORED-SOURCE-BEFORE.zip', 'x', zipfile.ZIP_DEFLATED) as archive:
        for name in sorted(set(names)):
            path = ROOT/name
            if not name or name.startswith(protected) or not path.is_file() or path.is_symlink():
                continue
            if path.suffix.lower() in ('.z64', '.v64', '.n64', '.eep', '.mpk', '.key', '.pem', '.jks', '.keystore'):
                raise ValueError('Unexpected private file in source set: '+name)
            archive.write(path, name)
            records.append({'path': name, 'sha256': sha(path), 'bytes': path.stat().st_size})
    (out/'SOURCE-FILES.json').write_text(json.dumps(records, indent=2)+'\n')
    (out/'TRACKED-DIFF.patch').write_bytes(git('diff', '--binary', 'HEAD'))
    (out/'GIT-STATUS.txt').write_bytes(git('status', '--short', '-uall'))
    (out/'GIT-HEAD.txt').write_bytes(git('rev-parse', 'HEAD'))
    (out/'SUBMODULES.txt').write_bytes(git('submodule', 'status', '--recursive'))
    with zipfile.ZipFile(out/'AUTHORED-SOURCE-BEFORE.zip') as archive:
        if archive.testzip():
            raise ValueError('Backup archive integrity failure')
        for record in records:
            if hashlib.sha256(archive.read(record['path'])).hexdigest() != record['sha256']:
                raise ValueError('Backup member changed')
    print('Verified recoverable source snapshot:', len(records), 'files;', out)

def configure(build, log, cmake, capsule=None):
    if capsule is None:raise ValueError('Reconstruct the release source capsule first, then pass --capsule. Historical stages are not a release default.')
    command = [cmake, '-S', str(ROOT/'runtime-recomp'), '-B', str(build),
        '-DDKR_RELEASE_VERSION:STRING='+VERSION,
        '-DCMAKE_PROJECT_INCLUDE='+str(AUTH/'ReleaseOverlay.cmake'),
        '-DDKR_RELEASE_EVIDENCE='+str(log.parent)]
    for name, value in STAGES.items():
        command.append('-DDKR_'+name+'_STAGE='+str(capsule/'roles'/name if capsule else ROOT/'build'/value))
    command += ['-DDKR_SDK2_REUSE_STAGE='+str(capsule/'roles/SDK2_BASE' if capsule else ROOT/'build/sdk2-beta7-20261018-r18/stage'),
        '-DDKR_SHARED_SAVES_REUSE_STAGE=', '-DDKR_SHARED_SAVES_SOURCE_STAGE=']
    command.append('-DDKR_RELEASE_CAPSULE='+str(capsule or ''))
    if capsule:
        for revision in (77,80):
            command.append('-DDKR_CAMERA_KERNEL_V'+str(revision)+'='+str(capsule/('prepared/CAMERA_KERNEL_V'+str(revision))))
    for flag in ('EXPERIMENTAL_RENDER', 'REPLAY', 'ANDROID_RENDER', 'WATER', 'TASK', 'LEGACY'):
        command.append('-DDKR_'+flag+'_QUALIFICATION=OFF')
    log.parent.mkdir(parents=True, exist_ok=True)
    with log.open('w') as stream:
        subprocess.run(command, stdout=stream, stderr=subprocess.STDOUT, check=True)
    print('Configured stable release without target execution:', build)

def compile_build(build, log, cmake, jobs):
    command = [cmake, '--build', str(build), '--config', 'RelWithDebInfo', '--target', 'DKRPortGame', '--parallel', str(jobs)]
    log.parent.mkdir(parents=True, exist_ok=True)
    with log.open('w') as stream:
        subprocess.run(command, stdout=stream, stderr=subprocess.STDOUT, check=True)
    print('Compile/link completed. No target execution:', build)

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('action', choices=('backup', 'configure', 'build', 'configure-build'))
    p.add_argument('--output', type=Path)
    p.add_argument('--build', type=Path)
    p.add_argument('--log', type=Path)
    p.add_argument('--cmake', default='cmake')
    p.add_argument('--jobs', type=int, default=4)
    p.add_argument('--capsule', type=Path)
    a = p.parse_args()
    if a.action == 'backup': backup(a.output)
    elif a.action == 'configure': configure(a.build, a.log, a.cmake, a.capsule)
    elif a.action == 'configure-build':
        configure(a.build, a.log, a.cmake, a.capsule)
        compile_build(a.build, a.log.parent/(a.log.stem+'-build.log'), a.cmake, a.jobs)
    else: compile_build(a.build, a.log, a.cmake, a.jobs)

if __name__ == '__main__': main()
