"""Compose the final intro, fade and original-water repairs in a NEW stage."""
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
        raise ValueError('Presentation anchor changed: ' + old[:100])
    return text.replace(old, new)


def verify(out):
    doc = json.loads((out / 'presentation-polish-receipt.json').read_text())
    for group in ('inputs', 'files'):
        for name, expected in doc[group].items():
            if group == 'files':
                path = out / name
            else:
                name = name.replace('\\', '/')
                if os.name != 'nt' and re.match(r'^[A-Za-z]:/', name):
                    name = '/mnt/' + name[0].lower() + name[2:]
                path = Path(name)
            if sha(path) != expected:
                raise ValueError('Presentation receipt changed: ' + str(path))
    credits = (out / 'native/credits_viewport_rt64.hpp').read_text()
    for required in ('clip.uly=0;', 'clip.lry=height*4;',
                     'rdp.fillRect(-gutter,0,1280+gutter,height*4,alignment);',
                     'constrain(pair.drawColorRect);', 'constrain(pair.drawDepthRect);',
                     'if(enabled_)normalise_native_bounds(pair,height_);',
                     'normalise_native_bounds(workload.fbPairs[workload.currentFramebufferPairIndex()],height);'):
        if required not in credits:
            raise ValueError('Missing credits native readback invariant: ' + required)
    if 'clip.uly=-' in credits or 'rdp.fillRect(-gutter,-' in credits:
        raise ValueError('Negative credits fade rows cannot enter native readback')
    print('Presentation composition inspected; no game, replay or tests executed.')


def stage(base, out, renderer_base):
    if out.exists() or not out.resolve().is_relative_to(ROOT / 'build'):
        raise ValueError('Choose a NEW build stage')
    accepted = json.loads((base / 'credits-viewport-receipt.json').read_text())
    for name, expected in accepted['files'].items():
        if sha(base / name) != expected:
            raise ValueError('Accepted credits stage changed: ' + name)
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
    symbols = []
    for revision in (77, 80):
        text = read(ROOT / f'extern/dkr-decomp/ver/symbols/symbol_addrs.us.v{revision}.txt')
        values = []
        for name in ('dDialogueBoxBegin', 'dDialogueBoxDrawModes', 'gFonts',
                     'gNumberOfFonts', 'gCompactKerning', 'gRareCopyrightString',
                     'gSkydomeSegment', 'gWaveTextureHeader'):
            found = re.findall(r'^' + name + r' = (0x[0-9A-Fa-f]+);', text, re.M)
            if len(found) != 1:
                raise ValueError('Missing/ambiguous presentation symbol: ' + name)
            values.append(found[0] + 'U')
        symbols.append('    {' + ','.join(values) + '}')
    put('polish_symbols.hpp', '#pragma once\n#include <array>\n#include <cstdint>\n'
        'namespace dkr::runtime::presentation {\n'
        'inline constexpr std::array<std::array<std::uint32_t,8>,2> polish_symbols{{\n' +
        ',\n'.join(symbols) + '\n}};\n}\n')
    for name in ('intro_caption_rt64.hpp', 'intro_sky_rt64.hpp', 'water_residency_rt64.hpp'):
        put(name, read(AUTH / name))

    text = read(base / 'native/credits_viewport_rt64.hpp')
    text = once(text, '#include "hle/rt64_rsp.h"',
        '#include "hle/rt64_rsp.h"\n#include "hle/rt64_workload_queue.h"')
    # Only horizontal overscan is needed for the reported side-edge gap.
    # A negative Y enters unsigned framebuffer readback rows in the effective
    # renderer. Keep raster Y native and isolate display width from RAM bounds.
    text = once(text, 'const int gutter=int(std::ceil(640.F*(cover_-1.F)));',
        'const int gutter=int(std::ceil(640.F*(cover_-1.F)))+8;')
    text = once(text, '        state.flush();\n        rdp.scissorRectStack[at]=previous;', '''        state.flush();
        // Preserve the signed per-draw scissor/rectangle for widescreen, but
        // never expose overscan to native RAM/depth readback or target sizing.
        auto& workload=state.ext.workloadQueue->workloads[state.ext.workloadQueue->writeCursor];
        normalise_native_bounds(workload.fbPairs[workload.currentFramebufferPairIndex()],height);
        rdp.scissorRectStack[at]=previous;''')
    text = once(text, 'signed full-window rectangle and scissor; original opacity',
        'signed full-window rectangle and scissor; original opacity; horizontal raster-edge overlap; native readback bounds')
    text = once(text, '''    template<class Pair> void normalise(Pair& pair) const {
        if(!enabled_ || pair.colorImage.width!=320 || pair.scissorRect.isNull())return;
        pair.scissorRect.ulx=0;pair.scissorRect.lrx=320*4;
    }''', '''    template<class Pair> static void normalise_native_bounds(Pair& pair,int height) {
        if(pair.colorImage.width!=320)return;
        const RT64::FixedRect native(0,0,320*4,height*4);
        const auto constrain=[&](auto& rect) {
            if(rect.isNull())return;
            rect=rect.intersection(native);
            if(rect.isNull() || rect.isEmpty())rect.reset();
        };
        constrain(pair.drawColorRect);
        constrain(pair.drawDepthRect);
        // Aggregate extent only, not the saved per-draw signed clipping.
        if(!pair.scissorRect.isNull())pair.scissorRect=native;
    }
    template<class Pair> void normalise(Pair& pair) const {
        if(enabled_)normalise_native_bounds(pair,height_);
    }''')
    put('credits_viewport_rt64.hpp', text)

    text = read(base / 'native/flow_history_rt64.hpp')
    text = once(text, 'std::uint32_t scene_=~0U,model_=0;',
        'std::uint32_t scene_=~0U,model_=0;\n    std::uint64_t material_generation_=0;')
    text = once(text, 'double time,bool allowed) {',
        'double time,bool allowed,std::uint64_t material_generation=0) {')
    text = once(text, 'time<time_ || time-time_>0.25)',
        'time<time_ || time-time_>0.25 || material_generation!=material_generation_)')
    text = once(text, 'scene_=scene;model_=model;',
        'scene_=scene;model_=model;material_generation_=material_generation;')
    text = once(text, 'it->second.uv==uv && it->second.period==period ?',
        'it->second.uv==uv && it->second.period==period && it->second.image==image ?')
    # A texture-frame change alone is expected in a flowing animation. Keep
    # the accepted wrap/velocity rules across animation frames. Only real
    # replacement-set changes reset the native history.
    put('flow_history_rt64.hpp', text)

    text = read(base / 'native/enhanced_water_registry.hpp')
    text = once(text, '        auto glow=collect_glow && !animation_only ?',
        '        liquid_headers_.clear();\n        auto glow=collect_glow && !animation_only ?')
    text = once(text, '                auto kind=classify(e);', '''                auto kind=classify(e);
                if(kind!=Kind::None && texture<nt && e.texture_valid)
                    add_liquid_header(m.u32(textures+texture*8));''')
    text = once(text, '            if(kind==Kind::None || !ptr || !m.valid(ptr,32))continue;',
        '            if(kind==Kind::None || !ptr || !m.valid(ptr,32))continue;\n            add_liquid_header(ptr);')
    text = once(text, 'class Registry {', '''class Registry {
    std::vector<std::uint32_t> liquid_headers_;
    void add_liquid_header(std::uint32_t header) {
        if(liquid_headers_.size()<256 && std::find(liquid_headers_.begin(),liquid_headers_.end(),header)==liquid_headers_.end())
            liquid_headers_.push_back(header);
    }
public:
    const auto& liquid_headers() const {return liquid_headers_;}
private:''')
    put('enhanced_water_registry.hpp', text)

    text = read(base / 'native/f3ddkr_rt64.cpp')
    text = once(text, '#include "credits_sky_rt64.hpp"', '''#include "credits_sky_rt64.hpp"
#include "intro_caption_rt64.hpp"
#include "intro_sky_rt64.hpp"
#include "water_residency_rt64.hpp"''')
    text = once(text, '    presentation::IntroViewport intro_viewport;', '''    presentation::IntroViewport intro_viewport;
    presentation::IntroCaption intro_caption;
    presentation::IntroSky intro_sky;''')
    text = once(text, '    data_->intro_viewport.begin(', '''    data_->intro_caption.begin(*state,enhancements::modern_presentation_enabled(),data_->world_window_aspect);
    data_->intro_sky.begin(*state,enhancements::modern_presentation_enabled() && enhancements::fit_to_window_enabled());
    data_->intro_viewport.begin(''')
    text = once(text, '    if(opcode==kDisplayListOpcode)active_->data_->title_text.prepare(*state,command);', '''    if(opcode==kDisplayListOpcode) {
        active_->data_->title_text.prepare(*state,command);
        active_->data_->intro_caption.prepare(*state,command);
    }''')
    text = once(text, '    if(active_->data_->title_text.draw(*state,display_list))return;', '''    if(active_->data_->intro_caption.draw(*state,display_list,[&](std::uint32_t address,unsigned count) {
        RunCommands(state,reinterpret_cast<RT64::DisplayList*>(state->fromRDRAM(address&0x1FFFFFFFU)),count);
    }))return;
    if(active_->data_->title_text.draw(*state,display_list))return;''')
    text = once(text, '        if(!canonical_shadow)data.credits_sky.vertex(address,vertex);', '''        if(!canonical_shadow) {
            data.credits_sky.vertex(address,vertex);
            data.intro_sky.vertex(address,vertex);
        }''')
    text = once(text, '        water::flow_history().begin(presentation::task_scene_generation(),', '''        water::residency().prepare(*state,*data_->water_registry,presentation::task_scene_generation(),!enhanced_water);
        water::flow_history().begin(presentation::task_scene_generation(),''')
    text = once(text, '            presentation::task_interpolation_allowed());',
        '            presentation::task_interpolation_allowed(),state->ext.textureCache->getReplacementGeneration());')
    # Seal an old material before clearing it: never let previous water calls
    # inherit the zeroed material when quality is changed between tasks.
    text = once(text, '    } else if(state->waterMaterial.identity.x>0) {\n        state->waterMaterial={};',
        '    } else if(state->waterMaterial.identity.x>0) {\n        state->flush();state->waterMaterial={};')
    put('f3ddkr_rt64.cpp', text)
    # Correct DKR's optional mip extension in an isolated renderer build copy.
    # Never edit the submodule or the accepted generated renderer input.
    renderer = out / 'renderer'
    renderer.mkdir()
    (renderer / 'native_resource_safety.hpp').write_text(read(AUTH / 'native_resource_safety.hpp'), encoding='utf-8', newline='\n')
    mip = read(renderer_base / 'rt64_generated_mips.h')
    mip = once(mip, '#include "rt64_generated_mip_config.h"',
        '#include "rt64_generated_mip_config.h"\n#include "native_resource_safety.hpp"')
    mip = once(mip, 'std::unique_ptr<RenderBuffer> &upload) {',
        'std::unique_ptr<RenderBuffer> &upload,bool single_level=false) {')
    mip = once(mip, 'const uint32_t count = levelCount(w,h);',
        'const uint32_t count = single_level ? 1U : levelCount(w,h);')
    mip = once(mip, 'if(count<=1 || uint64_t(rowPitch)<uint64_t(w)*4 || uint64_t(rowPitch)*h>byteCount) return false;',
        'if((count<=1 && !single_level) || !bytes || !w || !h || w>16384 || h>16384 || uint64_t(w)*h>64ULL*1024*1024 || uint64_t(rowPitch)<uint64_t(w)*4 || uint64_t(rowPitch)*h>byteCount) return false;')
    mip = once(mip, 'if(total>MaxScratchBytes) return false;',
        'if(!single_level && total>MaxScratchBytes) return false;')
    mip = once(mip, 'if(!chain || !staging) return false;',
        'if(!resource_valid(chain.get()) || !resource_valid(staging.get())) return false;')
    mip = once(mip, 'dst.generatedMipmaps=true;', 'dst.generatedMipmaps=count>1;')
    mip = once(mip, '            if (!chain) return false;',
        '            if (!resource_valid(chain.get())) return false;')
    mip = once(mip, '                if (!tex) return false;',
        '                if (!resource_valid(tex.get())) return false;')
    (renderer / 'rt64_generated_mips.h').write_text(mip, encoding='utf-8', newline='\n')
    cache = read(renderer_base / 'rt64_texture_cache.cpp')
    cache = once(cache, '''                if (!generateMipmaps || !GeneratedMips::uploadRGBA32(*replacementTexture, device, commandList, data, byteCount, uint32_t(width), uint32_t(height), rowPitch, dstUploadResource)) {
                    TextureCache::setRGBA32(replacementTexture, device, commandList, data, byteCount, uint32_t(width), uint32_t(height), rowPitch, dstUploadResource, resourcePool, uploadResourcePoolMutex);
                }
                stbi_image_free(data);
                loadedTexture = true;''', '''                loadedTexture = generateMipmaps && GeneratedMips::uploadRGBA32(*replacementTexture, device, commandList, data, byteCount, uint32_t(width), uint32_t(height), rowPitch, dstUploadResource);
                if(!loadedTexture)
                    loadedTexture=GeneratedMips::uploadRGBA32(*replacementTexture,device,commandList,data,byteCount,uint32_t(width),uint32_t(height),rowPitch,dstUploadResource,true);
                stbi_image_free(data);
                if(!loadedTexture)GeneratedMips::upload_failure();''')
    (renderer / 'rt64_texture_cache.cpp').write_text(cache, encoding='utf-8', newline='\n')
    first_party_renderer = read(ROOT / 'build/sdk2-beta7-20261018-r18/stage/native/src/game/rt64_renderer.cpp')
    first_party_renderer = once(first_party_renderer, '    dkr::runtime::texture_packs::renderer_started();', '''    dkr::runtime::texture_packs::renderer_started();
    if(application_->textureCache && application_->device) {
        constexpr std::uint64_t MiB=1024ULL*1024;
        const auto dedicated=application_->device->getDescription().dedicatedVideoMemory;
        const auto budget=std::clamp<std::uint64_t>(dedicated/2,512*MiB,3072*MiB);
        application_->textureCache->setReplacementPoolMaxSize(budget);
        std::fprintf(stderr,"[texture-packs][budget] effective=%llu MiB; scene and mip-upload headroom reserved\\n",
            static_cast<unsigned long long>(budget/MiB));
    }''')
    put('rt64_renderer.cpp', first_party_renderer)
    doc = {'schema': 1, 'version': '1.0.7+beta.10', 'inputs': inputs,
        'accepted_credits': accepted,
        'authors': {p.relative_to(ROOT).as_posix(): sha(p) for p in AUTH.iterdir() if p.is_file()},
        'files': {p.relative_to(out).as_posix(): sha(p) for p in out.rglob('*') if p.is_file()},
        'policy': 'Patch Pipeline only. Native presentation; no dependency, guest simulation, save or timing changes. Owner performs gameplay tests.'}
    (out / 'presentation-polish-receipt.json').write_text(json.dumps(doc, indent=2) + '\n')


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('action', choices=('stage', 'verify'))
    p.add_argument('--base', type=Path)
    p.add_argument('--renderer-base', type=Path)
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    if a.action == 'stage':
        stage(a.base.resolve(strict=True), a.output, a.renderer_base.resolve(strict=True))
    verify(a.output)


if __name__ == '__main__':
    main()
