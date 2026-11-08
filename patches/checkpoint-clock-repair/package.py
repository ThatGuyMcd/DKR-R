"""Allowlisted checkpoint repair Windows ZIP and Linux AppImage."""
from pathlib import Path
import argparse,importlib.util,json,sys
sys.dont_write_bytecode=True
AUTH=Path(__file__).resolve().parent
ROOT=AUTH.parents[1]
VERSION='1.0.9-spectator-playtest.11'
def load(name,path):
 s=importlib.util.spec_from_file_location(name,path);m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m
def main(a):
 if a.output.exists():raise ValueError('Choose a fresh package directory')
 binary=a.compiled/('libexec/dkr-r/DKR-R-Runtime.exe' if a.platform=='windows' else 'DKR-R')
 checked=load('checkpoint_package_audit',AUTH/'audit.py').inspect(a.stage,binary,a.baseline,a.previous)
 base=load('checkpoint_package_allowlist',ROOT/'patches/release-prep/v1.0.7/package_release.py');base.VERSION=VERSION
 base.notes=lambda root,label:base.copy(AUTH/'PLAYTEST.txt',root/'README.txt')
 a.output.mkdir(parents=True)
 root,artifact,files=getattr(base,a.platform)(a)
 receipt={'schema':11,'version':VERSION,'platform':a.platform,'arch':a.arch,
  'artifact':{'name':artifact.name,'sha256':base.sha(artifact),'bytes':artifact.stat().st_size},
  'files':files,'audit':checked,'published':False,'automatic_updates_signed':False,'validation':checked['validation']}
 (a.output/'BUILD-RECEIPT.json').write_text(json.dumps(receipt,indent=2)+'\n')
 (a.output/'SHA256SUMS.txt').write_text(base.sha(artifact)+'  '+artifact.name+'\n')
 base.copy(AUTH/'PLAYTEST.txt',a.output/'PLAYTEST.txt')
 print('Checkpoint repair packaged without target execution:',artifact)
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('platform',choices=('windows','linux'))
 for name in ('compiled','template','output','stage','trophy','baseline','previous'):p.add_argument('--'+name,type=Path,required=True)
 p.add_argument('--arch',choices=('x86_64','aarch64'),default='x86_64');p.add_argument('--runtime-image',type=Path)
 main(p.parse_args())
