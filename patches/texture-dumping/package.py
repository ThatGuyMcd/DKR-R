"""Allowlisted Windows/Linux texture-authoring playtests; no target execution."""
from pathlib import Path
import argparse
import importlib.util
import json
import sys
sys.dont_write_bytecode=True
AUTH=Path(__file__).resolve().parent
ROOT=AUTH.parents[1]
VERSION='1.0.9-texture-playtest.3'
def load(name,path):
    spec=importlib.util.spec_from_file_location(name,path)
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module);return module
def main(a):
    if a.output.exists():raise ValueError('Choose a NEW package destination')
    binary=a.compiled/('libexec/dkr-r/DKR-R-Runtime.exe' if a.platform=='windows' else 'DKR-R')
    report=load('texture_vehicle_source_inspection',ROOT/'patches/vehicle-seating/inspect_sources.py').inspect(a.stage,a.repair_stage,a.cars,binary)
    dump_report=load('texture_dump_static',AUTH/'inspect_dump.py').inspect(a.dump_stage,binary)
    data=binary.read_bytes()
    for marker in (b'[texture-dump] presentation-only PNG capture enabled',b'TEXTURE DUMPING',b'CAPTURE NEXT GAME',b'Texture-Starter.zip',b'dkr-r-texture-dump'):
        if marker not in data:raise ValueError('Texture dump implementation not linked: '+repr(marker))
    swaps=json.loads((a.dump_stage/'dump-swaps.json').read_text())
    baseline=load('texture_dump_package_allowlist',ROOT/'patches/release-prep/v1.0.7/package_release.py');baseline.VERSION=VERSION
    for row in swaps:
        if baseline.sha(Path(row['path']))!=row['input_sha256'] or baseline.sha(Path(row['new']))!=row['output_sha256']:raise ValueError('Texture dump input/stage changed')
    def notes(root,label):
        baseline.copy(AUTH/'TEXTURE-DUMPING.txt',root/'README.txt')
    baseline.notes=notes
    a.output.mkdir(parents=True)
    root,artifact,files=getattr(baseline,a.platform)(a)
    receipt={'schema':1,'version':VERSION,'platform':a.platform,'arch':a.arch,
        'artifact':{'name':artifact.name,'bytes':artifact.stat().st_size,'sha256':baseline.sha(artifact)},
        'files':files,'accepted_vehicle_inspection':report,'texture_pipeline':swaps,'texture_inspection':dump_report,
        'validation':'Compilation/link and static source/archive inspection only. No game, mod or test execution. Owner gameplay acceptance pending.',
        'published':False,'automatic_updates_signed':False}
    (a.output/'BUILD-RECEIPT.json').write_text(json.dumps(receipt,indent=2)+'\n')
    (a.output/'SHA256SUMS.txt').write_text(baseline.sha(artifact)+'  '+artifact.name+'\n')
    baseline.copy(AUTH/'TEXTURE-DUMPING.txt',a.output/'TEXTURE-DUMPING.txt')
    print('Texture authoring playtest packaged without target execution:',artifact)
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('platform',choices=('windows','linux'))
    for name in ('compiled','template','output','stage','repair-stage','cars','trophy','dump-stage'):p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--arch',choices=('x86_64','aarch64'),default='x86_64');p.add_argument('--runtime-image',type=Path)
    main(p.parse_args())
