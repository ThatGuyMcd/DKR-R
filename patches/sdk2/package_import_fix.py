"""Package the corrected Dev Menu/SDK, keeping all old deliveries intact."""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import shutil
import sys
from types import SimpleNamespace
import zipfile

sys.dont_write_bytecode = True
AUTH = Path(__file__).resolve().parent
sys.path.insert(0, str(AUTH/'sdk'))
from relocations import normalize, symbols
import distribution


def sha(data): return hashlib.sha256(data).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('stage','baseline','products','bible','output'):
        parser.add_argument('--'+name, type=Path, required=True)
    args = parser.parse_args()
    stage, baseline, products = args.stage.resolve(strict=True), args.baseline.resolve(strict=True), args.products.resolve(strict=True)
    output = args.output.resolve()
    if output.exists(): raise ValueError('Choose a NEW delivery directory')
    if (stage/'sdk2-engine-identity.txt').read_bytes() != (baseline/'sdk2-engine-identity.txt').read_bytes():
        raise ValueError('Native identity changed: cannot advertise a mod-only repair')
    headers = lambda root: {p.relative_to(root).as_posix():sha(p.read_bytes()) for p in root.rglob('*') if p.is_file()}
    if headers(stage/'sdk/include') != headers(baseline/'sdk/include'):
        raise ValueError('Public SDK header/ABI input changed')
    mod = products/'mod-compile/dkr_development_menu.nrm'
    report = json.loads((products/'mod-compile/relocation-report.json').read_text())
    # Re-derive addresses from the actual final files without modifying them.
    checked, second = normalize((products/'mod-compile/mod.elf').read_bytes(),
        (products/'mod-compile/mod_syms.bin').read_bytes(), (products/'mod-compile/mod_binary.bin').read_bytes())
    if second['changes'] or sha(checked) != report['symbols_sha256']:
        raise ValueError('Final symbol targets differ from the linked ELF')
    # tool-output.nrm is the pinned compiler's intermediate, not an importable
    # SDK package (it precedes metadata/resources and local-target correction).
    packages = [mod, *(p for p in sorted(products.glob('*-compile/*.nrm')) if p.name != 'tool-output.nrm')]
    inspected = []
    for package in dict.fromkeys(packages):
        with zipfile.ZipFile(package) as archive:
            if archive.testzip() is not None:  # ZIP CRC inspection only, not guest execution.
                raise ValueError('Bad archive CRC: '+str(package))
            data = archive.read('mod_syms.bin'); binary = archive.read('mod_binary.bin')
            sections, records = symbols(data, binary)
            manifest = json.loads(archive.read('mod.json'))
            if package == mod:
                if manifest['version'] != '2.0.1' or data != checked:
                    raise ValueError('Wrong Dev Menu edition/payload')
            inspected.append(dict(id=manifest['id'], version=manifest['version'], sections=len(sections), relocations=len(records), sha256=sha(package.read_bytes())))
    kit_output = products/'sdk-kit'
    examples = [products/'live-ai-compile/dkr_live_ai_boss_difficulty.nrm',
        products/'api1-trophy-compile/dkr_custom_trophies.nrm', products/'api1-difficulty-compile/dkr_ai_boss_difficulty.nrm']
    distribution.kit(SimpleNamespace(output=kit_output,stage=stage,bible=args.bible.resolve(strict=True),mod=mod,examples=examples))
    output.mkdir(parents=True)
    shutil.copy2(mod, output/'DKR-R-Dev-Menu-2.0.1.nrm')
    shutil.copy2(kit_output/'DKR-R-Modding-SDK-2.0-beta.7.zip', output/'DKR-R-Modding-SDK-2.0-beta.7-Import-Fix.zip')
    shutil.copy2(products/'mod-compile/relocation-report.json', output/'SDK2-RELOCATION-REPORT.json')
    shutil.copy2(AUTH/'DEV-MENU-IMPORT-FIX.txt', output/'README.txt')
    with zipfile.ZipFile(output/'DKR-R-Dev-Menu-2.0.1-Source.zip','x',zipfile.ZIP_DEFLATED,compresslevel=6) as archive:
        source=stage/'sdk/examples/dev-menu'
        for file in sorted(source.rglob('*')):
            if file.is_file() and file.suffix != '.pyc' and '__pycache__' not in file.parts:
                archive.write(file,'dkr-development-menu/'+file.relative_to(source).as_posix())
    findings = dict(schema=1, execution='Static inspection only. No game/mod/unit/replay/multiplayer/headset execution.',
        native_engine_identity=(stage/'sdk2-engine-identity.txt').read_text().strip(), app_rebuild_required=False,
        public_headers_unchanged=True, packages=inspected, dev_menu_corrections_by_type=dict(Counter(str(change['kind']) for change in report['changes'])),
        carry_checked_shared_lows=report['carry_checked_shared_lows'], elf_provenance_rechecked=True)
    (output/'STATIC-INSPECTION.json').write_text(json.dumps(findings,indent=2)+'\n', encoding='utf-8')
    (output/'SHA256SUMS.txt').write_text(''.join(sha(p.read_bytes())+'  '+p.name+'\n' for p in sorted(output.iterdir()) if p.is_file()),encoding='ascii')
    print('Corrected mod and SDK packaged without changing old builds: '+str(output))


if __name__ == '__main__': main()
