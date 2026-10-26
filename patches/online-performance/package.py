"""Allowlisted Windows ZIP and Linux AppImage playtests; no game execution."""
from pathlib import Path
import argparse,importlib.util,inspect,json,sys
sys.dont_write_bytecode=True
import audit,pipeline
AUTH=Path(__file__).resolve().parent;ROOT=AUTH.parents[1]
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('platform',choices=('windows','linux'))
    for name in ('compiled','template','output','build','trophy','baseline','accepted'):p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--arch',default='x86_64',choices=('x86_64',));p.add_argument('--runtime-image',type=Path);a=p.parse_args()
    if a.output.exists():raise ValueError('Choose a fresh package folder')
    binary=a.compiled/('libexec/dkr-r/DKR-R-Runtime.exe' if a.platform=='windows' else 'DKR-R')
    checked=audit.inspect(a.build,binary,a.baseline,a.accepted)
    s=importlib.util.spec_from_file_location('online_allowlist',ROOT/'patches/release-prep/v1.0.7/package_release.py')
    m=importlib.util.module_from_spec(s);s.loader.exec_module(m);m.VERSION=pipeline.VERSION
    source=inspect.getsource(m.features);old="b'workload-atomic promotion'"
    if source.count(old)!=1:raise ValueError('Accepted feature guard changed')
    exec(compile(source.replace(old,"b'immutable workload families'",1),str(AUTH/'package.py'),'exec'),m.__dict__)
    def notes(root,label):
        for name in ('README.txt','RELEASE-NOTES.txt'):m.copy(AUTH/'PLAYTEST.txt',root/name)
        m.copy(AUTH/'PIPELINE.txt',root/'ONLINE-PERFORMANCE.txt')
        m.copy(ROOT/'patches/character-library-portraits/PIPELINE.txt',root/'CHARACTER-PORTRAITS.txt')
    m.notes=notes;a.output.mkdir(parents=True)
    root,artifact,files=getattr(m,a.platform)(a)
    receipt={'schema':1,'version':pipeline.VERSION,'platform':a.platform,'arch':a.arch,
        'artifact':{'name':artifact.name,'sha256':m.sha(artifact),'bytes':artifact.stat().st_size},'files':files,
        'audit':checked,'published':False,'automatic_updates_signed':False}
    (a.output/'BUILD-RECEIPT.json').write_text(json.dumps(receipt,indent=2)+'\n')
    (a.output/'SHA256SUMS.txt').write_text(m.sha(artifact)+'  '+artifact.name+'\n')
    print('Online performance playtest packaged:',artifact)
