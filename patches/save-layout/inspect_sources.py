"""Read-only source/artifact inspection. Does not run game code or tests."""
import argparse
import hashlib
import json
import re
from pathlib import Path

definition = re.compile(r'(?:RECOMP_FUNC\s+void|static\s+void)\s+(\w+)\s*\([^{};]*\)\s*\{')
tokens = re.compile(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|//[^\n]*|/\*[\s\S]*?\*/|[{}]')
operations = {'populate_settings_from_save_data': 0, 'func_800732E8': 1,
              'func_80073588': 2, 'func_800738A4': 3, 'clear_lap_records': 4}
sha = lambda data: hashlib.sha256(data).hexdigest()


def functions(folder):
    result = {}
    for file in sorted(folder.glob('*.c')):
        text = file.read_text()
        matches = list(definition.finditer(text))
        for match in matches:
            depth = 1
            end = None
            for token in tokens.finditer(text, match.end()):
                if token[0] == '{':
                    depth += 1
                elif token[0] == '}':
                    depth -= 1
                if not depth:
                    end = token.end()
                    break
            if end is None:
                raise ValueError('Unterminated generated function: '+match[1])
            body = text[match.start():end]
            # File-level whitespace changes are irrelevant to a function body.
            result[match[1]] = body.rstrip()
    return result


def inspect(baseline, output):
    report = {'inspection': 'Read-only source comparison; no game or runtime tests', 'revisions': {}}
    for revision in (77, 80):
        before = baseline / f'generated-v{revision}'
        after = output / f'generated-v{revision}'
        old = functions(before)
        new = functions(after)
        changed = {name for name in old if old[name] != new[name]}
        if changed != set(operations):
            raise ValueError(f'Unexpected original function changes: {changed}')
        for name, op in operations.items():
            entry = f'\n    dkr_save_layout_{op}(rdram, ctx);\n    return;\n'
            if new[name].replace(entry, '', 1) != old[name]:
                raise ValueError('Original body was not retained verbatim: '+name)
        if set(new)-set(old) != {f'dkr_save_layout_{i}' for i in range(5)}:
            raise ValueError('Unexpected new guest functions')
        for file in before.iterdir():
            if file.suffix == '.c' or file.name == 'funcs.h':
                continue
            if file.read_bytes() != (after/file.name).read_bytes():
                raise ValueError('Unrelated baseline input changed: '+file.name)
        if not (after/'funcs.h').read_text().startswith((before/'funcs.h').read_text()):
            raise ValueError('Original declarations changed')
        receipt = json.loads((after/'save-layout-receipt.json').read_text())
        header = (after/'save_layout_guest.h').read_text()
        if receipt['helper_sha256'] != sha(header.encode()):
            raise ValueError('Normal helper receipt mismatch')
        owned = output/f'owned-v{revision}'
        manifest = json.loads((owned/'manifest.json').read_text())
        if manifest['save_layout_schema'] != 1 or manifest['save_layout_helper_sha256'] != receipt['helper_sha256']:
            raise ValueError('Rollback helper contract mismatch')
        if (owned/'save_layout_guest.h').read_text() != header:
            raise ValueError('Normal and rollback helpers differ')
        original_owned = functions(baseline/f'owned-v{revision}')
        fixed_owned = functions(owned)
        intended = set(operations) | {'dkr_code_body_'+name for name in operations}
        owned_changes = {name for name in original_owned if original_owned[name] != fixed_owned.get(name)}
        if owned_changes-intended:
            raise ValueError(f'Unrelated rollback guest function changes: {owned_changes}')
        if not set(operations).issubset(manifest['code_protected_policy']):
            raise ValueError('Save routines are not protected from code-mod replacement')
        if 'save-layout-invalid-guest-arrays-or-buffer' not in (owned/'blocked_imports.c').read_text():
            raise ValueError('Missing rollback failure fence')
        report['revisions'][str(revision)] = {
            'original_functions_preserved': len(old)-len(changed),
            'rollback_functions_preserved': len(original_owned)-len(owned_changes),
            'save_functions_redirected': sorted(changed),
            'normal_and_rollback_helper_sha256': receipt['helper_sha256']}
    return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('baseline', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    print(json.dumps(inspect(args.baseline, args.output), indent=2))
