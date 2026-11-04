"""Compose a website/API-only update from pinned, read-only portal sources."""
from pathlib import Path
import argparse
import difflib
import hashlib
import json
import shutil
import subprocess
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parents[2]
AUTH = Path(__file__).resolve().parent
CAPSULE = ROOT / 'patches/release-prep/v1.0.7/source-capsule-r2'
REVISION = 'library-20261022-workflow-1'
OBJECTS = {
    'app.js': '1831349d7ab11e40a3fe9cefc1b5a389459b9588c1661f5fba602b4e475940ca',
    'api-store.js': '64bc62e4a41028f710a63ff11e0bbc9e9f3096744b4172a3865b69cc9a5d6e96',
    'uploads-client.js': '9de9a251b6522ff4162e1bce04c8d35365b2eeac41ec7a75e96c7164eb6d9f93',
    'worker-single-file.mjs': 'cc32731c1f72abf9b0629cb5eb49da19ae33661bed607020b999ecd7a027e348',
}
SNAPSHOTS = {
    'index.html': '04ea98e6120649c234dc21c9d2e639e82945de58af1f1de675c56ad423bfc4e0',
    'assets/hosted-files.js': '716be714d89df60e19c40b80cf1ab954f684c665b14945ec720859d91bbeefe0',
}


def sha(raw):
    return hashlib.sha256(raw).hexdigest()


def once(text, before, after):
    if text.count(before) != 1:
        raise ValueError('Portal anchor changed: ' + before[:100])
    return text.replace(before, after)


def frozen(name):
    digest = OBJECTS[name]
    raw = (CAPSULE / (digest + '.txt')).read_bytes()
    if sha(raw) != digest:
        raise ValueError('Frozen source changed: ' + name)
    return raw.decode('utf-8')


def capture():
    for name, digest in SNAPSHOTS.items():
        target = AUTH / 'baseline' / name
        if target.exists():
            raw = target.read_bytes()
        else:
            raw = urllib.request.urlopen('https://positron-portal.com/dkr-r/' + name, timeout=30).read()
        if sha(raw) != digest:
            raise ValueError('Deployed baseline differs; review before rebasing: ' + name)
        if not target.exists():
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(raw)
    print('Public baseline snapshots verified; no remote writes.')


def authors():
    return {p.relative_to(AUTH).as_posix(): sha(p.read_bytes()) for p in sorted(AUTH.rglob('*'))
            if p.is_file() and '__pycache__' not in p.parts and p.suffix != '.pyc'}


def compose(output):
    import importlib.util
    if output.exists() or not output.resolve().is_relative_to(ROOT / 'build'):
        raise ValueError('Choose a NEW stage inside build/.')
    for name, digest in SNAPSHOTS.items():
        if sha((AUTH / 'baseline' / name).read_bytes()) != digest:
            raise ValueError('Baseline changed: ' + name)
    spec = importlib.util.spec_from_file_location('workflow_transforms', AUTH / 'transforms.py')
    transforms = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(transforms)
    ftp = output / 'FTP-PATCH'
    assets = ftp / 'assets'
    assets.mkdir(parents=True)
    for name in ('app.js', 'api-store.js', 'uploads-client.js'):
        (assets / name).write_text(transforms.transform(name, frozen(name)), encoding='utf-8', newline='\n')
    hosted = (AUTH / 'baseline/assets/hosted-files.js').read_text(encoding='utf-8')
    (assets / 'hosted-files.js').write_text(transforms.hosted(hosted), encoding='utf-8', newline='\n')
    for name in ('batch-client.js', 'workflow.css'):
        shutil.copy2(AUTH / name, assets / name)
    index = (AUTH / 'baseline/index.html').read_text(encoding='utf-8').replace('library-20261018-uploads-1', REVISION)
    index = once(index, 'characters, legacy mods, magic codes', 'characters, vehicles, magic codes')
    index = once(index, '  <script src="assets/app.js',
                 f'  <link rel="stylesheet" href="assets/workflow.css?v={REVISION}">\n'
                 f'  <script src="assets/batch-client.js?v={REVISION}" defer></script>\n  <script src="assets/app.js')
    (ftp / 'index.html').write_text(index, encoding='utf-8', newline='\n')
    backend = output / 'PRIVATE-API'
    backend.mkdir()
    (backend / 'worker-single-file.mjs').write_text(transforms.worker(frozen('worker-single-file.mjs')), encoding='utf-8', newline='\n')
    shutil.copy2(AUTH / 'workflow-schema.sql', backend / 'workflow-schema.sql')
    for name in ('DEPLOYMENT.md', 'RELEASE-NOTES.txt', 'QA.md'):
        shutil.copy2(AUTH / name, output / name)
    review = []
    for name in ('app.js', 'api-store.js', 'uploads-client.js', 'worker-single-file.mjs'):
        relative = 'PRIVATE-API/' + name if name.endswith('.mjs') else 'FTP-PATCH/assets/' + name
        review.extend(difflib.unified_diff(frozen(name).splitlines(True), (output / relative).read_text(encoding='utf-8').splitlines(True), fromfile='before/' + relative, tofile='after/' + relative))
    for name in SNAPSHOTS:
        review.extend(difflib.unified_diff((AUTH / 'baseline' / name).read_text(encoding='utf-8').splitlines(True), (ftp / name).read_text(encoding='utf-8').splitlines(True), fromfile='before/FTP-PATCH/' + name, tofile='after/FTP-PATCH/' + name))
    for name in ('batch-client.js', 'workflow.css'):
        review.extend(difflib.unified_diff([], (assets / name).read_text(encoding='utf-8').splitlines(True), fromfile='/dev/null', tofile='after/FTP-PATCH/assets/' + name))
    (output / 'REVIEW.patch').write_text(''.join(review), encoding='utf-8', newline='\n')
    rollback = output / 'ROLLBACK'
    (rollback / 'FTP-PATCH/assets').mkdir(parents=True)
    for name in ('app.js', 'api-store.js', 'uploads-client.js'):
        (rollback / 'FTP-PATCH/assets' / name).write_text(frozen(name), encoding='utf-8', newline='\n')
    for name in SNAPSHOTS:
        shutil.copy2(AUTH / 'baseline' / name, rollback / 'FTP-PATCH' / name)
    (rollback / 'PRIVATE-API').mkdir()
    (rollback / 'PRIVATE-API/worker-single-file.mjs').write_text(frozen('worker-single-file.mjs'), encoding='utf-8', newline='\n')
    files = {p.relative_to(output).as_posix(): sha(p.read_bytes()) for p in sorted(output.rglob('*')) if p.is_file()}
    (output / 'WORKFLOW-RECEIPT.json').write_text(json.dumps({
        'schema': 1, 'revision': REVISION, 'authored': authors(), 'inputs': OBJECTS,
        'policy': 'Website/API only. Launcher, FTP transport, submodules and generated recomp files unchanged.',
        'files': files}, indent=2) + '\n', encoding='utf-8')
    verify(output)


def verify(output):
    receipt = json.loads((output / 'WORKFLOW-RECEIPT.json').read_text(encoding='utf-8'))
    if receipt['authored'] != authors() or receipt['inputs'] != OBJECTS:
        raise ValueError('Patch inputs changed; compose a fresh stage.')
    files = {p.relative_to(output).as_posix(): sha(p.read_bytes()) for p in sorted(output.rglob('*'))
             if p.is_file() and p.name != 'WORKFLOW-RECEIPT.json'}
    if files != receipt['files']:
        raise ValueError('Staged membership or bytes changed.')
    print('Website/API stage verified; protected sources and launcher untouched.')


def package(output, archive):
    verify(output)
    if archive.exists():
        raise ValueError('Choose a new archive path.')
    archive.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(archive, 'x', zipfile.ZIP_DEFLATED) as z:
        for p in sorted(output.rglob('*')):
            if p.is_file():
                z.write(p, p.relative_to(output).as_posix())
    print(str(archive), sha(archive.read_bytes()))


def compose_community(output, archive, baseline, esbuild):
    """Package maintained first-party website/API sources through this entry point."""
    source = ROOT / 'runtime-recomp/src/game/mod_library/website'
    backend = ROOT / 'runtime-recomp/src/game/online_profile/backend'
    output = output.resolve()
    if output.exists() or not output.is_relative_to(ROOT / 'build'):
        raise ValueError('Choose a NEW community deployment directory inside build/.')
    if not baseline or not (baseline / 'PRIVATE-API/worker.mjs').is_file():
        raise ValueError('Supply the accepted website/Worker rollback directory with --baseline.')
    if not esbuild.is_file():
        raise ValueError('Install the documented local esbuild tool or pass --esbuild.')
    if archive and (archive.exists() or not archive.resolve().is_relative_to(ROOT / 'build')):
        raise ValueError('Choose a NEW deployment archive inside build/.')
    ftp = output / 'FTP-PATCH'
    private = output / 'PRIVATE-API'
    rollback = output / 'ROLLBACK'
    private.mkdir(parents=True)
    review = []
    for path in sorted(source.rglob('*')):
        if not path.is_file():
            continue
        relative = path.relative_to(source)
        target = ftp / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(path, target)
        old = baseline / 'FTP-PATCH' / relative
        if old.is_file():
            backup = rollback / 'FTP-PATCH' / relative
            backup.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(old, backup)
        review.extend(difflib.unified_diff(
            old.read_text(encoding='utf-8').splitlines(True) if old.is_file() else [],
            path.read_text(encoding='utf-8').splitlines(True),
            fromfile='before/FTP-PATCH/' + relative.as_posix(),
            tofile='after/FTP-PATCH/' + relative.as_posix()))
        if path.suffix == '.js':
            subprocess.run(['node', '--check', str(path)], check=True)
    for directory, destination in ((backend, private), (baseline / 'PRIVATE-API', rollback / 'PRIVATE-API')):
        destination.mkdir(parents=True, exist_ok=True)
        subprocess.run(['node', str(esbuild), str(directory / 'worker.mjs'), '--bundle',
                        '--format=esm', '--platform=neutral', '--target=es2022', '--external:cloudflare:*',
                        '--legal-comments=inline',
                        '--outfile=' + str(destination / 'worker-single-file.mjs')], check=True)
    shutil.copyfile(backend / 'community_schema.sql', private / 'community_schema.sql')
    for name in ('worker.mjs', 'library_api.mjs', 'community_api.mjs', 'community_schema.sql'):
        old = baseline / 'PRIVATE-API' / name
        review.extend(difflib.unified_diff(
            old.read_text(encoding='utf-8').splitlines(True) if old.is_file() else [],
            (backend / name).read_text(encoding='utf-8').splitlines(True),
            fromfile='before/PRIVATE-API/' + name, tofile='after/PRIVATE-API/' + name))
    (output / 'REVIEW.patch').write_text(''.join(review), encoding='utf-8', newline='\n')
    shutil.copyfile(ROOT / 'docs/MOD_LIBRARY_WEBSITE.md', output / 'DEPLOYMENT.md')
    if archive:
        archive.parent.mkdir(parents=True, exist_ok=True)
        with zipfile.ZipFile(archive, 'x', zipfile.ZIP_DEFLATED) as package_zip:
            for path in sorted(output.rglob('*')):
                if path.is_file():
                    package_zip.write(path, path.relative_to(output).as_posix())
        print(archive)
    print('Community website/API package created from maintained sources; no game build or deployment.')


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('action', choices=('capture', 'stage', 'verify', 'package', 'community'))
    p.add_argument('--output', type=Path)
    p.add_argument('--archive', type=Path)
    p.add_argument('--baseline', type=Path)
    p.add_argument('--esbuild', type=Path,
                   default=ROOT / 'build/library-community-tools/node_modules/esbuild/bin/esbuild')
    a = p.parse_args()
    if a.action == 'community':
        compose_community(a.output, a.archive, a.baseline, a.esbuild)
    elif a.action == 'capture':
        capture()
    elif a.action == 'stage':
        compose(a.output)
    elif a.action == 'verify':
        verify(a.output)
    else:
        package(a.output, a.archive)
