"""Inspect final source composition and linked bytes. Never execute targets."""
from pathlib import Path
import argparse
from collections import Counter
import hashlib
import importlib.util
import json
import re
import sys
sys.dont_write_bytecode=True
AUTH=Path(__file__).resolve().parent
ROOT=AUTH.parents[1]
VERSION='1.0.9-detail-playtest.1'

def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def require(value,message):
 if not value:raise ValueError(message)
def load(name,path):
 spec=importlib.util.spec_from_file_location(name,path);module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module);return module
def inspect(stage,binary=None):
 swaps=json.loads((stage/'swaps.json').read_text(encoding='utf-8'))
 expected=Counter(DKRPortGame=7,DKRLocalModelV77=1,DKRLocalModelV80=1,
  DKRExperimentalOwnedGameV77=2,DKRExperimentalOwnedGameV80=2,DKRRecompiledFuncsV77=2,DKRRecompiledFuncsV80=2)
 require(Counter(r['target'] for r in swaps)==expected,'Incomplete target composition')
 for r in swaps:
  require(not Path(r['new']).resolve().is_relative_to(ROOT),'Output inside checkout')
  require(sha(Path(r['path']))==r['input_sha256'],'Upstream input changed: '+r['path'])
  require(sha(Path(r['new']))==r['output_sha256'],'Generated copy changed: '+r['new'])
 for path in (AUTH/'native').iterdir():require(sha(path)==sha(stage/path.name),'Authored file not staged: '+path.name)
 coverage=json.loads((stage/'coverage.json').read_text())
 require(coverage=={t:({'residency':1,'mesh':1} if t.startswith('DKRRecompiledFuncs') else {'residency':1}) for t in expected if t.startswith(('DKRRecompiledFuncs','DKRExperimentalOwnedGame'))},'Incomplete two-revision hook coverage')
 native=lambda name:(stage/'DKRPortGame'/name).read_text(encoding='utf-8')
 observer=(stage/'detail_observer.inl').read_text(encoding='utf-8')
 for marker in ('!netplay::external_side_effects_allowed()','highest==current','half(object+0x48)!=1','g_detail_maps[g_recording_buffer&1U]','std::memcpy(words,&e,sizeof(e))','dkr_owned_draw_event_valid(&e)'):
  require(marker in observer,'Missing observer boundary: '+marker)
 require(not re.search(r'(?:ram|rdram)\s*\[[^;]*\]\s*=(?!=)',observer),'Observer writes guest memory')
 require(not re.search(r'ctx->\w+\s*=(?!=)',observer),'Observer mutates guest registers')
 identity=native('presentation_identity.cpp')
 for marker in ('frame.details=std::move(g_detail_maps[g_recording_buffer&1U])','g_active_detail=std::move(frame.details)','frame.markers.clear();frame.details.clear()','rows:g_detail_maps','g_active_marker_map.clear();g_active_detail.clear()'):
  require(marker in identity,'Missing immutable task ownership: '+marker)
 renderer=native('rt64_renderer.cpp')
 require(renderer.index('TaskIdentityScope identity_scope')<renderer.index('const bool detail_requested='),'Decoder policy checked before task ownership')
 require(renderer.count('detail_requested?&ordinary_models:nullptr')==3,'Ordinary decoder branch omitted')
 for marker in ('camera_requested||cars_requested||detail_requested','camera_decoder_.resize(DKR_CAMERA_KERNEL_BYTES)','std::memcpy(camera_decoder_.data(),rdram_snapshot,0x800000)','std::span<std::uint8_t>{camera_decoder_}'):
  require(marker in renderer,'Missing private decode arena: '+marker)
 enhancements=native('runtime_enhancements.cpp')
 policy=enhancements[enhancements.index('extern "C" void dkr_apply_maximum_racer_detail('):enhancements.index('extern "C" void dkr_restore_multiplayer_race_music(')]
 for marker in ('session().runtime_view().active','ViewportLayout))!=0)return','maximum_detail_enabled()','context->r8 = context->r4','kTimeTrialGhostBehaviour'):
  require(marker in policy,'Missing all-mode/native compatibility gate: '+marker)
 model=native('local_model_rt64.cpp')
 for marker in ('std::array<CachedPacket,128> racer_cache','std::array<unsigned,4> racer_triangles','detail::PreparedSteps','detail::ViewTriangles','detail::KernelOperations','p.build(o.e,false)','p.build(o.e,o.extra)','driver_mask->hide[step.source_triangle]','NativeDrawScope restore(state)','mix_identity(o.e.parameters[2])','o.e.token*0x9E3779B9U','o.replaced=true','original retained'):
  require(marker in model,'Missing renderer isolation/capacity: '+marker)
 require(model.index('p.build(o.e,false)')<model.index('Reader r{ram};\n        std::array<std::vector<std::uint32_t>,4> drawn;'),'Racers must be prepared before extra scenery')
 require(model.index('driver_mask->hide.size()')<model.index('NativeDrawScope restore(state)'),'Mask validation occurs after GPU drawing')
 bridge=native('f3ddkr_rt64.cpp')
 for marker in ('if(cars_requested||models)kRDRAMSize=0x1000000U','!active_->data_->online_view->suppress_rectangle(*state)'):
  require(marker in bridge,'Missing decoder/view boundary: '+marker)
 vehicle=native('vehicle_rt64.cpp')
 require('maximum_detail_?0U:' in vehicle and 'custom vehicle submitted' in vehicle,'Custom vehicles do not select/record their actual highest LOD')
 for rev in (77,80):
  kernel=(stage/f'DKRLocalModelV{rev}/visual_model_kernel.c').read_text(encoding='utf-8')
  fields={k:int(v,0) for k,v in re.findall(r'\b([A-Z][A-Z0-9_]*)\s*=\s*(0x[0-9A-Fa-f]+|\d+)',kernel)}
  spans=[(fields[k],fields[k]+fields['MAX_VERTICES']*10,k) for k in ('VERTICES','VERTEX2','DELTAS')]
  spans += [(fields['FRACTION'],fields['COMMANDS'],'fractions'),(fields['COMMANDS'],fields['COMMAND_END'],'commands'),(fields['STACK']-65536,fields['STACK']+64,'stack')]
  spans.sort()
  require(fields['MAX_VERTICES']==8192 and all(a[1]<=b[0] for a,b in zip(spans,spans[1:])),'Private model arenas overlap')
  require(all(0xC10000<=a<b<=0xD00000 for a,b,_ in spans),'Private model arena escapes ownership')
  require('width!=1 && width!=2 && width!=4' in kernel and 'model presentation private write span' in kernel,'Kernel ownership guards removed')
  owned=(stage/f'DKRExperimentalOwnedGameV{rev}/probe_native.c').read_text(encoding='utf-8')
  require('local_models[256]' in owned and 'if(local_model_count==256)' in owned,'Owned view observation capacity not staged')
  noop=owned[owned.index('static int canonical_identity_hook'):]
  require('"dkr_apply_maximum_racer_detail"' in noop,'Canonical detail hook is not neutralised')
  for row in swaps:
   if row['target'] not in (f'DKRRecompiledFuncsV{rev}',f'DKRExperimentalOwnedGameV{rev}') or not row.get('guest'):continue
   text=Path(row['new']).read_text(encoding='utf-8')
   if 'all-player-residency-v1' in text:
    require('MEM_BU(0x54,ctx->r2)==1U' in text and 'MEM_W(0X6C,ctx->r29)&=~12U' in text,'Racer-only flag patch missing')
    require('maximum_detail_enabled' not in text,'Local preference leaked into canonical guest')
 ui=native('runtime_ui.cpp')
 require('#include "all_detail_graphics.inl"' in ui,'Launcher not wired to staged help')
 require('highest-detail characters and vehicles in every view' in (stage/'all_detail_graphics.inl').read_text(encoding='utf-8'),'Setting help not updated')
 contracts=json.loads((stage/'contracts.json').read_text())
 require(set(contracts)=={'77','80'} and all(re.fullmatch('[a-f0-9]{64}',v) for v in contracts.values()),'Missing peer compatibility contracts')
 achievements=load('retained_achievement_audit',ROOT/'patches/achievements/audit.py').inspect(stage.parent/'achievements')
 if binary:
  data=binary.read_bytes()
  for marker in (VERSION.encode(),b'all-player-detail-v1',b'highest resident presentation mesh',b'custom vehicle submitted',b'collection=94 local-only v1',b'ACHIEVEMENT UNLOCKED',b'complete-family selection'):
   require(marker in data,'Linked feature/version missing: '+repr(marker))
 return {'schema':1,'version':VERSION,'swaps':swaps,'coverage':coverage,'peer_contracts':contracts,
  'retained_achievement_count':achievements['count'],'binary_sha256':sha(binary) if binary else None,
  'validation':'Compilation and static inspection only. No game, mod, gameplay test or AppImage was executed. Owner gameplay acceptance remains pending.'}
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--stage',type=Path,required=True);p.add_argument('--binary',type=Path);p.add_argument('--output',type=Path);a=p.parse_args();report=inspect(a.stage,a.binary)
 if a.output:a.output.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
 else:print(json.dumps(report,indent=2))
