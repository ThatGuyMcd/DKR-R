"""Allowlisted repair delivery. Does not execute any game or packaged binary."""
from pathlib import Path
import argparse
import importlib.util
import json
import sys

sys.dont_write_bytecode=True
AUTH=Path(__file__).resolve().parent
ROOT=AUTH.parents[1]
VERSION='1.0.9-mod-repair.1'


def load(name,path):
    spec=importlib.util.spec_from_file_location(name,path)
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
    return module


def main(args):
    if args.output.exists():raise ValueError('Choose a NEW package destination')
    binary=args.compiled/('libexec/dkr-r/DKR-R-Runtime.exe' if args.platform=='windows' else 'DKR-R')
    report=load('repair_static_inspection',AUTH/'inspect_sources.py').inspect(args.stage,binary)
    baseline=load('repair_allowlisted_packaging',ROOT/'patches/release-prep/v1.0.7/package_release.py')
    baseline.VERSION=VERSION
    baseline.notes=lambda root,label:baseline.copy(AUTH/'PLAYTEST.txt',root/'README.txt')
    args.output.mkdir(parents=True)
    root,artifact,files=getattr(baseline,args.platform)(args)
    # Receipt and detailed authoring notes stay alongside, not in the game tree.
    receipt={'schema':1,'version':VERSION,'platform':args.platform,'arch':args.arch,
        'artifact':{'name':artifact.name,'bytes':artifact.stat().st_size,'sha256':baseline.sha(artifact)},
        'files':files,'inspection':report,'rollback_build_ids':report['rollback_build_ids'],
        'validation':'Compilation/link and static source/package inspection only; owner gameplay acceptance pending.',
        'published':False,'automatic_updates_signed':False}
    (args.output/'BUILD-RECEIPT.json').write_text(json.dumps(receipt,indent=2)+'\n')
    (args.output/'SHA256SUMS.txt').write_text(baseline.sha(artifact)+'  '+artifact.name+'\n')
    baseline.copy(AUTH/'SDK-WORK-POLICY.txt',args.output/'SDK-WORK-POLICY.txt')
    print('Repair package completed without target execution:',artifact)


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('platform',choices=('windows','linux'))
    for name in ('compiled','template','output','stage','trophy'):parser.add_argument('--'+name,type=Path,required=True)
    parser.add_argument('--arch',choices=('x86_64','aarch64'),default='x86_64')
    parser.add_argument('--runtime-image',type=Path)
    main(parser.parse_args())
