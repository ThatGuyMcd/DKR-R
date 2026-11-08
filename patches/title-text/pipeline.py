"""Compose full-width title names over the accepted water/intro build."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import re
import sys
sys.dont_write_bytecode = True
AUTH = Path(__file__).resolve().parent
ROOT = AUTH.parents[1]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def once(text, old, new):
    if text.count(old) != 1:
        raise ValueError('Title text anchor changed: ' + old[:120])
    return text.replace(old, new)


def verify(out):
    receipt = json.loads((out / 'title-text-receipt.json').read_text())
    for group in ('inputs', 'files'):
        for name, expected in receipt[group].items():
            if group == 'files':
                path = out / name
            else:
                name = name.replace('\\', '/')
                if os.name != 'nt' and re.match(r'^[A-Za-z]:/', name):
                    name = '/mnt/' + name[0].lower() + name[2:]
                path = Path(name)
            if sha(path) != expected:
                raise ValueError('Title text receipt changed: ' + str(path))
    print('Title text composition inspected; no application or tests executed.')


def stage(base, out):
    if out.exists() or not out.resolve().is_relative_to(ROOT / 'build'):
        raise ValueError('Choose a NEW stage inside build/')
    accepted = json.loads((base / 'viewport-flow-receipt.json').read_text())
    for name, expected in accepted['files'].items():
        if sha(base / name) != expected:
            raise ValueError('Accepted water/intro file changed: ' + name)
    inputs = {}
    def read(path):
        inputs[str(path)] = sha(path)
        return path.read_text()
    native = out / 'native'
    native.mkdir(parents=True)
    # Keep the accepted decoder's quote-includes adjacent. Falling back to an
    # older SDK include directory would mix two water registry definitions.
    for path in sorted((base / 'native').glob('*.hpp')):
        (native / path.name).write_text(read(path), encoding='utf-8', newline='\n')
    # DKR's original GBI numbering differs from the more familiar F3DEX
    # numbering. Derive the two continuation tags from its actual definitions.
    gbi = read(ROOT / 'extern/dkr-decomp/include/PR/gbi.h')
    immediate = re.findall(r'^#define\s+G_IMMFIRST\s+(-?\d+)\s*$', gbi, re.M)
    if len(immediate) != 1:
        raise ValueError('Ambiguous DKR immediate opcode base')
    half_tags = {}
    for name in ('G_RDPHALF_1', 'G_RDPHALF_2'):
        offsets = re.findall(r'^#define\s+' + name + r'\s+\(G_IMMFIRST-(\d+)\)\s*$', gbi, re.M)
        if len(offsets) != 1:
            raise ValueError('Ambiguous DKR continuation opcode: ' + name)
        half_tags[name] = ((int(immediate[0]) - int(offsets[0])) & 255) << 24
    # Read symbol maps, never modify or compile the decomp/dependencies.
    fields = ('dDialogueBoxBegin', 'dDialogueBoxDrawModes', 'gFonts',
              'gNumberOfFonts', 'gCompactKerning', 'gTitleCinematicText',
              'gTitleCinematicTextColours')
    tables = []
    for revision in (77, 80):
        symbols = read(ROOT / f'extern/dkr-decomp/ver/symbols/symbol_addrs.us.v{revision}.txt')
        values = []
        for field in fields:
            matches = re.findall(r'^' + re.escape(field) + r' = (0x[0-9A-Fa-f]+);$', symbols, re.M)
            if len(matches) != 1:
                raise ValueError('Ambiguous title symbol: ' + field)
            values.append(matches[0] + 'U')
        tables.append('    {' + ','.join(values) + '}')
    header = read(AUTH / 'title_text_rt64.hpp').replace('@TITLE_SYMBOL_TABLES@', ',\n'.join(tables))
    header = header.replace('@TITLE_RDPHALF1@', f'0x{half_tags["G_RDPHALF_1"]:08X}U')
    header = header.replace('@TITLE_RDPHALF2@', f'0x{half_tags["G_RDPHALF_2"]:08X}U')
    (native / 'title_text_rt64.hpp').write_text(header, encoding='utf-8', newline='\n')
    text = read(base / 'native/f3ddkr_rt64.cpp')
    text = once(text, '#include "intro_viewport_rt64.hpp"',
        '#include "intro_viewport_rt64.hpp"\n#include "title_text_rt64.hpp"')
    text = once(text, '    presentation::IntroViewport intro_viewport;',
        '    presentation::IntroViewport intro_viewport;\n    presentation::TitleTextViewport title_text;')
    text = once(text, '    data_->intro_viewport.begin(', '''    data_->title_text.begin(*state,data_->world_window_aspect,
        enhancements::modern_presentation_enabled() && enhancements::fit_to_window_enabled());
    data_->intro_viewport.begin(''')
    text = once(text, '    if(opcode==kFullSyncOpcode && active_->data_->intro_viewport.enabled()) {', '''    if(opcode==kFullSyncOpcode && active_->data_->title_text.has_images()) {
        state->flush();
        auto& workload=state->ext.workloadQueue->workloads[state->ext.workloadQueue->writeCursor];
        for(unsigned i=0;i<workload.fbPairCount;++i)
            active_->data_->title_text.normalise(workload.fbPairs[i]);
    }
    if(opcode==kDisplayListOpcode)active_->data_->title_text.prepare(*state,command);
    if(opcode==kFullSyncOpcode && active_->data_->intro_viewport.enabled()) {''')
    text = once(text, '    const bool flip = ((*display_list)->w0 >> 24U) == G_TEXRECTFLIP;', '''    if(active_->data_->title_text.draw(*state,display_list))return;
    const bool flip = ((*display_list)->w0 >> 24U) == G_TEXRECTFLIP;''')
    (native / 'f3ddkr_rt64.cpp').write_text(text, encoding='utf-8', newline='\n')
    receipt = {'schema': 1, 'version': '1.0.7+beta.10', 'inputs': inputs, 'continuation_tags': half_tags,
        'accepted_viewport_flow': accepted,
        'files': {p.relative_to(out).as_posix(): sha(p) for p in out.rglob('*') if p.is_file()},
        'policy': 'First-party immutable-task presentation only. No guest/dependency changes. Owner performs gameplay testing.'}
    (out / 'title-text-receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=('stage', 'verify'))
    parser.add_argument('--base', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if args.action == 'stage':
        stage(args.base.resolve(strict=True), args.output)
    verify(args.output)


if __name__ == '__main__':
    main()
