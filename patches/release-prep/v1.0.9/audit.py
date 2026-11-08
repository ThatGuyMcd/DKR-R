"""Verify the accepted native chain, stable metadata and final compiler bindings."""
from pathlib import Path
import argparse,hashlib,importlib.util,json,re,sys,types
sys.dont_write_bytecode=True
AUTH=Path(__file__).resolve().parent
ROOT=AUTH.parents[2]
ONLINE=ROOT/'patches/online-directory'

def need(ok,message):
    if not ok:raise ValueError(message)
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def load(name,path):
    if path.name=='audit.py':
        # Ninja escapes Windows drive colons (D$:/...). The retained desktop
        # audits normally read Visual Studio projects; decode that syntax when
        # inspecting the Android Ninja graph on Windows. Assertions are intact.
        text=path.read_text().replace("Path(m[1]).resolve()","Path(m[1].replace('$:',':').replace('$ ', ' ')).resolve()")
        module=types.ModuleType(name);module.__file__=str(path)
        exec(compile(text,str(path),'exec'),module.__dict__)
        if hasattr(module,'load'):module.load=load
        return module
    spec=importlib.util.spec_from_file_location(name,path);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);return m

def inspect(build,binary,baseline,accepted):
    stage=build/'generated/online-directory';ledger=build/'generated/release109/swaps.json';graph={};adapters=[];account_inputs=[]
    if ledger.exists():
        rows=json.loads(ledger.read_text())
        expected={('DKRNetplayCore','online_directory.cpp'):stage/'online_directory.cpp',('DKRPortGame','mod_store.cpp'):stage/'mod_store.cpp',
            ('DKRCustomVehicles','vehicle_binding.cpp'):build/'generated/custom-vehicles/native/vehicle_binding.cpp',
            ('DKRPortGame','achievements.cpp'):build/'generated/achievements/achievements.cpp',
            ('DKRPortGame','texture_dump.cpp'):build/'generated/texture-dumping/texture_dump.cpp'}
        need(len(rows)==5 and {(r['target'],Path(r['path']).name) for r in rows}==set(expected),'Unexpected release platform replacements')
        for row in rows:
            old=Path(row['path']);new=Path(row['new'])
            need(old.resolve()==expected[(row['target'],old.name)].resolve(),'Adapter input escaped accepted stage')
            need(new.resolve()==(build/'generated/release109'/old.name).resolve(),'Adapter output escaped release stage')
            if old.name=='online_directory.cpp':
                platform=load('stable109_platform',AUTH/'android_snapshot.py')
                need(new.read_bytes()==platform.compose(old,new.parent),'Android snapshot adapter bytes differ')
            elif old.name=='mod_store.cpp':
                platform=load('stable109_http',AUTH/'android_http.py');original,files=platform.compose(old,new.parent)
                for name,data in files.items():need((new.parent/name).read_bytes()==data,'Android HTTPS adapter bytes differ: '+name)
                transport=json.loads((new.parent/'transport.json').read_text())
                need(sha(original)==transport['input_sha256'] and sha(new.parent/'mod_store_android_http.inl')==transport['output_sha256'],'HTTPS platform receipt differs')
            elif old.name=='texture_dump.cpp':
                platform=load('stable109_texture',AUTH/'android_texture.py');gallery,helper,files=platform.compose(old,new.parent)
                for name,data in files.items():need((new.parent/name).read_bytes()==data,'Android texture cache adapter differs: '+name)
                for path,digest in json.loads((new.parent/'texture-inputs.json').read_text()).items():need(sha(Path(path))==digest,'Android texture input changed')
            else:
                platform=load('stable109_shared',AUTH/'android_shared.py')
                need(new.read_bytes()==platform.compose(old),'Android resource snapshot bytes differ')
            need(sha(old)==row['input_sha256'] and sha(new)==row['output_sha256'],'Platform source receipt differs')
            graph[(row['target'],old.resolve())]=new.resolve()
        adapters=rows
        account_inputs=json.loads((build/'generated/release109/account-inputs.json').read_text())
        need(len(account_inputs)==3 and {Path(h['path']).name for h in account_inputs}=={'store_account_page.inl','mod_store.hpp','mod_store_install.hpp'},'Unexpected Android account inputs')
        for header in account_inputs:
            expected_parent=build/'generated'/('store-vehicles' if Path(header['path']).name=='store_account_page.inl' else 'release-store')
            need(Path(header['path']).resolve().is_relative_to(expected_parent.resolve()),'Android account input escaped build')
            need(sha(Path(header['source']))==header['sha256']==sha(Path(header['path'])),'Retained account-page input changed')
        for row in rows:
            target=row['target'];project=build/(target+'.vcxproj')
            if project.exists():compiled={Path(v).resolve() for v in re.findall(r'<ClCompile Include="([^"]+)"',project.read_text(encoding='utf-8-sig'))}
            else:
                compiled={Path(m[1].replace('$:',':').replace('$ ',' ')).resolve() for line in (build/'build.ninja').read_text().splitlines() if line.startswith('build CMakeFiles/'+target+'.dir/') if (m:=re.search(r': [^ ]+ (\S+)',line))}
            need(Path(row['new']).resolve() in compiled and Path(row['path']).resolve() not in compiled,'Android final compiler replacement missing: '+row['new'])
    def resolve_binding(target,path):return graph.get((target,path),path)
    saved={name:sys.modules.get(name) for name in ('pipeline','transforms')}
    try:
        sys.modules['transforms']=load('stable109_directory_transforms',ONLINE/'transforms.py')
        sys.modules['pipeline']=load('stable109_directory_pipeline',ONLINE/'pipeline.py')
        # Retain every original source/security audit. Only extend its final
        # binding lookup through the explicit, byte-verified release adapter.
        path=ONLINE/'audit.py';text=path.read_text()
        anchor="Path(row['new']).resolve() in sources(row['target'])"
        need(text.count(anchor)==1,'Accepted directory binding assertion changed')
        text=text.replace(anchor,"resolve_binding(row['target'],Path(row['new']).resolve()) in sources(row['target'])",1)
        text=text.replace("Path(m[1]).resolve()","Path(m[1].replace('$:',':').replace('$ ', ' ')).resolve()")
        directory=types.ModuleType('stable109_directory_audit');directory.__file__=str(path)
        directory.__dict__['resolve_binding']=resolve_binding
        exec(compile(text,str(path),'exec'),directory.__dict__);directory.VERSION='1.0.9';directory.load=load
        result=directory.inspect(stage,binary,baseline)
    finally:
        for name,value in saved.items():
            if value is None:sys.modules.pop(name,None)
            else:sys.modules[name]=value
    previous=json.loads(accepted.read_text())['audit']
    need(result['directory_contract']==previous['directory_contract'],'Owner-accepted directory logic changed')
    need(result['retained']['scoreboard_contract']==previous['retained']['scoreboard_contract'],'Owner-accepted Scoreboard changed')
    for binding in result['compiler_bindings']:
        binding['source']=str(resolve_binding(binding['target'],Path(binding['source']).resolve()))
    cache=(build/'CMakeCache.txt').read_text()
    need(re.search(r'^DKR_RELEASE_VERSION:STRING=1\.0\.9$',cache,re.M),'Native stable version not configured')
    need(json.loads((build/'dkr-release.json').read_text())['version']=='1.0.9','Generated release version differs')
    need((ROOT/'VERSION').read_text().strip()=='1.0.9','Network release metadata differs')
    if binary:
        data=binary.read_bytes()
        need(b'1.0.9-public-lobbies-playtest.1' not in data,'Playtest version remains in native release')
        need(b'1.0.9-spectator-playtest.13' not in data,'Old spectator version remains in native release')
    result.update({'version':'1.0.9','label':'v1.0.9 Release Build','platform_adapters':adapters,'platform_account_inputs':account_inputs,
        'native_execution_performed':False,'accepted_pilot_receipt_sha256':sha(accepted)})
    return result

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('build','binary','baseline','accepted','output'):p.add_argument('--'+name,type=Path,required=name!='binary')
    a=p.parse_args();r=inspect(a.build,a.binary,a.baseline,a.accepted)
    a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(json.dumps(r,indent=2)+'\n')
    print('Stable metadata, accepted directory/Scoreboard, source baseline and actual compiler bindings verified.')
