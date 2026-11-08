"""Compose workload-atomic HD water selection in a new isolated build stage."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import re
import sys
sys.dont_write_bytecode=True
AUTH=Path(__file__).resolve().parent
ROOT=AUTH.parents[1]

def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def translate(name):
    name=name.replace('\\','/')
    if os.name!='nt' and re.match(r'^[A-Za-z]:/',name):name='/mnt/'+name[0].lower()+name[2:]
    return Path(name)
def once(text,old,new):
    if text.count(old)!=1:raise ValueError('Liquid streaming anchor changed: '+old[:100])
    return text.replace(old,new)
def verify(out):
    receipt=json.loads((out/'liquid-streaming-receipt.json').read_text())
    for name,expected in receipt['inputs'].items():
        if sha(translate(name))!=expected:raise ValueError('Liquid input changed: '+name)
    for name,expected in receipt['files'].items():
        if sha(out/name)!=expected:raise ValueError('Liquid stage changed: '+name)
    print('Liquid streaming source receipt inspected; no application execution.')
def stage(base,renderer,out):
    if out.exists() or not out.resolve().is_relative_to(ROOT/'build'):raise ValueError('Choose a NEW stage under build')
    accepted=json.loads((base/'performance-receipt.json').read_text())
    for name,expected in accepted['files'].items():
        if sha(base/name)!=expected:raise ValueError('Accepted performance stage changed: '+name)
    inputs={}
    def read(path):inputs[str(path)]=sha(path);return path.read_text()
    def put(name,text):
        path=out/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_text(text,encoding='utf-8',newline='\n')
    for folder in ('native','renderer'):
        for path in (base/folder).rglob('*'):
            if path.is_file():put(path.relative_to(base).as_posix(),read(path))
    for name in ('water_residency_rt64.hpp','liquid_replacement_gate.hpp'):put('native/'+name,read(AUTH/name))
    cache=read(base/'renderer/rt64_texture_cache.cpp')
    cache=once(cache,'#include "rt64_generated_mips.h"','#include "rt64_generated_mips.h"\n#include "liquid_replacement_gate.hpp"')
    cache=once(cache,'    ReplacementMap::~ReplacementMap() {','    ReplacementMap::~ReplacementMap() {\n        dkr::runtime::water::streaming::forget(this);')
    cache=once(cache,'    void ReplacementMap::clear(std::vector<Texture *> &evictedTextures) {','    void ReplacementMap::clear(std::vector<Texture *> &evictedTextures) {\n        dkr::runtime::water::streaming::forget(this);')
    cache=once(cache,'            unusedTextureList.pop_back();','            unusedTextureList.pop_back();\n            dkr::runtime::water::streaming::changed(this);')
    cache=once(cache,'        entry.texture = texture;','        entry.texture = texture;\n        dkr::runtime::water::streaming::changed(this);')
    put('renderer/rt64_texture_cache.cpp',cache)
    frame=read(renderer/'rt64_framebuffer_renderer.cpp')
    frame=once(frame,'#include "rt64_framebuffer_renderer.h"','#include "rt64_framebuffer_renderer.h"\n#include "liquid_replacement_gate.hpp"')
    frame=once(frame,'        textureCacheReplacementMapEnabled = textureCache->textureMap.replacementMapEnabled;',
        '''        textureCacheReplacementMapEnabled = textureCache->textureMap.replacementMapEnabled;
        dkr::runtime::water::streaming::capture(this,textureCache->textureMap,
            textureCacheTextureReplacements,textureCacheVersions,textureCacheGlobalVersion);''')
    anchor='''                // Describe the GPU tile for a regular texture.'''
    frame=once(frame,anchor,'''                // The workload's descriptor snapshot is authoritative while a
                // liquid family streams. Native dimensions/flags MUST match
                // its native descriptor; toggling highRes alone is incorrect.
                if(dkr::runtime::water::streaming::blocked(this,callTile.tmemHashOrID) &&
                   textureIndex<textureCacheTextures.size() && textureCacheTextures[textureIndex]) {
                    const auto* native=textureCacheTextures[textureIndex];
                    textureReplaced=false;gpuTile.tcScale={1.0f,1.0f};
                    gpuTile.textureDimensions={float(native->width),float(native->height),float(std::max(native->mipmaps,1U))};
                    hasMipmaps=native->mipmaps>1;generatedMipmaps=native->generatedMipmaps;shiftedByHalf=false;
                }

'''+anchor)
    # Bound thread-local snapshots to the actual renderer lifetime.
    frame=once(frame,'    FramebufferRenderer::~FramebufferRenderer() {','    FramebufferRenderer::~FramebufferRenderer() {\n        dkr::runtime::water::streaming::release_frame(this);')
    put('renderer/rt64_framebuffer_renderer.cpp',frame)
    # Verify unchanged accepted native presentation/flow and GPU upload bodies.
    checks={}
    for name in accepted['files']:
        if name in ('native/water_residency_rt64.hpp','renderer/rt64_texture_cache.cpp'):continue
        if sha(base/name)!=sha(out/name):raise ValueError('Unrelated input changed: '+name)
        checks[name]=sha(out/name)
    for token in ('directWorker->wait();','worker->wait();'):
        if cache.count(token)!=(base/'renderer/rt64_texture_cache.cpp').read_text().count(token):raise ValueError('GPU wait changed')
    receipt={'schema':1,'version':'1.0.7+beta.10','inputs':inputs,
        'files':{p.relative_to(out).as_posix():sha(p) for p in out.rglob('*') if p.is_file()},
        'unchanged_accepted_files':checks,
        'policy':'Patch Pipeline only; isolated renderer source copies; no checked-out dependency, generated guest code, GPU lifetime, shader, simulation, save or online identity change.',
        'validation':'Compilation and static source/package inspection only; owner visual acceptance pending.'}
    (out/'liquid-streaming-receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
    verify(out)
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('action',choices=('stage','verify'))
    p.add_argument('--base',type=Path);p.add_argument('--renderer',type=Path);p.add_argument('--output',type=Path,required=True)
    a=p.parse_args()
    if a.action=='stage':stage(a.base.resolve(strict=True),a.renderer.resolve(strict=True),a.output)
    else:verify(a.output)
if __name__=='__main__':main()
