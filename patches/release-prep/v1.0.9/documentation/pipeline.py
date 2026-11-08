"""Apply the v1.0.9 documentation overlay with a recoverable source snapshot.

All repository documentation changes are authored here, never in dependencies
or generated recompilation trees. No game, launcher or native tests are run.
"""
from pathlib import Path
import argparse
import hashlib
import importlib.util
import json
import re
import subprocess
import zipfile

AUTH = Path(__file__).resolve().parent
ROOT = AUTH.parents[3]
PROTECTED = ('extern/', 'runtime-recomp/RecompiledFuncs/', 'runtime-recomp/RecompiledPatches/')


def sha(data):
    return hashlib.sha256(data).hexdigest()


def files():
    raw = subprocess.check_output(['git', 'ls-files', '-z', '--cached', '--others', '--exclude-standard'], cwd=ROOT)
    return sorted({p.decode('utf-8') for p in raw.split(b'\0') if p and
                   not p.decode('utf-8').startswith(('.wrangler/',) + PROTECTED)
                   and (ROOT / p.decode('utf-8')).is_file()})


def document(name):
    return Path(name).suffix.lower() in {'.md', '.txt', '.rst'} and Path(name).name not in {
        'CMakeLists.txt', 'gamecontrollerdb.txt', 'music_sequences.txt', 'linux-host-libraries.txt'}


def historical(name):
    return (name.startswith('patches/') and not name.startswith('patches/release-prep/v1.0.9/')) or bool(
        re.search(r'/(?:history/|PUBLIC-|BETA\d|PLAYTEST\d|PR\d)', name)) or bool(
        re.search(r'-20\d{6}\.', name)) or Path(name).name in {
        'RELATORIO_IA_BOTS.md', 'CHANGELOG.md', 'HISTORY.md', 'MODDING_ARCHITECTURE.md',
        'BLENDER_ADDON_PLAN.md', 'CUSTOM_MUSIC_PLAN.md', 'CUSTOM_MINIMAP_PLAN.md',
        'LEGACY-ACTIVATION-WORK.md', 'LEGACY-CUSTOM-CHARACTERS-PLAN.md', 'LEGACY-MOD-COMPATIBILITY-BETA7.md',
        'LEGACY-MOD-IMPORT-PREVIEW.md', 'LEGACY-NATIVE-CHARACTER-STAGE-PLAN.md', 'LEGACY-NATIVE-STAGE-IMPLEMENTATION.md',
        'OPTIONAL-MIPMAP-PLAN.md', 'ONLINE-STABILITY.md', 'LAUNCHER-PERFORMANCE.md', 'LIVE-TEXTURE-LOD.md',
        'MAGIC-CODES.md', 'SPLIT-SCREEN-VALIDATION.md', 'TRACK-PERFORMANCE.md', 'FINISH-PRESENTATION-VALIDATION.md',
        'GENERATED-MIPMAP-VALIDATION.md', 'HUD-REFERENCE-ALIGNMENT.md', 'CPU-POWER-OPTIMISATION.md',
        'ENHANCED-WATER-BETA15.txt', 'EXPERIMENTAL-ROLLBACK-TESTING.txt', 'BETA12-OPTIMISATION.md',
        'BETA10-REGRESSION-VALIDATION.md', 'ARM-PLATFORM-PREVIEW.md', 'ARM-PLATFORM-VALIDATION.md', 'ANDROID-MOBILE-UI.md'}


def disposition(name, changed):
    if name in changed:
        return 'updated-current-guide'
    if 'source-capsule' in name:
        return 'frozen-source-input-not-documentation'
    if re.search(r'(?i)(?:LICENSE|COPYING|NOTICE|THIRD_PARTY)', name):
        return 'licence-and-attribution-preserved'
    if name.startswith('patches/release-prep/v1.0.9/documentation/'):
        return 'documentation-pipeline-input'
    if historical(name):
        return 'historical-evidence-preserved'
    return 'current-reference-reviewed-no-version-change-needed'


def overlay(evidence):
    spec = importlib.util.spec_from_file_location('dkrr109_docs_content', AUTH / 'content.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    with zipfile.ZipFile(evidence / 'before-docs.zip') as baseline:
        return module.build(ROOT, baseline)


def snapshot(evidence):
    evidence.mkdir(parents=True, exist_ok=True)
    target = evidence / 'BEFORE.json'
    if target.exists():
        raise ValueError('Use a fresh evidence directory; existing backups are never replaced.')
    rows = []
    with zipfile.ZipFile(evidence / 'before-docs.zip', 'x', compression=zipfile.ZIP_DEFLATED) as archive:
        for name in files():
            data = (ROOT / name).read_bytes()
            rows.append({'path': name, 'sha256': sha(data), 'bytes': len(data)})
            archive.writestr(name, data)
    target.write_text(json.dumps(rows, indent=2) + '\n', encoding='utf-8')
    (evidence / 'SUBMODULES.txt').write_bytes(subprocess.check_output(['git', 'submodule', 'status', '--recursive'], cwd=ROOT))
    (evidence / 'GIT-STATUS.txt').write_bytes(subprocess.check_output(['git', 'status', '--short'], cwd=ROOT))
    print('Recoverable documentation baseline:', len(rows), 'first-party files')


def apply(evidence):
    desired = overlay(evidence)
    rows = json.loads((evidence / 'BEFORE.json').read_text(encoding='utf-8'))
    baseline = {r['path']: r['sha256'] for r in rows}
    previous_file = evidence / 'CHANGES.json'
    previous = json.loads(previous_file.read_text(encoding='utf-8')) if previous_file.exists() else {}
    # Preflight every target before changing any file.
    for name, text in desired.items():
        if name.startswith(PROTECTED) or Path(name).suffix.lower() not in {'.md', '.txt'}:
            raise ValueError('Overlay target is outside the documentation boundary: ' + name)
        path = ROOT / name
        if name in previous:
            if sha(path.read_bytes()) != previous[name]['after']:
                raise ValueError('Document changed outside the pipeline: ' + name)
        elif name in baseline:
            if sha(path.read_bytes()) != baseline[name]:
                raise ValueError('Document changed since snapshot: ' + name)
        elif path.exists():
            raise ValueError('New documentation target already exists: ' + name)
    changes = {}
    for name, text in desired.items():
        data = text.replace('\r\n', '\n').encode('utf-8')
        if name in baseline and baseline[name] == sha(data):
            continue
        path = ROOT / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        changes[name] = {'before': baseline.get(name), 'after': sha(data)}
    (evidence / 'CHANGES.json').write_text(json.dumps(changes, indent=2) + '\n', encoding='utf-8')
    verify(evidence)


def verify(evidence):
    changes = json.loads((evidence / 'CHANGES.json').read_text(encoding='utf-8'))
    before = json.loads((evidence / 'BEFORE.json').read_text(encoding='utf-8'))
    for row in before:
        name = row['path']
        if name.startswith('patches/release-prep/v1.0.9/documentation/'):
            continue  # This is the declared authoring boundary, not native build input.
        expected = changes[name]['after'] if name in changes else row['sha256']
        if sha((ROOT / name).read_bytes()) != expected:
            raise ValueError('Unexpected source/documentation change: ' + name)
    if subprocess.check_output(['git', 'submodule', 'status', '--recursive'], cwd=ROOT) != (evidence / 'SUBMODULES.txt').read_bytes():
        raise ValueError('Protected submodule revisions changed')
    for name, row in changes.items():
        if sha((ROOT / name).read_bytes()) != row['after']:
            raise ValueError('Applied document differs: ' + name)
    inventory = []
    for name in files():
        if not document(name):
            continue
        text = (ROOT / name).read_text(encoding='utf-8', errors='replace')
        inventory.append({'path': name, 'disposition': disposition(name, changes),
                          'sha256': sha((ROOT / name).read_bytes()),
                          'version_mentions': sorted(set(re.findall(r'\b1\.0\.\d+(?:[+-][\w.-]+)?', text)))})
    links = []
    for name in changes:
        if not name.endswith('.md') or '/history/' in name:
            continue
        text = (ROOT / name).read_text(encoding='utf-8')
        for match in re.finditer(r'(?<!!)\[[^\]\n]+\]\(([^)\s]+)(?:\s+"[^"]*")?\)', text):
            link = match.group(1)
            if re.match(r'^[a-zA-Z][a-zA-Z0-9+.-]*:', link) or link.startswith('#'):
                continue
            destination = link.split('#')[0]
            if not destination:
                continue
            target = ((ROOT / name).parent / destination).resolve()
            if not target.is_relative_to(ROOT) or not target.exists():
                links.append({'document': name, 'missing': link})
    if links:
        raise ValueError('Broken local documentation links: ' + json.dumps(links))
    # Current Online contract must not recommend the removed backend selector.
    for name in ('README.md', 'docs/ONLINE_MULTIPLAYER.md', 'docs/TROUBLESHOOTING.md', 'docs/MAGIC_CODES.md'):
        text = (ROOT / name).read_text(encoding='utf-8')
        if re.search(r'Auto selects Experimental|require Auto / Experimental|no .*spectator mode|no .*public matchmaking', text, re.I):
            raise ValueError('Stale Online contract: ' + name)
    (evidence / 'DOCUMENTATION-INVENTORY.json').write_text(json.dumps(inventory, indent=2) + '\n', encoding='utf-8')
    counts = {}
    for row in inventory:
        counts[row['disposition']] = counts.get(row['disposition'], 0) + 1
    report = {'schema': 1, 'version': '1.0.9', 'changed_documents': len(changes), 'inventory': counts,
              'unchanged_existing_first_party_files': len(before) - len([n for n in changes if changes[n]['before']]),
              'local_markdown_links': 'passed', 'protected_submodule_revisions': 'unchanged',
              'native_execution': False}
    (evidence / 'VERIFICATION.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=('snapshot', 'apply', 'verify'))
    parser.add_argument('--evidence', type=Path, required=True)
    args = parser.parse_args()
    if args.evidence.resolve().is_relative_to(ROOT):
        parser.error('Keep recoverable evidence outside the repository')
    globals()[args.action](args.evidence.resolve())
