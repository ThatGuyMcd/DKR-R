"""Read-only source/linked-byte inspection for the records and LOD repair."""
from pathlib import Path
import argparse
import hashlib
import importlib.util
import json
import re
import sys
sys.dont_write_bytecode=True
AUTH=Path(__file__).resolve().parent
ROOT=AUTH.parents[1]
VERSION='1.0.9-detail-playtest.2'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def require(ok,why):
 if not ok:raise ValueError(why)
def load(name,path):
 spec=importlib.util.spec_from_file_location(name,path);module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module);return module
def inspect(stage,binary=None):
 upstream=load('retained_all_player_audit',ROOT/'patches/all-player-detail/audit.py').inspect(stage.parent/'all-player-detail')
 rows=json.loads((stage/'swaps.json').read_text())
 require({Path(r['new']).name for r in rows}=={'runtime_enhancements.cpp','presentation_identity.cpp','local_model_rt64.cpp','runtime_legacy_mods.cpp'} and len(rows)==4,'Final repair target coverage')
 for row in rows:
  require(sha(Path(row['path']))==row['input_sha256'],'Repair input changed: '+row['path'])
  require(sha(Path(row['new']))==row['output_sha256'],'Repair output changed: '+row['new'])
  require(not Path(row['new']).resolve().is_relative_to(ROOT),'Repair staged inside checkout')
 for p in (AUTH/'native').iterdir():require(sha(p)==sha(stage/p.name),'Native repair not staged: '+p.name)
 records=(stage/'records_guest.inl').read_text()
 for marker in ('0x80123510U','0x80123A90U','assets->boot_bank()->revision()','course.level_id<65||course.level_id>=128','valid(table,128*2,2)','if(!restore||!valid(p,2,2))','if(!good){if(restore)return {};continue;}','rdram+((p-0x80000000U)^2U)'):
  require(marker in records,'Missing records ownership/layout guard: '+marker)
 require('payload->get_settings' not in records and 'dkr_probe_run' not in records,'Records observer still executes guest code')
 require(records.index('for(const auto& row:rows)')>records.index('if(!good){if(restore)return {};continue;}'),'Record restoration writes before complete destination validation')
 runtime=(stage/'runtime_legacy_mods.cpp').read_text()
 require('#include "records_guest.inl"' in runtime and 'world.error().empty()' in runtime,'Underlying record error hidden or wrong observer included')
 policy=(stage/'runtime_enhancements.cpp').read_text()
 policy=policy[policy.index('extern "C" void dkr_apply_maximum_racer_detail('):policy.index('extern "C" void dkr_restore_multiplayer_race_music(')]
 require('if(dkr::runtime::netplay::session().runtime_view().active)return;' in policy,'Online selector not canonical')
 require('ViewportLayout' not in policy and 'context->r8 = context->r4' in policy and 'kTimeTrialGhostBehaviour' in policy,'Local view LOD restriction still active or highest selector/ghost protection lost')
 observer=(stage/'detail_observer.inl').read_text()
 require('||!netplay::session().runtime_view().active)return;' in observer,'Local multiplayer still records parallel model work')
 identity=(stage/'presentation_identity.cpp').read_text()
 require('#include "detail_observer.inl"' in identity,'Final identity TU not bound to repaired observer')
 model=(stage/'local_model_rt64.cpp').read_text()
 for marker in ('std::lower_bound(p.observations.begin()','std::upper_bound(p.suppressed.begin()','if(!o.extra)p.suppress(','#include "detail_batch.inl"','detail::MicroScope prepare_cost','detail::MicroScope submit_cost','retain_texture(r,p)','p.texture_scene!=scene','std::erase_if(p.observations'):
  require(marker in model,'Missing hot-path repair: '+marker)
 require('for(auto& o:p.observations)' not in model[model.index('void LocalModelRT64Pass::draw_at'):],'Per-command linear scan remains')
 batch=(stage/'detail_batch.inl').read_text()
 for marker in ('std::array<Corner,30> batch','batch_flags!=step.w0||batch_texture!=step.w1','batch[end].matrix==batch[first].matrix','std::uint8_t(end-first),std::uint8_t(first)','submit_batch();command','driver_mask->hide[step.source_triangle]'):
  require(marker in batch,'Missing batch boundary/mask: '+marker)
 require('rsp.setVertex(kVertices+c*sizeof' not in model,'Per-corner transform uploads remain')
 cache=(stage/'detail_texture_cache.inl').read_text()
 for marker in ('std::memcmp(r.ram.data()+input.physical','dependency(header,native_bytes)','dependency(commands,count*8U)','dependency(r.word(commands+c*8+4)&0xFFFFFFU,end-begin)','entry.checked!=texture_frame','retained_textures.size()<256','4U*1024U*1024U-retained_texture_bytes'):
  require(marker in cache,'Missing material cache invalidation/bound: '+marker)
 contracts=json.loads((stage/'contracts.json').read_text())
 require(set(contracts)=={'77','80'} and all(re.fullmatch('[a-f0-9]{64}',v) for v in contracts.values()),'Missing repaired peer contracts')
 if binary:
  data=binary.read_bytes()
  # GCC can prove the fixed-address word reads are in bounds and remove that
  # unreachable error string. Inspect guards above; use reachable service
  # diagnostics as the platform-independent linked-byte evidence instead.
  for marker in (VERSION.encode(),b'local-all-views-v2',b'batched-detail-v2',b'Could not capture custom-course records from the owned guest: ',b'Custom records have an unsupported owned asset revision.',b'material cache input escapes source snapshot',b'collection=94 local-only v1',b'complete-family selection',b'custom vehicle submitted'):
   require(marker in data,'Missing linked repair/retained feature: '+repr(marker))
 return {'schema':1,'version':VERSION,'swaps':rows,'peer_contracts':contracts,'retained_all_player_audit':upstream,
  'binary_sha256':sha(binary) if binary else None,'validation':'Compile/link and static package/source checks only. No game, mod, test executable or AppImage run. Owner gameplay/performance acceptance pending.'}
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--stage',type=Path,required=True);p.add_argument('--binary',type=Path);p.add_argument('--output',type=Path);a=p.parse_args();report=inspect(a.stage,a.binary)
 if a.output:a.output.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
 else:print(json.dumps(report,indent=2))
