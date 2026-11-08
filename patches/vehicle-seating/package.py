"""Allowlisted car playtest delivery; no target/AppImage/helper execution."""
from pathlib import Path
import argparse
import importlib.util
import json
import sys

sys.dont_write_bytecode=True
AUTH=Path(__file__).resolve().parent
ROOT=AUTH.parents[1]
VERSION='1.0.9-vehicle-playtest.18'

def load(name,path):
    spec=importlib.util.spec_from_file_location(name,path)
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
    return module

def main(args):
    if args.output.exists():raise ValueError('Choose a NEW package destination')
    binary=args.compiled/('libexec/dkr-r/DKR-R-Runtime.exe' if args.platform=='windows' else 'DKR-R')
    report=load('vehicle_seating_static',AUTH/'inspect_sources.py').inspect(args.stage,args.repair_stage,args.cars,binary)
    baseline=load('vehicle_seating_allowlist',ROOT/'patches/release-prep/v1.0.7/package_release.py')
    baseline.VERSION=VERSION
    baseline.notes=lambda root,label:baseline.copy(AUTH/'PLAYTEST.txt',root/'README.txt')
    args.output.mkdir(parents=True)
    root,artifact,files=getattr(baseline,args.platform)(args)
    worker=root/('libexec/dkr-r/DKR-R-ModWorker.exe' if args.platform=='windows' else 'usr/libexec/dkr-r/DKR-R-ModWorker')
    worker_data=worker.read_bytes()
    # The sandbox worker validates/transports the wire manifest. The main
    # runtime owns the model/seating parser; it is correctly dead-stripped
    # from the worker. Its adapter-2 manifest source is checked above.
    for marker in (b'This build cannot safely prepare the required custom car.',):
        if marker not in worker_data:raise ValueError('Stale worker: '+repr(marker))
    receipt={'schema':1,'version':VERSION,'platform':args.platform,'arch':args.arch,
        'artifact':{'name':artifact.name,'bytes':artifact.stat().st_size,'sha256':baseline.sha(artifact)},
        'files':files,'inspection':report,'rollback_build_ids':report['rollback_build_ids'],
        'validation':'Compilation/link and static archive/source/package inspection only; owner gameplay acceptance pending.',
        'published':False,'automatic_updates_signed':False}
    (args.output/'BUILD-RECEIPT.json').write_text(json.dumps(receipt,indent=2)+'\n')
    (args.output/'SHA256SUMS.txt').write_text(baseline.sha(artifact)+'  '+artifact.name+'\n')
    baseline.copy(AUTH/'FORMAT.txt',args.output/'VEHICLE-FORMAT.txt')
    print('Fixed-reference cars packaged without target execution:',artifact)

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('platform',choices=('windows','linux'))
    for name in ('compiled','template','output','stage','repair-stage','cars','trophy'):p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--arch',choices=('x86_64','aarch64'),default='x86_64');p.add_argument('--runtime-image',type=Path)
    main(p.parse_args())
