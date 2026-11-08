"""Inspect API 1 member bytes after compilation. Never executes a mod."""
from pathlib import Path
import argparse
import hashlib
import json
import zipfile

p=argparse.ArgumentParser(description=__doc__);p.add_argument('task',type=Path);p.add_argument('--baseline',type=Path,required=True);a=p.parse_args()
records=[]
for folder,name in [('api1-trophy-compile','dkr_custom_trophies.nrm'),('api1-difficulty-compile','dkr_ai_boss_difficulty.nrm')]:
    new=a.task/folder/name;old=a.baseline/folder/name
    with zipfile.ZipFile(new) as left,zipfile.ZipFile(old) as right:
        names=sorted(left.namelist())
        if names!=sorted(right.namelist()) or any(left.read(name)!=right.read(name) for name in names):raise ValueError('API 1 member changed: '+name)
        records.append({'file':name,'sha256':hashlib.sha256(new.read_bytes()).hexdigest(),'members':names})
output=a.task/'API1-COMPATIBILITY.json'
if output.exists():raise ValueError('Use a new inspection output')
output.write_text(json.dumps({'same_zip_member_contents':True,'baseline':a.baseline.as_posix(),'files':records,'execution':'none'},indent=2)+'\n')
print('API 1 trophy and difficulty: byte-identical compiled ZIP member contents. No execution.')
