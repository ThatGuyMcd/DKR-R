"""Audit the entire accepted repair chain at the stable release version."""
from pathlib import Path
import argparse,importlib.util,json,sys
sys.dont_write_bytecode=True
AUTH=Path(__file__).resolve().parent;ROOT=AUTH.parents[2];VERSION=(ROOT/'VERSION').read_text().strip()
def load(name,path):
    s=importlib.util.spec_from_file_location(name,path);m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m
def inspect(build,binary,baseline,accepted):
    previous=ROOT/'patches/online-performance';saved=sys.modules.get('pipeline')
    try:
        pipeline=load('release111_vehicle_pipeline',previous/'pipeline.py');pipeline.VERSION=VERSION
        sys.modules['pipeline']=pipeline
        result=load('release111_vehicle_audit',previous/'audit.py').inspect(build,binary,baseline,accepted)
    finally:
        if saved is None:sys.modules.pop('pipeline',None)
        else:sys.modules['pipeline']=saved
    if (ROOT/'VERSION').read_text().strip()!=VERSION:raise ValueError('Root release version differs')
    numeric=build/'generated/release111-vehicle-numeric-policy.json'
    if 'aarch64-linux-gnu' in (build/'CMakeCache.txt').read_text():
        policy=json.loads(numeric.read_text());ninja=(build/'build.ninja').read_text()
        for target in ('DKRCustomVehicles','DKRVehicleOriginalChecks','DKRVehicleOriginalSanitizerChecks'):
            blocks=[s for s in ninja.split('\n\n') if s.startswith('build CMakeFiles/'+target+'.dir/') and
                ('vehicle_package.cpp.o:' in s if target=='DKRCustomVehicles' else 'package_checks.cpp.o:' in s)]
            if len(blocks)!=1 or '-ffp-contract=off' not in blocks[0]:raise ValueError('ARM64 vehicle numeric compiler binding missing: '+target)
        result['vehicle_numeric_policy']=policy
    if binary:
        data=binary.read_bytes()
        for old in ('1.0.10-vehicle-playtest.','1.0.10-render-playtest.','1.0.10-memory-playtest.'):
            if old.encode() in data or old.encode('utf-16le') in data:raise ValueError('Old playtest label linked')
    result['label']='v'+VERSION+' Release Build'
    return result
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('build','binary','baseline','accepted','output'):p.add_argument('--'+n,type=Path,required=n!='binary')
    a=p.parse_args();r=inspect(a.build,a.binary,a.baseline,a.accepted)
    a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(json.dumps(r,indent=2)+'\n')
    print('Stable version and complete memory/render/vehicle/online source chain verified.')
