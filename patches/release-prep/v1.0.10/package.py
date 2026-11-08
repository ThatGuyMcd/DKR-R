"""Allowlisted v1.0.10 Windows and Linux release packages."""
from pathlib import Path
import argparse,importlib.util,json,sys
sys.dont_write_bytecode=True
AUTH=Path(__file__).resolve().parent;ROOT=AUTH.parents[2];VERSION='1.0.10'
def load(name,path):
    s=importlib.util.spec_from_file_location(name,path);m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('platform',choices=('windows','linux'))
    for n in ('compiled','template','output','build','trophy','baseline','accepted'):p.add_argument('--'+n,type=Path,required=True)
    p.add_argument('--arch',choices=('x86_64','aarch64'),default='x86_64');p.add_argument('--runtime-image',type=Path);a=p.parse_args()
    if a.output.exists():raise ValueError('Choose a fresh release package directory')
    if a.platform=='windows' and a.arch!='x86_64':raise ValueError('Windows release is x64')
    binary=a.compiled/('libexec/dkr-r/DKR-R-Runtime.exe' if a.platform=='windows' else 'DKR-R')
    checked=load('release110_audit',AUTH/'audit.py').inspect(a.build,binary,a.baseline,a.accepted)
    base=load('release110_allowlist',ROOT/'patches/release-prep/v1.0.7/package_release.py');base.VERSION=VERSION
    def notes(root,label):
        base.copy(ROOT/'packaging/RELEASE-README.md',root/'README.txt')
        base.copy(AUTH/'RELEASE-NOTES.txt',root/'RELEASE-NOTES.txt')
    base.notes=notes;a.output.mkdir(parents=True)
    root,artifact,files=getattr(base,a.platform)(a)
    r={'schema':1,'version':VERSION,'label':'v1.0.10 Release Build','platform':a.platform,'arch':a.arch,
       'artifact':{'name':artifact.name,'sha256':base.sha(artifact),'bytes':artifact.stat().st_size},
       'files':files,'audit':checked,'published':False,'automatic_updates_signed':False}
    (a.output/'BUILD-RECEIPT.json').write_text(json.dumps(r,indent=2)+'\n')
    print('Release packaged:',artifact)
