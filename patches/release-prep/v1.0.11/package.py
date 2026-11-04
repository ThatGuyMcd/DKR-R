"""Allowlisted stable desktop packages; no game or AppImage execution."""
from pathlib import Path
import argparse,importlib.util,json,sys
sys.dont_write_bytecode=True
import audit
AUTH=Path(__file__).resolve().parent;ROOT=AUTH.parents[2];VERSION=(ROOT/'VERSION').read_text().strip()
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('platform',choices=('windows','linux'))
    for n in ('compiled','template','output','build','trophy','baseline','accepted'):p.add_argument('--'+n,type=Path,required=True)
    p.add_argument('--arch',choices=('x86_64','aarch64'),default='x86_64');p.add_argument('--runtime-image',type=Path);a=p.parse_args()
    if a.output.exists():raise ValueError('Choose a fresh release package directory')
    if a.platform=='windows' and a.arch!='x86_64':raise ValueError('Windows release is x64')
    binary=a.compiled/('libexec/dkr-r/DKR-R-Runtime.exe' if a.platform=='windows' else 'DKR-R')
    checked=audit.inspect(a.build,binary,a.baseline,a.accepted)
    s=importlib.util.spec_from_file_location('release111_allowlist',ROOT/'patches/release-prep/v1.0.7/package_release.py')
    base=importlib.util.module_from_spec(s);s.loader.exec_module(base);base.VERSION=VERSION
    base.WATER_FEATURE_MARKER=b'immutable workload families'
    def notes(root,label):
        base.copy(ROOT/'packaging/RELEASE-README.md',root/'README.txt')
        base.copy(ROOT/('docs/PUBLIC-'+VERSION+'-NOTES.txt'),root/'RELEASE-NOTES.txt')
        legal=root/('ThirdPartyLicenses' if a.platform=='windows' else 'usr/share/doc/dkr-port')
        base.copy(ROOT/'runtime-recomp/assets/localisation/OFL-NotoSans.txt',legal/'NotoSans-LICENSE.txt')
    base.notes=notes;a.output.mkdir(parents=True)
    root,artifact,files=getattr(base,a.platform)(a)
    receipt={'schema':1,'version':VERSION,'label':'v'+VERSION+' Release Build','platform':a.platform,'arch':a.arch,
        'artifact':{'name':artifact.name,'sha256':base.sha(artifact),'bytes':artifact.stat().st_size},
        'files':files,'audit':checked,'published':False,'automatic_updates_signed':False}
    (a.output/'BUILD-RECEIPT.json').write_text(json.dumps(receipt,indent=2)+'\n')
    print('Release packaged:',artifact)
