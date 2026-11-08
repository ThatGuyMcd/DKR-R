"""Add border-free credits over the owner-accepted title/water build."""
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
        raise ValueError('Credits anchor changed: ' + old[:100])
    return text.replace(old, new)


def stage(base, out):
    if out.exists() or not out.resolve().is_relative_to(ROOT / 'build'):
        raise ValueError('Choose a NEW stage inside build/')
    accepted = json.loads((base / 'title-text-receipt.json').read_text())
    for name, expected in accepted['files'].items():
        if sha(base / name) != expected:
            raise ValueError('Accepted title/water input changed: ' + name)
    inputs = {}
    def read(path):
        inputs[str(path)] = sha(path)
        return path.read_text()
    native = out / 'native'
    native.mkdir(parents=True)
    def put(name, text):
        (native / name).write_text(text, encoding='utf-8', newline='\n')
    for path in sorted((base / 'native').glob('*.hpp')):
        put(path.name, read(path))
    addresses = []
    void_symbols = []
    for revision in (77, 80):
        text = read(ROOT / f'extern/dkr-decomp/ver/symbols/symbol_addrs.us.v{revision}.txt')
        found = re.findall(r'^gCreditsMenuElements = (0x[0-9A-Fa-f]+);', text, re.M)
        if len(found) != 1:
            raise ValueError('Ambiguous credit rows symbol')
        addresses.append(found[0] + 'U')
        values = []
        for name in ('gVoidData', 'gVoidVerts', 'gVoidCurrVerts', 'gVoidVertexFlip', 'gVoidVertCount'):
            found = re.findall(r'^' + name + r' = (0x[0-9A-Fa-f]+);', text, re.M)
            if len(found) != 1:
                raise ValueError('Ambiguous credits backdrop symbol: ' + name)
            values.append(found[0] + 'U')
        void_symbols.append('    {' + ','.join(values) + '}')
    put('credits_sky_rt64.hpp', read(AUTH / 'credits_sky_rt64.hpp').replace('@CREDITS_VOID_SYMBOLS@', ',\n'.join(void_symbols)))

    # Reuse the accepted viewport algorithm in a separately admitted credits
    # instance. The original logo helper remains byte-for-byte unchanged.
    text = read(base / 'native/intro_viewport_rt64.hpp')
    text = text.replace('class IntroViewport', 'class CreditsViewport')
    text = text.replace('MENU_LOGOS', 'MENU_CREDITS').replace('[intro][viewport]', '[credits][viewport]')
    text = text.replace('full-frame picture; copyright text retained; logo proportions preserved',
        'full-frame cinematic; borders removed; original scene proportions')
    text = once(text, '    bool enabled_=false;', '    bool enabled_=false;\n    float cover_=1.F;')
    text = once(text, '    void begin(bool enabled) {enabled_=enabled;picture_=false;top_=bottom_=0;height_=240;}', '''    void begin(bool enabled,float aspect) {
        enabled_=enabled;picture_=false;top_=bottom_=0;height_=240;
        cover_=std::clamp(std::isfinite(aspect)?aspect/(4.F/3.F):1.F,1.F,8.F);
    }''')
    text = once(text, '#include <cmath>', '#include <cmath>\n#include <algorithm>')
    text = once(text, '        if(!picture_)return;', '''        if(!picture_) {
            // Ordinary 2D/transition resets use the inclusive 319/239 bounds.
            // Keep their native aspect reference complete, without widening
            // arbitrary partial UI scissors or changing their projection.
            if(r.ulx==0 && r.uly==0 && (r.lrx==319*4 || r.lrx==320*4) &&
                (r.lry==239*4 || r.lry==240*4 || r.lry==287*4 || r.lry==288*4)) {
                r.lrx=320*4;r.lry=r.lry>240*4?288*4:240*4;
                state.updateDrawStatusAttribute(RT64::DrawAttribute::Scissor);
            }
            return;
        }''')
    start = text.index('    bool fill(')
    end = text.index('    template<class Pair>', start)
    text = text[:start] + read(AUTH / 'credit_fade.inl') + text[end:]
    put('credits_viewport_rt64.hpp', text)

    text = read(base / 'native/title_text_rt64.hpp')
    text = once(text, '    static constexpr unsigned capacity=32;', '    static constexpr unsigned capacity=96;')
    text = once(text, '    unsigned image_count_=0;', '''    unsigned image_count_=0;
    bool credits_=false,quiet_=false;
    std::uint32_t font_base_=0,credits_elements_=0;
    static constexpr std::array<std::uint32_t,2> credit_rows_{{''' + ','.join(addresses) + '''}};''')
    text = once(text, '    static void skipped(unsigned reason,unsigned detail=0) {', '''    void skipped(unsigned reason,unsigned detail=0) const {
        if(quiet_)return;''')
    text = once(text, '    bool colour(std::uint32_t env,std::uint32_t prim) const {', '''    bool colour(std::uint32_t env,std::uint32_t prim) const {
        if(credits_)return (prim&0xFFFFFF00U)==0xFFFFFF00U; // Exact row colours checked after parsing.''')
    text = once(text, 'public:\n    void begin', read(AUTH / 'credit_match.inl') + '\npublic:\n    void begin')
    text = once(text, '        count_=image_count_=0;enabled_=false;ram_=state.RDRAM;',
        '        count_=image_count_=0;enabled_=false;credits_=quiet_=false;ram_=state.RDRAM;')
    text = once(text, '        if(u32(revision_addresses::CurrentMenuId)!=0 || u32(revision_addresses::TitleRevealTimer)!=0)return;', '''        const auto menu=u32(revision_addresses::CurrentMenuId);
        credits_=menu==25;
        if(!credits_ && (menu!=0 || u32(revision_addresses::TitleRevealTimer)!=0))return;''')
    text = once(text, '        const auto fonts=u32(symbols_->fonts);', '''        credits_elements_=credit_rows_[v80?1:0];
        const auto fonts=u32(symbols_->fonts);font_base_=fonts;''')
    text = once(text, '    void prepare(RT64::State& state,const RT64::DisplayList* start) {', '''    void prepare(RT64::State& state,const RT64::DisplayList* start) {
        if(!credits_){prepare_one(state,start);return;}
        if(!enabled_ || start->p0(16,1) ||
            (state.rsp->fromSegmented(start->w1)&0x1FFFFFFFU)!=(symbols_->begin&0x1FFFFFFFU))return;
        for(const unsigned font:std::array<unsigned,2>{2,0}) {
            font_=font_base_+font*0x400;quiet_=true;
            prepare_one(state,start);quiet_=false;
            if(count_)return;
        }
        skipped(8);
    }
    void prepare_one(RT64::State& state,const RT64::DisplayList* start) {''')
    text = once(text, '        for(unsigned i=1;i<256;++i) {', '        for(unsigned i=1;i<1024;++i) {')
    old = '''        for(unsigned name=0;name<10;++name) {
            glyphs_=raw;count_=n;
            if(match_name(u32(symbols_->names+name*0x2C)))return;
        }'''
    text = once(text, old, '''        if(credits_) {
            for(unsigned row=0;row<9;++row) {
                const auto element=credits_elements_+row*0x20;
                if(!u32(element+0x14))break;
                glyphs_=raw;count_=n;
                if(match_credit(element,env,prim))return;
            }
        } else {
''' + old + '''
        }''')
    text = once(text, '            std::fprintf(stderr,"[title][names] full-width travel; unclipped signed glyphs; original proportions and colour trails\\n");', '''            if(credits_)std::fprintf(stderr,"[credits][text] full-width slides; original font proportions; signed glyphs and spaces\\n");
            else std::fprintf(stderr,"[title][names] full-width travel; unclipped signed glyphs; original proportions and colour trails\\n");''')
    # Admission notices are separate; showing the title cannot suppress the
    # later credits notice in the same thread/session.
    text = once(text, '        static thread_local bool notice=false;\n        if(!notice) {', '''        static thread_local std::array<bool,2> notices{};
        auto& notice=notices[credits_?1:0];
        if(!notice) {''')
    put('title_text_rt64.hpp', text)

    text = read(base / 'native/f3ddkr_rt64.cpp')
    text = once(text, '#include "intro_viewport_rt64.hpp"',
        '#include "intro_viewport_rt64.hpp"\n#include "credits_viewport_rt64.hpp"\n#include "credits_sky_rt64.hpp"')
    text = once(text, '    presentation::IntroViewport intro_viewport;',
        '    presentation::IntroViewport intro_viewport;\n    presentation::CreditsViewport credits_viewport;\n    presentation::CreditsSky credits_sky;')
    text = once(text, '    data_->intro_viewport.begin(', '''    data_->credits_viewport.begin(enhancements::modern_presentation_enabled() &&
        enhancements::fit_to_window_enabled() && ReadU32(state->RDRAM,kCurrentMenuIdAddress)==25U,data_->world_window_aspect);
    data_->credits_sky.begin(*state,data_->world_window_aspect,data_->credits_viewport.enabled());
    data_->intro_viewport.begin(''')
    text = once(text, '    if(opcode==kFullSyncOpcode && active_->data_->intro_viewport.enabled()) {', '''    if(opcode==kFullSyncOpcode && active_->data_->credits_viewport.enabled()) {
        state->flush();
        auto& workload=state->ext.workloadQueue->workloads[state->ext.workloadQueue->writeCursor];
        for(unsigned i=0;i<workload.fbPairCount;++i)
            active_->data_->credits_viewport.normalise(workload.fbPairs[i]);
    }
    if(opcode==kFullSyncOpcode && active_->data_->intro_viewport.enabled()) {''')
    text = once(text, '    AdjustSplitViewportCommand(state, command, opcode);', '''    if(opcode==kSetScissorOpcode)data.credits_viewport.scissor(*state);
    else if(opcode==kMoveMemOpcode && command->p0(16,8)==kViewportMoveMemType)
        data.credits_viewport.viewport(*state);
    AdjustSplitViewportCommand(state, command, opcode);''')
    text = once(text, '    if(data.intro_viewport.fill(*state,**display_list))return;',
        '    if(data.intro_viewport.fill(*state,**display_list))return;\n    if(data.credits_viewport.fill(*state,**display_list))return;')
    text = once(text, '        vertex.color.a = ReadU8(state->RDRAM, address + 9U);', '''        vertex.color.a = ReadU8(state->RDRAM, address + 9U);
        if(!canonical_shadow)data.credits_sky.vertex(address,vertex);''')
    put('f3ddkr_rt64.cpp', text)
    receipt = {'schema': 1, 'version': '1.0.7+beta.10', 'inputs': inputs,
        'accepted_title_text': accepted,
        'files': {p.relative_to(out).as_posix(): sha(p) for p in out.rglob('*') if p.is_file()},
        'policy': 'Patch Pipeline only. Immutable-task presentation; no guest, timing, save or dependency changes. Owner performs gameplay testing.'}
    (out / 'credits-viewport-receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')


def verify(out):
    receipt = json.loads((out / 'credits-viewport-receipt.json').read_text())
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
                raise ValueError('Credits receipt changed: ' + str(path))
    print('Credits viewport/text composition inspected; no target or tests executed.')


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
