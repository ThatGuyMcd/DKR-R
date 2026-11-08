"""Read-only documentation/helper checks. No native executable is launched."""
from pathlib import Path
import argparse
import ast
import hashlib
import importlib.util
import json
import re
import tempfile
import zipfile

AUTH = Path(__file__).resolve().parent
ROOT = AUTH.parents[3]


def load(path):
    spec = importlib.util.spec_from_file_location('dkrr109_static_update_check', path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def anchors(text):
    values = set()
    seen = {}
    for heading in re.findall(r'^#{1,6}\s+(.+?)\s*#*\s*$', text, re.M):
        heading = re.sub('<[^>]*>', '', heading).lower()
        base = re.sub(r'[^\w -]', '', heading).replace(' ', '-')
        number = seen.get(base, 0)
        seen[base] = number + 1
        values.add(base + ('-' + str(number) if number else ''))
    return values


def main(args):
    changes = json.loads((args.evidence / 'CHANGES.json').read_text(encoding='utf-8'))
    link_count = 0
    for name in changes:
        if not name.endswith('.md') or '/history/' in name:
            continue
        for match in re.finditer(r'(?<!!)\[[^\]\n]+\]\(([^)\s]+)(?:\s+"[^"]*")?\)', (ROOT / name).read_text(encoding='utf-8')):
            link = match.group(1)
            if re.match(r'^[a-zA-Z][a-zA-Z0-9+.-]*:', link):
                continue
            link_count += 1
            file, _, fragment = link.partition('#')
            target = ((ROOT / name).parent / file).resolve() if file else ROOT / name
            if not target.is_file():
                if not target.is_dir():
                    raise ValueError('Missing link: ' + name + ': ' + link)
            elif fragment and target.suffix == '.md' and fragment not in anchors(target.read_text(encoding='utf-8')):
                raise ValueError('Missing Markdown anchor: ' + name + ': ' + link)
    for path in (ROOT / 'patches/release-prep/v1.0.9').rglob('*.py'):
        ast.parse(path.read_text(encoding='utf-8'), filename=str(path))
    with zipfile.ZipFile(args.evidence / 'before-docs.zip') as before:
        if before.read('RELEASE-VALIDATION.md') != (ROOT / 'docs/history/RELEASE-VALIDATION-1.0.8.md').read_bytes():
            raise ValueError('Archived validation is not byte-for-byte preserved')
    update = load(ROOT / 'patches/release-prep/v1.0.9/update_assets.py')
    old = update.verify_signature(args.old_manifest, args.old_signature)
    if old['version'] != '1.0.8' or old['tag'] != 'Version1.0.8':
        raise ValueError('Unexpected historical update fixture')
    rejected = 0
    with tempfile.TemporaryDirectory(prefix='dkrr109-static-tamper-') as work:
        work = Path(work)
        tampered = work / 'edited.json'
        value = dict(old, version='1.0.9')
        tampered.write_bytes(update.load_signer().canonical(value))
        try:
            update.verify_signature(tampered, args.old_signature)
        except ValueError:
            rejected += 1
        else:
            raise ValueError('An edited old manifest unexpectedly retained trust')
        invalid = work / 'zero.sig'
        invalid.write_text('00' * 64, encoding='ascii')
        try:
            update.verify_signature(args.old_manifest, invalid)
        except ValueError:
            rejected += 1
        else:
            raise ValueError('An invalid signature was accepted')
    report = {'version': '1.0.9', 'current_documents': len(changes), 'local_file_and_anchor_links': link_count,
              'python_helper_syntax': 'passed', 'old_signature_pinned_key': 'passed',
              'tampered_manifest_and_invalid_signature_rejected': rejected,
              'archived_validation': 'byte-identical', 'native_execution': False}
    (args.evidence / 'STATIC-CHECKS.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--evidence', type=Path, required=True)
    parser.add_argument('--old-manifest', type=Path, required=True)
    parser.add_argument('--old-signature', type=Path, required=True)
    main(parser.parse_args())
