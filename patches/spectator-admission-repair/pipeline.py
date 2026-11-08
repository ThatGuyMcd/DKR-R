"""Compose a narrow spectator repair above unchanged playtest.3 sources."""
from pathlib import Path
import argparse, hashlib, json, re, sys
sys.dont_write_bytecode = True
import transforms
AUTH = Path(__file__).resolve().parent
ROOT = AUTH.parents[1]

def sha(data): return hashlib.sha256(data).hexdigest()
def write(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    if not path.is_file() or path.read_bytes() != data: path.write_bytes(data)

def compose(rows, output):
    expected = {('DKRLegacyModLaunch', 'resident_music.cpp'), ('DKRPortGame', 'party_runtime.cpp'),
        ('DKRExperimentalOwnedGameV77', 'probe_driver.cpp'), ('DKRExperimentalOwnedGameV80', 'probe_driver.cpp')}
    if len(rows) != 4 or {(r['target'], Path(r['path']).name) for r in rows} != expected:
        raise ValueError('Expected the final resident music, party handler and two owned driver sources')
    outputs = {p.name: p.read_bytes() for p in sorted((AUTH/'native').iterdir()) if p.is_file()}
    resident = next(Path(r['path']) for r in rows if Path(r['path']).name == 'resident_music.cpp')
    match = re.search(r'^#include "([^"\n]*resident_music[.]hpp)"', resident.read_text(), re.M)
    if not match: raise ValueError('Final resident music header missing')
    header = Path(match[1]); header = header if header.is_absolute() else resident.parent/header
    if not header.is_file(): raise ValueError('Final resident music header cannot be resolved')
    swaps = []
    for row in rows:
        source = Path(row['path'])
        if any(p.lower() in ('extern', 'recompiledfuncs', 'recompiledpatches') for p in source.parts):
            raise ValueError('Protected source cannot be an admission input')
        raw = source.read_bytes(); text = raw.decode().replace('\r\n', '\n')
        transform = {'resident_music.cpp': transforms.resident, 'party_runtime.cpp': transforms.party,
                     'probe_driver.cpp': transforms.driver}[source.name]
        text = transform(text)
        def include(m):
            name = Path(m[1])
            if name.name in ('observer_music.hpp', 'admission_retry.hpp'):
                return '#include "'+(output/name.name).as_posix()+'"'
            if name.name == 'resident_music.hpp':
                return '#include "'+header.resolve().as_posix()+'"'
            path = name if name.is_absolute() else source.parent/name
            return '#include "'+path.resolve().as_posix()+'"' if path.is_file() else m[0]
        text = re.sub(r'^#include "([^"\n]+)"', include, text, flags=re.M)
        name = row['target']+'/'+source.name
        outputs[name] = text.encode()
        swaps.append({**row, 'new': str(output/name), 'input_sha256': sha(raw), 'output_sha256': sha(text.encode())})
    # The scope delegates the exact retained PreparationScope ABI. Resolve its
    # header explicitly; no altered class definition or checkpoint layout.
    outputs['observer_music.hpp'] = outputs['observer_music.hpp'].replace(b'#include "resident_music.hpp"',
        ('#include "'+header.resolve().as_posix()+'"').encode(), 1)
    fixture=(ROOT/'patches/race-release-repair/tests/runtime_tests.cpp').read_text()
    if fixture.count('\nint main() {')!=1:raise ValueError('Retained handler fixture changed')
    outputs['retained_fixture.inl']=fixture.split('\nint main() {',1)[0].encode()
    return swaps, outputs

if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--inputs', type=Path, required=True); p.add_argument('--output', type=Path, required=True)
    a = p.parse_args(); output = a.output.resolve()
    if output.is_relative_to(ROOT): raise ValueError('Generated sources must stay outside the checkout')
    rows, outputs = compose(json.loads(a.inputs.read_text()), output)
    for name, data in outputs.items(): write(output/name, data)
    write(output/'swaps.json', (json.dumps(rows, indent=2)+'\n').encode())
    print('Verified spectator music preparation and bounded recovery staged above playtest.3.')
