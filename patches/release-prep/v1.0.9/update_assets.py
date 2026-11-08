"""Prepare, sign and statically verify the finished v1.0.9 release update assets.

Uses the existing updater format, signer and pinned public key. Never generates
a replacement key, changes a package, publishes a release or executes a target.
"""
from pathlib import Path
import argparse
import hashlib
import importlib.util
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from types import SimpleNamespace

sys.dont_write_bytecode = True
AUTH = Path(__file__).resolve().parent
ROOT = AUTH.parents[2]
VERSION = '1.0.9'
TAG = 'Version1.0.9'
EXPECTED = {
    'windows-x64': ('DKR-R-1.0.9-Windows-x64.zip', 59387124, '02e6b1071ff447f33ac8cc82ad9ff9d462bd6575f76be1da1a19f6eb12ff903c'),
    'linux-x86_64': ('DKR-R-1.0.9-Linux-x86_64.AppImage', 68254200, 'b8c4e57451289d4847ac4b32ede90be33cc25293229c693ee4f795ad9dca1913'),
    'linux-aarch64': ('DKR-R-1.0.9-Linux-aarch64.AppImage', 58915336, '0ccf0736ab9ad776695b27d1f16e333ea2a84b7e7ab05faa73dafb5416538547'),
    'android-arm64': ('DKR-R-1.0.9-Android-arm64.apk', 48167287, '359a4904827fe5c71d57a45723e8b889b921a30ea6159424fc86aa2b86a77329'),
}
DECK = 'DKR-R-1.0.9-SteamDeck-x86_64.AppImage'
TRUSTED = 'c6673fd87da3c3c7fb4991b81476a88a316e2a4b2960bde664d7c1cffdd0bbc6'


def load_signer():
    spec = importlib.util.spec_from_file_location('dkrr_existing_release_signer', ROOT / 'scripts/update_release.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def openssl():
    found = shutil.which('openssl')
    if not found and os.name == 'nt':
        for name in ('C:/Program Files/Git/usr/bin/openssl.exe', 'C:/Program Files/Git/mingw64/bin/openssl.exe'):
            if Path(name).is_file():
                found = name
                os.environ['PATH'] = str(Path(name).parent) + os.pathsep + os.environ.get('PATH', '')
                break
    if not found:
        raise ValueError('OpenSSL with Ed25519 support is required; no private key is generated.')
    return found


def trust():
    key = json.loads((ROOT / 'packaging/update-public-key.json').read_text(encoding='utf-8'))
    if key.get('public_key') != TRUSTED or key.get('algorithm') != 'Ed25519':
        raise ValueError('The existing trusted updater key changed; do not rotate it implicitly.')
    return bytes.fromhex(TRUSTED)


def verify_signature(manifest, signature):
    raw = manifest.read_bytes()
    encoded = signature.read_text(encoding='ascii')
    if not re.fullmatch('[0-9a-fA-F]{128}', encoded):
        raise ValueError('The signature must be exactly 128 hexadecimal characters.')
    with tempfile.TemporaryDirectory(prefix='dkrr-public-verification-') as folder:
        base = Path(folder)
        # This temporary key is PUBLIC SPKI DER; private key material is never copied.
        public = base / 'public.der'
        public.write_bytes(bytes.fromhex('302a300506032b6570032100') + trust())
        detached = base / 'signature.bin'
        detached.write_bytes(bytes.fromhex(encoded))
        command = [openssl(), 'pkeyutl', '-verify', '-pubin', '-keyform', 'DER',
                   '-inkey', str(public), '-rawin', '-in', str(manifest), '-sigfile', str(detached)]
        run = subprocess.run(command, capture_output=True)
        if run.returncode:
            raise ValueError('Signature does not verify against the installed launcher trust key.')
    return json.loads(raw)


def committed_source(commit):
    if not re.fullmatch('[0-9a-f]{40}', commit):
        raise ValueError('Use the full committed release source SHA, not a branch name or placeholder.')
    def git(*args):
        return subprocess.check_output(['git', *args], cwd=ROOT)
    if git('show', commit + ':VERSION').decode().strip() != VERSION:
        raise ValueError('Selected source commit is not the stable v1.0.9 tree.')
    # A source commit that omits newly authored native layers is not a true
    # identity for the built release. Local runtime caches and earlier plans
    # are not release source and are intentionally excluded.
    allowed_local = ('.wrangler/', 'patches/lobby-discovery-plan-20261023/', 'patches/lobby-discovery-plan-20261024/')
    untracked = git('ls-files', '-z', '--others', '--exclude-standard').split(b'\0')
    omitted = [p.decode() for p in untracked if p and not p.decode().startswith(allowed_local)]
    if omitted:
        raise ValueError('Commit the complete release source before signing; omitted paths: ' + ', '.join(omitted[:5]))
    changed = git('diff', '--name-only', commit, '--').decode().splitlines()
    if changed:
        raise ValueError('Working source differs from the selected release commit: ' + ', '.join(changed[:5]))
    return commit


def manifest_value(packages, commit):
    trust()
    signer = load_signer()
    artifacts = []
    for platform, (name, size, checksum) in EXPECTED.items():
        path = packages / name
        if not path.is_file() or path.stat().st_size != size or signer.digest(path) != checksum:
            raise ValueError('Finished release package is missing or changed: ' + name)
        kind = signer.PLATFORMS[platform]
        item = {'platform': platform, 'name': name, 'kind': kind, 'bytes': size,
                'sha256': checksum, 'files': []}
        if platform.startswith('linux-'):
            item['minimum_glibc'] = '2.38'
        else:
            item['minimum_os'] = '10.0' if platform == 'windows-x64' else '28'
        if kind == 'zip':
            item['root'], item['files'] = signer.zip_table(path, platform)
        artifacts.append(item)
    deck = packages / DECK
    _, size, checksum = EXPECTED['linux-x86_64']
    if not deck.is_file() or deck.stat().st_size != size or signer.digest(deck) != checksum:
        raise ValueError('Steam Deck must be the exact qualified Linux x86_64 payload.')
    return {'schema': 1, 'minimum_updater': 1, 'version': VERSION, 'tag': TAG,
            'channel': 'stable', 'commit': commit,
            'notes': 'https://github.com/ThatGuyMcd/DKR-R/releases/tag/' + TAG,
            'artifacts': artifacts}


def receipt(output, manifest, signature, value):
    report = {'schema': 1, 'version': VERSION, 'tag': TAG, 'commit': value['commit'],
              'signature': 'verified-Ed25519-existing-pinned-key',
              'manifest_sha256': hashlib.sha256(manifest.read_bytes()).hexdigest(),
              'signature_file_sha256': hashlib.sha256(signature.read_bytes()).hexdigest(),
              'packages': [{'name': x['name'], 'bytes': x['bytes'], 'sha256': x['sha256']} for x in value['artifacts']],
              'steam_deck': 'identical-to-linux-x86_64', 'native_execution': False,
              'published': False}
    target = output / 'DKR-R-update-VERIFICATION.json'
    with target.open('x', encoding='utf-8', newline='\n') as stream:
        stream.write(json.dumps(report, indent=2) + '\n')
    return report


def main(args):
    if args.action == 'verify-signature':
        value = verify_signature(args.manifest, args.signature)
        print('Existing trusted-key signature verified:', value.get('version'), value.get('tag'))
        return
    signer = load_signer()
    output = args.output.resolve()
    packages = args.packages.resolve()
    if args.action == 'verify':
        manifest, signature = output / 'DKR-R-update.json', output / 'DKR-R-update.sig'
        value = verify_signature(manifest, signature)
        expected = manifest_value(packages, value['commit'])
        if value != expected or manifest.read_bytes() != signer.canonical(expected):
            raise ValueError('Signed manifest differs from the qualified v1.0.9 packages/canonical format.')
        print('Verified signature, canonical manifest, four platform entries, Windows ownership/file table and five package hashes.')
        return
    value = manifest_value(packages, committed_source(args.commit))
    output.mkdir(parents=True, exist_ok=True)
    if args.action == 'prepare':
        target = output / 'DKR-R-update.UNSIGNED.json'
        with target.open('xb') as stream:
            stream.write(signer.canonical(value))
        print('Prepared review manifest:', target)
        print('Unsigned: obtain the existing private Ed25519 key to create the installable pair.')
        return
    if not args.private_key or not args.private_key.is_file():
        raise ValueError('Signing needs the existing private Ed25519 key path, not an old JSON/.sig or the public key.')
    private = args.private_key.resolve()
    if private.is_relative_to(ROOT) or private.is_relative_to(output) or private.is_relative_to(packages):
        raise ValueError('Keep the private key outside source and release/output folders.')
    openssl()
    # The unchanged signer checks public-key continuity before writing either asset.
    signer.create_manifest(SimpleNamespace(
        private_key=str(private), verification_key=str(ROOT / 'packaging/update-public-key.json'),
        version=VERSION, tag=TAG, commit=value['commit'], channel='stable', output=str(output),
        artifact=[p + '=' + str(packages / row[0]) for p, row in EXPECTED.items()], minimum_os=[]))
    manifest, signature = output / 'DKR-R-update.json', output / 'DKR-R-update.sig'
    signed = verify_signature(manifest, signature)
    if signed != value or manifest.read_bytes() != signer.canonical(value):
        raise ValueError('Signed output did not match the preflighted manifest.')
    print(json.dumps(receipt(output, manifest, signature, signed), indent=2))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=('prepare', 'sign', 'verify', 'verify-signature'))
    parser.add_argument('--packages', type=Path)
    parser.add_argument('--commit')
    parser.add_argument('--private-key', type=Path)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--manifest', type=Path)
    parser.add_argument('--signature', type=Path)
    args = parser.parse_args()
    required = ('manifest', 'signature') if args.action == 'verify-signature' else ('packages', 'output')
    if args.action in ('prepare', 'sign'):
        required += ('commit',)
    for name in required:
        if getattr(args, name) is None:
            parser.error('--' + name + ' is required for ' + args.action)
    try:
        main(args)
    except (ValueError, OSError, KeyError, json.JSONDecodeError, subprocess.CalledProcessError) as error:
        parser.exit(1, str(error) + '\n')
