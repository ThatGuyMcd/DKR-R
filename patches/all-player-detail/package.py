"""Allowlisted detail playtests. No game, helper, mod or AppImage execution."""
from pathlib import Path
import argparse
import importlib.util
import json
import sys
sys.dont_write_bytecode=True
AUTH=Path(__file__).resolve().parent
ROOT=AUTH.parents[1]
VERSION='1.0.9-detail-playtest.1'
def load(name,path):
 spec=importlib.util.spec_from_file_location(name,path);module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module);return module
def main(a):
 if a.output.exists():raise ValueError('Choose a NEW package destination')
 binary=a.compiled/('libexec/dkr-r/DKR-R-Runtime.exe' if a.platform=='windows' else 'DKR-R')
 audit=load('all_player_detail_audit',AUTH/'audit.py').inspect(a.stage,binary)
 base=load('detail_package_allowlist',ROOT/'patches/release-prep/v1.0.7/package_release.py');base.VERSION=VERSION
 base.notes=lambda root,label:base.copy(AUTH/'PLAYTEST.txt',root/'README.txt')
 a.output.mkdir(parents=True)
 root,artifact,files=getattr(base,a.platform)(a)
 receipt={'schema':1,'version':VERSION,'platform':a.platform,'arch':a.arch,
  'artifact':{'name':artifact.name,'bytes':artifact.stat().st_size,'sha256':base.sha(artifact)},
  'files':files,'detail_audit':audit,'validation':audit['validation'],'published':False,'automatic_updates_signed':False}
 (a.output/'BUILD-RECEIPT.json').write_text(json.dumps(receipt,indent=2)+'\n',encoding='utf-8')
 (a.output/'SHA256SUMS.txt').write_text(base.sha(artifact)+'  '+artifact.name+'\n',encoding='utf-8')
 base.copy(AUTH/'PLAYTEST.txt',a.output/'PLAYTEST.txt')
 print('All-player detail packaged without target execution:',artifact)
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('platform',choices=('windows','linux'))
 for name in ('compiled','template','output','stage','trophy'):p.add_argument('--'+name,type=Path,required=True)
 p.add_argument('--arch',choices=('x86_64','aarch64'),default='x86_64');p.add_argument('--runtime-image',type=Path);main(p.parse_args())
