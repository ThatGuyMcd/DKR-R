"""Allowlisted stable 1.0.9 client packages; no target execution."""
from pathlib import Path
import argparse,importlib.util,json,sys
sys.dont_write_bytecode=True
AUTH=Path(__file__).resolve().parent
ROOT=AUTH.parents[2]

def load(name,path):
    spec=importlib.util.spec_from_file_location(name,path);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);return m

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('platform',choices=('windows','linux'))
    for name in ('compiled','template','output','build','trophy','baseline','accepted'):p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--arch',choices=('x86_64','aarch64'),default='x86_64');p.add_argument('--runtime-image',type=Path)
    a=p.parse_args()
    if a.output.exists():raise ValueError('Choose a fresh stable package directory')
    binary=a.compiled/('libexec/dkr-r/DKR-R-Runtime.exe' if a.platform=='windows' else 'DKR-R')
    checked=load('stable109_audit',AUTH/'audit.py').inspect(a.build,binary,a.baseline,a.accepted)
    base=load('stable109_allowlist',ROOT/'patches/release-prep/v1.0.7/package_release.py');base.VERSION='1.0.9'
    def notes(root,label):
        base.copy(ROOT/'packaging/RELEASE-README.md',root/'README.txt')
        base.copy(AUTH/'RELEASE-NOTES.txt',root/'RELEASE-NOTES.txt')
    base.notes=notes;a.output.mkdir(parents=True)
    root,artifact,files=getattr(base,a.platform)(a)
    receipt={'schema':1,'version':'1.0.9','label':'v1.0.9 Release Build','platform':a.platform,'arch':a.arch,
        'artifact':{'name':artifact.name,'sha256':base.sha(artifact),'bytes':artifact.stat().st_size},
        'files':files,'audit':checked,'published':False,'automatic_updates_signed':False,'validation':checked['validation']}
    (a.output/'BUILD-RECEIPT.json').write_text(json.dumps(receipt,indent=2)+'\n')
    (a.output/'SHA256SUMS.txt').write_text(base.sha(artifact)+'  '+artifact.name+'\n')
    print('v1.0.9 Release Build packaged without game execution:',artifact)
