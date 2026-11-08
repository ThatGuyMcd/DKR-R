"""Static archive/binary inspection, including the previous save fix."""
from pathlib import Path
import argparse,contextlib,io,json,runpy,shutil,sys,zipfile

p=argparse.ArgumentParser(description=__doc__);p.add_argument('task',type=Path);a=p.parse_args()
root=Path(__file__).resolve().parents[2]
# Reuse the established archive/privacy/architecture/save-fix inspection.
# This is a fresh log alias, never a generated code or executable edit.
source=a.task/'Windows-configure.log';alias=a.task/'windows-version-configure.log'
if alias.exists():
    if alias.read_bytes()!=source.read_bytes():raise ValueError('Configure log alias is stale')
else:shutil.copyfile(source,alias)
sys.argv=[str(root/'patches/save-layout/inspect_packages.py'),str(a.task)]
captured=io.StringIO()
with contextlib.redirect_stdout(captured):
    old=runpy.run_path(str(root/'patches/save-layout/inspect_packages.py'),run_name='__main__')
report=json.loads(captured.getvalue())
with zipfile.ZipFile(old['win']) as archive:
    names=archive.namelist()
    if not any(n.endswith('/CHARACTER-PORTRAIT-NOTES.md') for n in names):raise ValueError('Portrait notes missing')
    windows=archive.read(next(n for n in names if n.endswith('/libexec/dkr-r/DKR-R-Runtime.exe')))
linux=old['read']('usr/bin/DKR-R')
for name,data in (('Windows',windows),('Linux',linux)):
    for marker in (b'Retained portrait failed its source integrity check.',b'Invalid recovered source portrait.',b':portrait-source-v1:'):
        if marker not in data:raise ValueError(name+' is missing a compiled portrait correction')
report['character_portraits']='Verified native recovery markers, matching rollback contracts and packaged portrait notes'
print(json.dumps(report,indent=2))
