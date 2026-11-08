"""Silence only confirmed inline track previews in isolated source copies."""
from pathlib import Path
import argparse
import hashlib
import json
import re
import sys

sys.dont_write_bytecode = True
AUTH = Path(__file__).resolve().parent
ROOT = AUTH.parents[1]
INC = re.compile(r'^#include "([^"\n]+)"', re.M)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def once(text, old, new):
    if text.count(old) != 1:
        raise ValueError('Music preview input changed: ' + old[:140])
    return text.replace(old, new, 1)


def native(text):
    return once(text,
        '    dkr_probe_mod_dispatch("dkr_owned_course_prepare",rdram,ctx,0,0,0,0,&ignored);',
        '''    // Inline Track Select previews are confirmed but silent, just like
    // deferred menu backgrounds. This does not authorise an audio decoder.
    const uint64_t silent_music_preview=inline_track_preview(rdram) && confirmed_menu_tick;
    dkr_probe_mod_dispatch("dkr_owned_course_prepare",rdram,ctx,&silent_music_preview,1,0,0,&ignored);''')


def runtime(text):
    return once(text,
        '''        else if(name=="dkr_owned_course_prepare") {
            arity(0);frozen_course_hook("prepare_memory",ram,ctx);
        }''',
        '''        else if(name=="dkr_owned_course_prepare") {
            arity(1);
            if(args[0]>1)throw mods::Error("Invalid inline music preview policy.");
            mods::online::resident_music::PreviewScope preview(args[0]!=0);
            frozen_course_hook("prepare_memory",ram,ctx);
        }''')


def header(text):
    anchor = '// Delegates the existing integer mixer/checkpoint ABI'
    return once(text, anchor,
        '''// A preview can suppress music without granting permission to decode.
// Kept local to the course-prepare service so a failed guest continuation
// cannot leave thread-local suppression active after its native unwind.
class PreviewScope final {
public:
    explicit PreviewScope(bool enabled) noexcept;
    ~PreviewScope();
    PreviewScope(const PreviewScope&)=delete;
    PreviewScope& operator=(const PreviewScope&)=delete;
private:
    bool enabled_;
};

''' + anchor)


def resident(text):
    anchor = 'PreparationScope::PreparationScope(bool enabled,bool audible)noexcept:'
    return once(text, anchor,
        '''PreviewScope::PreviewScope(bool enabled)noexcept:enabled_(enabled){if(enabled_)++silent_preparation_depth;}
PreviewScope::~PreviewScope(){if(enabled_)--silent_preparation_depth;}

''' + anchor)


def compose(rows, output):
    expected = {('DKRPortGame', 'runtime_legacy_mods.cpp'),
                ('DKRLegacyModLaunch', 'resident_music.cpp'),
                ('DKRExperimentalOwnedGameV77', 'probe_native.c'),
                ('DKRExperimentalOwnedGameV80', 'probe_native.c')}
    if len(rows) != 4 or {(r['target'], Path(r['path']).name) for r in rows} != expected:
        raise ValueError('Expected the runtime, music adapter and both native revisions')
    music = next(Path(r['path']) for r in rows if Path(r['path']).name == 'resident_music.cpp')
    original_header = music.parent / 'resident_music.hpp'
    header_raw = original_header.read_bytes()
    outputs = {'resident_music.hpp': header(header_raw.decode().replace('\r\n', '\n')).encode()}
    swaps = []
    for row in rows:
        old = Path(row['path'])
        if any(p.lower() in ('extern', 'recompiledfuncs', 'recompiledpatches') for p in old.parts):
            raise ValueError('Protected source is not a music preview input')
        raw = old.read_bytes()
        text = raw.decode().replace('\r\n', '\n')
        transform = native if old.name == 'probe_native.c' else resident if old.name == 'resident_music.cpp' else runtime
        text = transform(text)

        def include(match):
            name = Path(match[1])
            if name.name == 'resident_music.hpp':
                return '#include "' + (output / 'resident_music.hpp').as_posix() + '"'
            adjacent = name if name.is_absolute() else old.parent / name
            return '#include "' + adjacent.resolve().as_posix() + '"' if adjacent.is_file() else match[0]

        text = INC.sub(include, text)
        name = row['target'] + '/' + old.name
        outputs[name] = text.encode()
        swaps.append({**row, 'new': str(output / name), 'input_sha256': sha(raw),
                      'output_sha256': sha(outputs[name])})
    includes = [{'path': str(original_header), 'new': str(output / 'resident_music.hpp'),
                 'input_sha256': sha(header_raw), 'output_sha256': sha(outputs['resident_music.hpp'])}]
    return swaps, includes, outputs


def main(args):
    output = args.output.resolve()
    if output.is_relative_to(ROOT):
        raise ValueError('Generated music preview output must stay outside the checkout')
    swaps, includes, outputs = compose(json.loads(args.inputs.read_text()), output)
    for name, data in outputs.items():
        path = output / name
        path.parent.mkdir(parents=True, exist_ok=True)
        if not path.exists() or path.read_bytes() != data:
            path.write_bytes(data)
    for name, rows in [('swaps.json', swaps), ('includes.json', includes)]:
        (output / name).write_text(json.dumps(rows, indent=2) + '\n', encoding='utf-8')
    print('Confirmed inline previews silenced; decoder/loading/rollback guards retained. No target execution.')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--inputs', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    main(parser.parse_args())
