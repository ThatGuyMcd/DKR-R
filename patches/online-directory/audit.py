"""Audit final compiler bindings, retained repairs and private deployment inputs."""
from pathlib import Path
import argparse,hashlib,importlib.util,json,re,sys
sys.dont_write_bytecode=True
import pipeline
AUTH=Path(__file__).resolve().parent
ROOT=AUTH.parents[1]
VERSION='1.0.9-public-lobbies-playtest.1'
def need(ok,message):
 if not ok:raise ValueError(message)
def load(name,path):
 spec=importlib.util.spec_from_file_location(name,path);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);return m
def contract():
 h=hashlib.sha256()
 for p in sorted([AUTH/'ReleaseOverlay.cmake',AUTH/'pipeline.py',AUTH/'transforms.py',*(AUTH/'native').iterdir()],key=lambda p:p.name):
  if p.is_file():h.update(p.name.encode());h.update(p.read_bytes().replace(b'\r\n',b'\n'))
 return h.hexdigest()
def inspect(stage,binary=None,baseline=None,previous=None):
 swaps,includes,outputs=pipeline.compose(json.loads((stage/'inputs.json').read_text()),stage)
 need(swaps==json.loads((stage/'swaps.json').read_text()),'Directory source receipt differs')
 need(includes==json.loads((stage/'includes.json').read_text()),'Directory UI receipt differs')
 for name,data in outputs.items():
  need(not (stage/name).resolve().is_relative_to(ROOT),'Native output escaped external stage')
  need((stage/name).read_bytes()==data,'Stale staged file: '+name)
 saved={name:sys.modules.get(name) for name in ('pipeline','transforms')}
 try:
  sys.modules['transforms']=load('directory_scoreboard_transforms',ROOT/'patches/rollback-scoreboard/transforms.py')
  sys.modules['pipeline']=load('directory_scoreboard_pipeline',ROOT/'patches/rollback-scoreboard/pipeline.py')
  prior=load('directory_scoreboard_audit',ROOT/'patches/rollback-scoreboard/audit.py')
 finally:
  for name,value in saved.items():
   if value is None:sys.modules.pop(name,None)
   else:sys.modules[name]=value
 retained=prior.inspect(stage.parent/'rollback-scoreboard')
 if previous:
  accepted=json.loads(previous.read_text())['audit']
  need(retained['scoreboard_contract']==accepted['scoreboard_contract'],'Accepted Scoreboard contract changed')
 build=stage.parent.parent;bindings=[]
 def sources(target):
  project=build/(target+'.vcxproj')
  if project.exists():return {Path(value).resolve() for value in re.findall(r'<ClCompile Include="([^"]+)"',project.read_text(encoding='utf-8-sig'))}
  return {Path(m[1]).resolve() for line in (build/'build.ninja').read_text().splitlines() if line.startswith('build CMakeFiles/'+target+'.dir/') if (m:=re.search(r': [^ ]+ (\S+)',line))}
 for row in swaps+[{'target':'DKRNetplayCore','new':str(stage/'online_directory.cpp')}]:
  need(Path(row['new']).resolve() in sources(row['target']),'Final compiler source is missing: '+row['new'])
  if 'path' in row:need(Path(row['path']).resolve() not in sources(row['target']),'Superseded source is still compiled')
  bindings.append({'target':row['target'],'source':row['new']})
 for row in swaps:
  need(not any(part.lower() in ('extern','recompiledfuncs','recompiledpatches') for part in Path(row['path']).parts),'Forbidden authored replacement')
 direct=outputs['direct_session.cpp'].decode();owner=outputs['owner_pump.inl'].decode();native=outputs['online_directory.cpp'].decode();ui=outputs['runtime_online_ui.inl'].decode()
 for marker in ('dkr::online::native::authorize','binding->client!=pending->client_public','binding->privacy!=owner->privacy',
                'gate->phase==dkr::runtime::party_start::Phase::Committing','owner->blocked.contains(binding->account)',
                'dkr::online::client_key(this)','owner_state.hpp','owner_pump.inl'):
  need(marker in direct,'Native admission safeguard missing: '+marker)
 for marker in ('roster=snapshot.generation','party->stopping','"returning"','"reservedSpectators"',
                'consume_friend_admission_locked(proof)','owner->policy!=policy','owner->privacy','dkr::runtime::rollback_check::active(this)'):
  need(marker in owner,'Owner lifecycle safeguard missing: '+marker)
 for forbidden in ('world.', 'receiver.', 'CheckpointReceiver', 'run_runtime(', 'set_ready(', 'request_start('):
  need(forbidden not in owner+native,'Directory may alter gameplay: '+forbidden)
 for marker in ('dkrr-online-v1','dkrr-ticket-','/v1/online/session','identity.generation!=generation',
                'host_revision=current.value','std::erase_if(s.ui.rooms','channel==s.channel'):
  need(marker in native,'Recovery or authentication safeguard missing: '+marker)
 need('config.caCertificatePemFile=directory_ca_bundle()' in native and 'disableTlsVerification' not in native,'Directory TLS verification is not explicit')
 tls=outputs['directory_tls.hpp'].decode()
 need('CertOpenSystemStoreW(0,L"ROOT")' in tls and '/etc/ssl/certs/ca-certificates.crt' in tls,'Platform TLS trust sources missing')
 quick=next(path for path in sources('DKRNetplayCore') if path.name=='quick_join_transport.cpp')
 retained_root=re.search(r'R"PEM\((.*?)\)PEM"',quick.read_text(),re.S)
 directory_root=re.search(r'R"PEM\((.*?)\)PEM"',tls,re.S)
 need(retained_root and directory_root and retained_root[1]==directory_root[1],'Accepted TLS fallback root differs')
 for marker in ('BeginOlScoreboard(kResults)','DirectoryVisibilityHeight','DrawOlDirectoryAccount','DrawOlPublicLobbies','g_directory_friends_view'):
  need(marker in ui,'Retained UI/integration missing: '+marker)
 account=outputs['mod_store.cpp'].decode()
 need('CryptProtectData' in account and 'keyring::store' in account and 'keyring::load' in account,'Secure session persistence paths missing')
 need('online_instance' in account and 'account_bind.inl' in account,'Shared account bridge missing')
 checked=0
 if baseline:
  for row in json.loads(baseline.read_text()):
   need(pipeline.sha((ROOT/row['path']).read_bytes())==row['sha256'],'Pre-existing authored source changed: '+row['path']);checked+=1
 if binary:
  data=binary.read_bytes()
  for marker in (VERSION.encode(),b'dkrr-online-v1',b'/v1/online/ticket',b'players online',b'PRIVATE - friends or lobby code',
                 b'RACERS - CONNECTION TO HOST',b'SPECTATORS - PLAYBACK TIMING',b'[lobby][countdown] five seconds',
                 b'[party][checkpoint] first chunk received',b'[lobby][leave] application close notice',b'PLAYER SWAP REQUESTS',b'BACK TO MY RACE'):
   need(marker in data,'Linked current/retained feature missing: '+repr(marker))
 return {'schema':1,'version':VERSION,'directory_contract':contract(),'protocol':1,
  'unchanged_authored_files':checked,'swaps':swaps,'includes':includes,'compiler_bindings':bindings,'retained':retained,
  'validation':'Native compilation/linking, compile-time admission assertions and static source/binary/package checks. No game, mod, native test executable or AppImage was run. Backend runtime checks are recorded separately. Owner network/controller acceptance remains required.'}
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__)
 for name in ('stage','binary','baseline','previous','output'):p.add_argument('--'+name,type=Path,required=name=='stage')
 a=p.parse_args();result=inspect(a.stage,a.binary,a.baseline,a.previous)
 if a.output:a.output.write_text(json.dumps(result,indent=2)+'\n')
 else:print(json.dumps(result,indent=2))
