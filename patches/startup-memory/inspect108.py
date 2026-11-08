"""Static startup/source and release-artifact inspection; never runs DKR-R."""
from pathlib import Path
import argparse
import hashlib
import json
import re
import shutil
import sys
import zipfile

sys.dont_write_bytecode = True
AUTH = Path(__file__).resolve().parent
ROOT = AUTH.parents[1]
BASE = ROOT / 'patches/release-prep/v1.0.7'


def sha(path):
    h = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()


def source(build, output):
    relative = 'librecomp/src/recomp.cpp'
    before = (build / 'generated/custom-magic-code-heap' / relative).read_bytes().replace(b'\r\n', b'\n')
    fixed = build / 'generated/startup-memory' / relative
    after = fixed.read_bytes().replace(b'\r\n', b'\n')
    old_start = before.index(b'#ifdef _WIN32\n    rdram =')
    new_start = after.index(b'#ifdef _WIN32\n    // Keep the complete guest address guard')
    end = b'#else\n    rdram = (uint8_t*)mmap'
    old_end = before.index(end, old_start)
    new_end = after.index(end, new_start)
    if before[:old_start] != after[:new_start] or before[old_end:] != after[new_end:]:
        raise ValueError('Non-Windows allocation or unrelated runtime source changed')
    block = after[new_start:new_end]
    for expected in (b'VirtualAlloc(nullptr, allocation_size, MEM_RESERVE, PAGE_NOACCESS)',
                     b'VirtualAlloc(rdram, mem_size, MEM_COMMIT, PAGE_READWRITE)',
                     b'memory_error = GetLastError();', b'VirtualFree(rdram, 0, MEM_RELEASE)',
                     b'available-commit-bytes=', b'guard=reserved-noaccess'):
        if expected not in block:
            raise ValueError('Missing allocation/lifetime/diagnostic contract')
    if b'MEM_COMMIT | MEM_RESERVE' in block or b'VirtualProtect(' in block:
        raise ValueError('Full-guard commit survived')
    if block.count(b'VirtualAlloc(') != 2:
        raise ValueError('Unexpected allocation fallback')
    build_files = [build / 'build.ninja']
    if not build_files[0].exists():
        build_files = list(build.rglob('librecomp.vcxproj'))
    marker = 'generated/startup-memory/librecomp/src/recomp.cpp'
    if not any(marker in p.read_text(encoding='utf-8-sig').replace('\\', '/') for p in build_files if p.is_file()):
        raise ValueError('Build target does not compile the staged correction')
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps({'before_sha256': hashlib.sha256(before).hexdigest(),
        'after_sha256': hashlib.sha256(after).hexdigest(), 'scope': 'Windows allocation branch only',
        'reserved_bytes': 4294967296, 'committed_bytes': 536870912,
        'uncommitted_guard_bytes': 3758096384,
        'execution': 'Source and generated build graph inspection only; no target/tests executed.'}, indent=2) + '\n')
    print('Staged allocation, build graph and unchanged POSIX/runtime remainder inspected:', build)


def apk(args):
    original = BASE / 'inspect_apk.py'
    text = original.read_text(encoding='utf-8').replace('1.0.7', '1.0.8').replace('1070018', '1080001')
    # Existing verifier includes both published and previous signer comparisons.
    sys.argv = [str(original)]
    for name in ('apk', 'published', 'beta', 'tools', 'output'):
        sys.argv += ['--' + name, str(getattr(args, name))]
    exec(compile(text, str(original), 'exec'), {'__file__': str(original), '__name__': '__main__'})


def collect(work, output, windows_binaries):
    if output.exists():
        raise ValueError('Never overwrite a delivered release folder')
    contracts = {}
    for key in ('windows', 'linux', 'arm64', 'android'):
        text = (work / 'evidence' / key / 'DKRExperimentalOwnedRegistry-COMPILE_DEFINITIONS.txt').read_text()
        ids = dict(re.findall(r'DKR_OWNED_BUILD_ID_V(77|80)="([a-f0-9]{64})"', text))
        if set(ids) != {'77', '80'}:
            raise ValueError('Missing native online contract: ' + key)
        contracts[key] = ids
    if any(value != contracts['windows'] for value in contracts.values()):
        raise ValueError('Mixed cross-platform online contracts')
    for key in ('linux', 'arm64'):
        if json.loads((work / 'evidence' / key / 'ELF-INSPECTION.json').read_text())['missing_non_system_dependencies']:
            raise ValueError('Unresolved ELF dependency')
    report = json.loads((work / 'evidence/android/APK-INSPECTION.json').read_text())
    if report['version'] != '1.0.8' or report['version_code'] != 1080001:
        raise ValueError('Uninspected APK identity')
    for key in ('windows', 'linux', 'arm64', 'android'):
        json.loads((work / 'evidence' / key / 'SOURCE-INSPECTION.json').read_text())
    review = json.loads((work / 'evidence/SOURCE-REVIEW.json').read_text())
    if review['issues'] or review['missing_relative_links']:
        raise ValueError('Source review needs attention')
    inputs = {
        'DKR-R-1.0.8-Windows-x64.zip': work / 'packages/windows/DKR-R-1.0.8-Windows-x64.zip',
        'DKR-R-1.0.8-Linux-x86_64.AppImage': work / 'packages/linux/DKR-R-1.0.8-Linux-x86_64.AppImage',
        'DKR-R-1.0.8-Linux-aarch64.AppImage': work / 'packages/arm64/DKR-R-1.0.8-Linux-aarch64.AppImage',
        'DKR-R-1.0.8-SteamDeck-x86_64.AppImage': work / 'packages/linux/DKR-R-1.0.8-Linux-x86_64.AppImage',
        'DKR-R-1.0.8-Android-arm64.apk': work / 'packages/android/DKR-R-1.0.8-Android-arm64.apk',
    }
    native = (windows_binaries / 'libexec/dkr-r/DKR-R-Runtime.exe').read_bytes()
    if b'guard=reserved-noaccess' not in native or b'win32-error=' not in native:
        raise ValueError('Windows executable lacks the linked allocation correction')
    output.mkdir(parents=True)
    records = {}
    for name, path in inputs.items():
        target = output / name
        shutil.copy2(path, target)
        if sha(path) != sha(target):
            raise ValueError('Artifact copy changed')
        records[name] = {'sha256': sha(target), 'bytes': target.stat().st_size}
    for name, path in [('RELEASE-NOTES.txt', ROOT / 'docs/PUBLIC-1.0.8-NOTES.txt'),
                       ('README.txt', ROOT / 'packaging/RELEASE-README.md')]:
        shutil.copy2(path, output / name)
        records[name] = {'sha256': sha(output / name), 'bytes': (output / name).stat().st_size}
    (output / 'SHA256SUMS.txt').write_text(''.join(x['sha256'] + '  ' + name + '\n' for name, x in sorted(records.items())))
    (work / 'evidence/DELIVERY.json').write_text(json.dumps({'version': '1.0.8', 'files': records,
        'rollback_build_ids': contracts['windows'], 'automatic_update_signed': False,
        'owner_startup_confirmation': 'Pending on affected PC; no game/tests executed.'}, indent=2) + '\n')
    symbols = work / 'private-symbols/windows'
    symbols.mkdir(parents=True, exist_ok=True)
    for rel in ('DKR-R.exe', 'libexec/dkr-r/DKR-R-Runtime.exe', 'libexec/dkr-r/DKR-R-ModWorker.exe',
                'libexec/dkr-r/DKR-R-InputHost.exe', 'libexec/dkr-r/DKR-R-Updater.exe'):
        for path in (windows_binaries / rel, (windows_binaries / rel).with_suffix('.pdb')):
            if path.is_file():
                shutil.copy2(path, symbols / path.name)
    history = AUTH / 'history/RELEASE-VALIDATION-1.0.7.txt'
    if not history.exists():
        history.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(ROOT / 'RELEASE-VALIDATION.md', history)
    (ROOT / 'RELEASE-VALIDATION.md').write_text('''# v1.0.8 release build record

Windows x64, Linux x86_64/ARM64 and Android ARM64 compile/link and static package
inspection completed. Steam Deck uses the exact Linux x86_64 AppImage bytes.
All four native builds share both revision-specific owned online identities.
Android retains the existing certificate/application ID, with versionCode 1080001.
Linux ABI requirements remain GLIBC 2.38 and GLIBCXX_3.4.32.

The compiled Windows runtime reserves 4 GiB but commits only the original
512 MiB usable RDRAM. The untouched POSIX/runtime remainder and staged source
selection were inspected on every build. No guest size, save, shader, SDK or
accepted water-body change. Checked-out dependencies/generated guest code stay
read-only; the normal Patch Pipeline composes the dependency fix in build copies.

No game, launcher, mod, unit suite, replay or benchmark was executed. Owner
confirmation on the affected PC and device gameplay acceptance remain pending.
The old logs identify the allocation path, not the precise Windows error/RAM
state. The new diagnostic retains the error number for a genuine future failure.
Manual downloads are complete. Automatic-update signatures still need the
existing private release signing key; it was not replaced. No push/tag/publish
was performed as an implicit part of this build pass.

Private evidence: ''' + work.as_posix() + '''/evidence.
The [preceding stable build record](patches/startup-memory/history/RELEASE-VALIDATION-1.0.7.txt)
is preserved as history, not reclassified as new gameplay acceptance.
''', encoding='utf-8', newline='\n')
    print('Complete v1.0.8 delivery collected:', output)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('action', choices=('source', 'apk', 'collect'))
    for name in ('build', 'work', 'output', 'windows-binaries', 'apk', 'published', 'beta', 'tools'):
        p.add_argument('--' + name, type=Path)
    a = p.parse_args()
    if a.action == 'source':
        source(a.build, a.output)
    elif a.action == 'apk':
        apk(a)
    else:
        collect(a.work, a.output, a.windows_binaries)


if __name__ == '__main__':
    main()
