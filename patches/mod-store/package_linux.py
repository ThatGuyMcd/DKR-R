"""Repackage the accepted Linux AppDir without executing DKR-R or AppImage code."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys

sys.dont_write_bytecode = True
AUTH = Path(__file__).resolve().parent
VERSION = '1.0.7+beta.9'


def digest(path):
    value = hashlib.sha256()
    with path.open('rb') as source:
        for block in iter(lambda: source.read(1024 * 1024), b''):
            value.update(block)
    return value.hexdigest()


def elf(path):
    header = path.read_bytes()[:64]
    if header[:6] != b'\x7fELF\x02\x01' or struct.unpack_from('<H', header, 18)[0] != 62:
        raise ValueError('Expected Linux x86_64 image: ' + str(path))


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for name in ('stage', 'template', 'runtime-image', 'compiled', 'output', 'build-log'):
        p.add_argument('--' + name, type=Path, required=True)
    a = p.parse_args()
    subprocess.run([sys.executable, '-B', str(AUTH / 'pipeline.py'), 'verify', '--output', str(a.stage)], check=True)
    if a.output.exists():
        raise ValueError('Choose a fresh Linux delivery directory')
    if a.output.resolve().is_relative_to(a.template.resolve()):
        raise ValueError('Delivery cannot be inside its template')
    log = a.build_log.read_text(errors='replace')
    if 'Linking CXX executable bin/Release/DKR-R' not in log:
        raise ValueError('Missing final Linux runtime link')
    a.output.mkdir(parents=True)
    appdir = a.output / ('DKR-R-' + VERSION + '-Linux-x86_64.AppDir')
    for path in a.template.rglob('*'):
        if path.is_symlink() and not path.resolve(strict=True).is_relative_to(a.template.resolve()):
            raise ValueError('Template symlink escapes its AppDir: ' + str(path))
    shutil.copytree(a.template, appdir, symlinks=True)
    binaries = {}
    for source, relative in ((a.compiled / 'DKR-R', 'usr/bin/DKR-R'),
                              (a.compiled / 'libexec/dkr-r/DKR-R-ModWorker', 'usr/libexec/dkr-r/DKR-R-ModWorker'),
                              (a.compiled / 'libexec/dkr-r/DKR-R-Updater', 'usr/libexec/dkr-r/DKR-R-Updater')):
        elf(source)
        target = appdir / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target)
        target.chmod(0o755)
        binaries[relative] = digest(target)
    runtime = (appdir / 'usr/bin/DKR-R').read_bytes()
    # GCC can inline/split short filesystem literals into instruction immediates.
    # A complete long diagnostic proves the registry module is linked without
    # depending on the compiler's representation of its short filename.
    for marker in (VERSION.encode(), b'The mod installation registry needs recovery; it was not replaced.', b'install.lock', b'/auth/launcher/start',
                   b'This installation choice is no longer available. Cancel and retry the download.',
                   b'An installation receipt has an unsafe path (',
                   b'An interrupted retirement is missing its original and recovery group.',
                   b'The track ZIP could not be opened: ',
                   b'The track package contains more than one course. Import each .dkrmap separately.',
                   b'This listing has unsaved changes. Keep editing, or discard them before leaving.',
                   b'Clears completed entries only. Installed mods and cached packages are kept.',
                   b'This copy is installed separately. Your other copy has not been changed.',
                   b'frozen offline EEPROM verified; no mutable route switch'):
        if marker not in runtime:
            raise ValueError('Missing runtime version/feature/startup marker')
    shutil.copy2(AUTH / 'RELEASE-NOTES.txt', appdir / 'RELEASE-NOTES.txt')
    (appdir / 'README.txt').write_text('DKR-R v1.0.7 beta 9 - Linux x86_64 preview\n\n'
        'Make the AppImage executable and launch it. Bring your own original ROM.\n'
        'Open MODS / HACKS > Mod Store. See RELEASE-NOTES.txt for this update.\n'
        'Compilation and static packaging inspection only; owner acceptance remains.\n')
    # Reuse only the verified outer runtime from the accepted AppImage. Find
    # its real squashfs superblock with a filesystem tool, not --appimage-*.
    elf(a.runtime_image)
    original = a.runtime_image.read_bytes()
    if original[8:11] != b'AI\x02':
        raise ValueError('Expected a type-2 AppImage runtime')
    offset = None
    for match in re.finditer(b'hsqs', original[:4 * 1024 * 1024]):
        candidate = match.start()
        result = subprocess.run(['unsquashfs', '-s', '-o', str(candidate), str(a.runtime_image)], capture_output=True)
        if result.returncode == 0:
            offset = candidate
            break
    if offset is None:
        raise ValueError('Could not identify the accepted AppImage payload boundary')
    squash = a.output / ('DKR-R-' + VERSION + '-Linux-x86_64.squashfs')
    subprocess.run(['mksquashfs', str(appdir), str(squash), '-noappend', '-comp', 'zstd', '-processors', '4', '-no-progress'], check=True)
    subprocess.run(['unsquashfs', '-s', str(squash)], check=True)
    image = a.output / ('DKR-R-' + VERSION + '-Linux-x86_64.AppImage')
    with image.open('xb') as out:
        out.write(original[:offset])
        with squash.open('rb') as source:
            shutil.copyfileobj(source, out)
    image.chmod(0o755)
    subprocess.run(['unsquashfs', '-s', '-o', str(offset), str(image)], check=True)
    forbidden = {'.z64', '.n64', '.v64', '.eep', '.mpk', '.sra', '.fla', '.pdb', '.o2r', '.otr'}
    for path in appdir.rglob('*'):
        if path.is_file() and path.suffix.lower() in forbidden:
            raise ValueError('Unexpected private or game data in AppDir: ' + str(path))
    files = {path.relative_to(appdir).as_posix(): digest(path) for path in sorted(appdir.rglob('*')) if path.is_file()}
    report = {'schema': 1, 'version': VERSION, 'platform': 'linux-x86_64',
        'validation': 'Compilation and static ELF/AppDir/squashfs inspection only; no application execution.',
        'binaries': binaries, 'files': files, 'source_receipt': json.loads((a.stage / 'mod-store-receipt.json').read_text()),
        'build_log_sha256': digest(a.build_log), 'template_runtime_sha256': digest(a.runtime_image),
        'appimage': {'file': image.name, 'sha256': digest(image), 'bytes': image.stat().st_size}}
    (a.output / 'BUILD-RECEIPT.json').write_text(json.dumps(report, indent=2) + '\n')
    (a.output / 'SHA256SUMS.txt').write_text(digest(image) + '  ' + image.name + '\n')
    print('Prepared Linux AppImage without launching it: ' + str(image))


if __name__ == '__main__':
    main()
