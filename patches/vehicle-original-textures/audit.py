"""Verify bounded vehicle preparation and the entire accepted repair chain."""
from pathlib import Path
import argparse,importlib.util,json,re,sys
sys.dont_write_bytecode=True
import pipeline
AUTH=Path(__file__).resolve().parent;ROOT=AUTH.parents[1]
def need(ok,why):
    if not ok:raise ValueError(why)
def load(name,path):
    s=importlib.util.spec_from_file_location(name,path);m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m
def compiled(build,target):
    project=build/(target+'.vcxproj')
    if project.is_file():return {Path(p).resolve() for p in re.findall(r'<ClCompile Include="([^"]+)"',project.read_text(encoding='utf-8-sig'))}
    return {Path(m[1]).resolve() for line in (build/'build.ninja').read_text().splitlines()
        if line.startswith('build CMakeFiles/'+target+'.dir/') if (m:=re.search(r': [^ ]+ (\S+)',line))}
def inspect(build,binary,baseline,accepted):
    stage=build/'generated/vehicle-original-textures';rows,files=pipeline.compose(json.loads((stage/'inputs.json').read_text()),stage)
    need(rows==json.loads((stage/'swaps.json').read_text()),'Car texture receipt differs')
    for name,data in files.items():need((stage/name).read_bytes()==data,'Stale car texture input: '+name)
    for row in rows:
        sources=compiled(build,row['target']);need(Path(row['new']).resolve() in sources and Path(row['path']).resolve() not in sources,'Car compiler binding differs')
    package=files['DKRCustomVehicles/vehicle_package.cpp'].decode();render=files['DKRPortGame/vehicle_rt64.cpp'].decode()
    for marker in ('original-resolution-v1','original_textures::prepare(*p)','original_textures::seating_contract(manifest,*p)',
        'if(!original_resolution&&texels*2>4096)','if(profile=="native-4kib-rgba32-safe-v1"&&texels*4>4096)',
        'PNG dimensions exceed the vehicle image limit.','vehicle textures exceed their decoded memory budget.',
        'LOD textures differ from the root material table.','a mesh references a missing texture.'):
        need(marker in package,'Car admission guard missing: '+marker)
    for row in rows:
        if row['target']=='DKRPortGame':
            original=Path(row['path']).read_text()
            for marker in ('NativeDrawScope restore(s);','rdp.setTextureImage(','rdp.loadTile(','rsp.drawIndexedTri(','wheel_interpolation::record(', 's.flush();'):
                need(original.count(marker)==render.count(marker),'Existing draw lifecycle changed: '+marker)
    need('original_textures::model_anchor(car,driver_->base,driver_->native_scale)' in render,'Explicit anchor units not bound')
    saved=sys.modules.get('pipeline')
    try:
        prior_pipeline=load('car_handoff_pipeline',ROOT/'patches/graphics-handoff/pipeline.py');prior_pipeline.VERSION=pipeline.VERSION
        sys.modules['pipeline']=prior_pipeline
        prior=load('car_handoff_audit',ROOT/'patches/graphics-handoff/audit.py')
        retained=prior.inspect(build,binary,baseline,accepted)
    finally:
        if saved is None:sys.modules.pop('pipeline',None)
        else:sys.modules['pipeline']=saved
    if binary:
        need(binary.stat().st_mtime_ns>=max((stage/name).stat().st_mtime_ns for name in files),'Binary predates final car preparation')
        data=binary.read_bytes()
        for marker in (pipeline.VERSION,'[custom-vehicles][original-textures]','original RGBA8 pixels retained; 4 KiB limits unchanged',
            'unsupported explicit seating contract revision.','car textures must fit a native 4 KiB material tile',
            'custom boost anchors are not supported yet; export with the native boost position.'):
            need(marker.encode() in data,'Linked car compatibility missing: '+marker)
    return {'schema':1,'version':pipeline.VERSION,'swaps':rows,'rollback_build_ids':retained['rollback_build_ids'],
        'retained':retained,'package_abi_changed':False,'canonical_game_or_wire_state_changed':False,'protected_sources_edited':False,
        'validation':'Exact preparation composition and two final compiler bindings, unchanged draw lifecycle, retained whole graphics/memory/online chain and linked guards. Actual gameplay acceptance remains separate.'}
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('build','binary','baseline','accepted','output'):p.add_argument('--'+name,type=Path,required=name!='binary')
    a=p.parse_args();result=inspect(a.build,a.binary,a.baseline,a.accepted)
    a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(json.dumps(result,indent=2)+'\n')
    print('Original car textures/native anchor units and retained graphics/online repairs verified.')
