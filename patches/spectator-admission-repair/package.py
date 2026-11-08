"""Package matching spectator-admission desktop test builds."""
from pathlib import Path
import argparse, importlib.util, json, sys
sys.dont_write_bytecode=True
AUTH=Path(__file__).resolve().parent;ROOT=AUTH.parents[1]
VERSION='1.0.9-stability-playtest.4'
def load(name,path):
    s=importlib.util.spec_from_file_location(name,path);m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('platform',choices=('windows','linux'))
    for name in ('compiled','template','output','build','trophy','baseline','accepted'):p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--runtime-image',type=Path);p.add_argument('--arch',choices=('x86_64',),default='x86_64');a=p.parse_args()
    if a.output.exists():raise ValueError('Choose a fresh package directory')
    binary=a.compiled/('libexec/dkr-r/DKR-R-Runtime.exe' if a.platform=='windows' else 'DKR-R')
    checked=load('observer_package_audit',AUTH/'audit.py').inspect(a.build,binary,a.baseline,a.accepted)
    base=load('observer_package_allowlist',ROOT/'patches/release-prep/v1.0.7/package_release.py');base.VERSION=VERSION
    def notes(root,label):
        base.copy(AUTH/'PLAYTEST.txt',root/'README.txt');base.copy(AUTH/'PLAYTEST.txt',root/'RELEASE-NOTES.txt')
    base.notes=notes;a.output.mkdir(parents=True)
    root,artifact,files=getattr(base,a.platform)(a)
    result={'schema':1,'version':VERSION,'platform':a.platform,'arch':a.arch,'artifact':{'name':artifact.name,'sha256':base.sha(artifact),'bytes':artifact.stat().st_size},
        'files':files,'audit':checked,'published':False,'automatic_updates_signed':False}
    (a.output/'BUILD-RECEIPT.json').write_text(json.dumps(result,indent=2)+'\n')
    (a.output/'SHA256SUMS.txt').write_text(base.sha(artifact)+'  '+artifact.name+'\n')
    print('Spectator admission playtest packaged:',artifact)
