"""Inspect APK metadata, signer, ZIP and native features; never installs/runs it."""
from pathlib import Path
import argparse
import hashlib
import importlib.util
import json
import os
import re
import struct
import subprocess
import zipfile

AUTH=Path(__file__).resolve().parent
p=argparse.ArgumentParser(description=__doc__)
for name in ('apk','published','beta','tools','output'):p.add_argument('--'+name,type=Path,required=True)
a=p.parse_args()
def command(tool,*args):
    path=a.tools/tool
    argv=[str(path),*map(str,args)]
    if path.suffix=='.bat':argv=[os.environ.get('COMSPEC','C:/Windows/System32/cmd.exe'),'/c',*argv]
    return subprocess.check_output(argv,text=True,stderr=subprocess.STDOUT)
def cert(path):
    text=command('apksigner.bat','verify','--print-certs',path)
    match=re.search(r'certificate SHA-256 digest: ([a-f0-9]{64})',text)
    if not match:raise ValueError('Missing verified signer')
    return match[1]
signers={name:cert(getattr(a,name)) for name in ('apk','published','beta')}
if len(set(signers.values()))!=1:raise ValueError('APK update certificate changed')
badging=command('aapt.exe','dump','badging',a.apk)
expected="package: name='io.github.thatguymcd.dkrr.preview' versionCode='1070018' versionName='1.0.7'"
if expected not in badging or "sdkVersion:'28'" not in badging or "native-code: 'arm64-v8a'" not in badging:raise ValueError('Wrong release APK metadata')
spec=importlib.util.spec_from_file_location('release_checks',AUTH/'package_release.py')
checks=importlib.util.module_from_spec(spec);spec.loader.exec_module(checks)
files={}
with zipfile.ZipFile(a.apk) as archive:
    if archive.testzip():raise ValueError('APK ZIP CRC mismatch')
    for entry in archive.infolist():
        if entry.is_dir():continue
        name=entry.filename
        if name.startswith('/') or '..' in Path(name).parts or '\\' in name:raise ValueError('Unsafe APK path')
        if name.endswith(('.pdf','.html','.pdb','.z64','.n64','.v64','.eep','.mpk')) or '/Custom-Magic-Codes/' in name:raise ValueError('Guide/authoring/private data in APK')
        raw=archive.read(entry);files[name]={'bytes':len(raw),'sha256':hashlib.sha256(raw).hexdigest()}
        if name.startswith('lib/'):
            if not name.startswith('lib/arm64-v8a/') or raw[:6]!=b'\x7fELF\x02\x01' or struct.unpack_from('<H',raw,18)[0]!=183:raise ValueError('Foreign native library')
        if name=='lib/arm64-v8a/libDKR-R.so':
            checks.features(raw)
            if b'1.0.7' not in raw or b'1.0.7+beta.10' in raw:raise ValueError('Stale native release')
    if 'lib/arm64-v8a/libDKR-R.so' not in files:raise ValueError('Missing native runtime')
report={'schema':1,'version':'1.0.7','version_code':1070018,'application_id':'io.github.thatguymcd.dkrr.preview',
    'signer_sha256':signers['apk'],'matches_published_1_0_6_and_beta_7':True,'min_sdk':28,'arch':'arm64-v8a',
    'artifact':{'name':a.apk.name,'bytes':a.apk.stat().st_size,'sha256':hashlib.sha256(a.apk.read_bytes()).hexdigest()},
    'files':files,'execution':'APK/ELF/ZIP/signature metadata inspection only. No installation or application execution.'}
a.output.write_text(json.dumps(report,indent=2)+'\n')
print('APK metadata, CRC, native features and original published signing identity inspected.')
