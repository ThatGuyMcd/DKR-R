"""Assemble a Windows preview from the accepted package and new compiled images.

No target program is executed. Inputs and stages are never modified.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import zipfile

sys.dont_write_bytecode = True
AUTH = Path(__file__).resolve().parent
ROOT = AUTH.parents[1]
VERSION = '1.0.7+beta.9'
NAME = f'DKR-R-{VERSION}-Windows-x64'
DENIED = {'.z64', '.v64', '.n64', '.eep', '.mpk', '.sra', '.fla', '.o2r', '.otr', '.pdb', '.dkr-probe', '.dkr-bootstrap', '.dkr-component'}


def digest(path):
    value = hashlib.sha256()
    with path.open('rb') as stream:
        for part in iter(lambda: stream.read(1024 * 1024), b''):
            value.update(part)
    return value.hexdigest()


def pe(path, version=False):
    data = path.read_bytes()
    if data[:2] != b'MZ':
        raise ValueError('Missing Windows image: ' + str(path))
    at = struct.unpack_from('<I', data, 0x3c)[0]
    if data[at:at + 4] != b'PE\0\0' or struct.unpack_from('<H', data, at + 4)[0] != 0x8664:
        raise ValueError('Not a Windows x64 image: ' + str(path))
    if version and VERSION.encode('utf-16le') not in data:
        raise ValueError('Windows version metadata is stale: ' + str(path))


def archive(directory, output):
    with zipfile.ZipFile(output, 'x', zipfile.ZIP_DEFLATED, compresslevel=6) as target:
        for path in sorted(directory.rglob('*')):
            if path.is_file():
                target.write(path, directory.name + '/' + path.relative_to(directory).as_posix())
    with zipfile.ZipFile(output) as target:
        if target.testzip():
            raise ValueError('Archive CRC verification failed')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('stage', 'template', 'compiled', 'output', 'build-log'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--windows-only', action='store_true', help='Do not republish the unchanged server companion')
    args = parser.parse_args()
    subprocess.run([sys.executable, '-B', str(AUTH / 'pipeline.py'), 'verify', '--output', str(args.stage)], check=True)
    log = args.build_log.read_text(encoding='utf-8-sig', errors='replace')
    if 'DKRPortGame.vcxproj ->' not in log or 'DKRLegacyModWorker.vcxproj ->' not in log:
        raise ValueError('The final compile log does not show both native links')
    receipt = json.loads((args.stage / 'mod-store-receipt.json').read_text())
    template = args.template.resolve(strict=True)
    binary = args.compiled.resolve(strict=True) / 'bin/RelWithDebInfo'
    output = args.output.resolve()
    if output.exists():
        raise ValueError('Choose a NEW delivery directory')
    if output.is_relative_to(template) or output.is_relative_to(args.compiled.resolve()):
        raise ValueError('Keep delivery separate from its inputs')
    for source in template.rglob('*'):
        if source.is_symlink() or (source.is_file() and source.suffix.lower() in DENIED):
            raise ValueError('Unexpected template entry: ' + str(source))
    output.mkdir(parents=True)
    package = output / NAME
    shutil.copytree(template, package)
    replacements = ['DKR-R.exe'] + ['libexec/dkr-r/' + name for name in (
        'DKR-R-Runtime.exe', 'DKR-R-InputHost.exe', 'DKR-R-ModWorker.exe', 'DKR-R-Updater.exe',
        'SDL2.dll', 'SDL3.dll', 'dxcompiler.dll', 'dxil.dll')]
    binaries = {}
    for relative in replacements:
        source = binary / relative
        pe(source, relative in ('DKR-R.exe', 'libexec/dkr-r/DKR-R-Runtime.exe'))
        shutil.copy2(source, package / relative)
        binaries[relative] = digest(source)
    runtime = (package / 'libexec/dkr-r/DKR-R-Runtime.exe').read_bytes()
    for marker in (b'positron-portal.com', b'/auth/launcher/start', b'installed-names.json',
                   b'installations.json', b'install.lock', b'Waiting for your duplicate choice',
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
            raise ValueError('New native feature or accepted startup fix is absent')
    for forbidden in (b'DKR_REPLAY_CAPTURE_FILE', b'DKR_UPDATE_TEST_VERIFIER', b'DKR_WATER_TEST_MAP'):
        if forbidden in runtime:
            raise ValueError('Private execution fixture in public runtime')
    notes = (AUTH / 'RELEASE-NOTES.txt').read_text(encoding='utf-8')
    (package / 'RELEASE-NOTES.txt').write_text(notes, encoding='utf-8')
    (package / 'README.txt').write_text(
        'DKR-R v1.0.7 beta 9 - Windows preview\n\n'
        'Extract the entire folder, then launch DKR-R.exe. Keep libexec and assets in place.\n'
        'Add your own supported original ROM when asked. No ROM or save is included.\n\n'
        'Open MODS / HACKS > Mod Store. Choose either Store view in its View selector.\n'
        'Use Installed, Updates and Duplicates to manage Store-linked mods.\n'
        'Read RELEASE-NOTES.txt for update, duplicate and recovery behaviour.\n'
        'Do not publish this preview as a fully verified all-platform release.\n'
        'The owner performs gameplay and account/install acceptance. No such testing was run here.\n', encoding='utf-8')
    subprocess.run([sys.executable, '-B', str(ROOT / 'scripts/update_release.py'), 'prepare-tree',
                    '--directory', str(package), '--version', VERSION, '--platform', 'windows-x64'], check=True)
    files = {}
    expected_exes = {name for name in replacements if name.endswith('.exe')}
    for path in sorted(package.rglob('*')):
        if not path.is_file():
            continue
        relative = path.relative_to(package).as_posix()
        if path.is_symlink() or path.suffix.lower() in DENIED:
            raise ValueError('Unexpected distribution entry: ' + relative)
        if path.suffix.lower() == '.exe' and relative not in expected_exes:
            raise ValueError('Unexpected executable: ' + relative)
        with path.open('rb') as stream:
            if stream.read(4).hex() in ('80371240', '37804012', '40123780'):
                raise ValueError('N64 ROM found in package')
        files[relative] = digest(path)
    archive(package, output / (NAME + '.zip'))
    if not args.windows_only:
        server = output / 'DKR-R-Mod-Store-Server-beta9'
        server.mkdir()
        shutil.copytree(args.stage / 'backend', server / 'CLOUDFLARE')
        shutil.copytree(args.stage / 'FTP-PATCH', server / 'FTP-PATCH')
        shutil.copy2(AUTH / 'backend/OWNER-SETUP.txt', server / 'READ-ME-FIRST.txt')
        shutil.copy2(args.stage / 'mod-store-receipt.json', server / 'SOURCE-RECEIPT.json')
        archive(server, output / 'DKR-R-Mod-Store-Server-beta9.zip')
    report = {
        'schema': 1, 'version': VERSION, 'platform': 'windows-x64',
        'validation': 'Compilation, syntax and static package inspection only. No application or gameplay execution.',
        'remote_deployed': False, 'other_platforms_built': False, 'server_update_included': not args.windows_only,
        'authored_changes': 'patches/mod-store only; protected dependencies and generated game sources untouched',
        'template': str(template), 'template_files': {p.relative_to(template).as_posix(): digest(p) for p in template.rglob('*') if p.is_file()},
        'source_receipt': receipt, 'binaries': binaries, 'windows_files': files,
        'build_log_sha256': digest(args.build_log),
        'archives': {p.name: {'sha256': digest(p), 'bytes': p.stat().st_size} for p in output.glob('*.zip')}
    }
    (output / 'BUILD-RECEIPT.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    (output / 'SHA256SUMS.txt').write_text(''.join(digest(p) + '  ' + p.name + '\n' for p in sorted(output.iterdir()) if p.is_file()), encoding='ascii')
    print('Prepared Windows preview' + ('' if args.windows_only else ' and separate server update') + ': ' + str(output))


if __name__ == '__main__':
    main()
