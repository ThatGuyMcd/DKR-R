"""Reconstruct the accepted source layers without historical stage directories.

Only first-party source deltas enter Git. Prepared guest/draw payloads stay
private, explicit, content-checked build inputs. No target execution.
"""
from pathlib import Path
import argparse
import difflib
import hashlib
import importlib.util
import json
import re
import shutil
import sys
sys.dont_write_bytecode=True
AUTH=Path(__file__).resolve().parent
ROOT=AUTH.parents[2]
DATA=AUTH/'source-capsule-r2'
PREPARED={
    'CAMERA_KERNEL_V77':ROOT/'build/camera-plus-20261017-r13/kernel-v77',
    'CAMERA_KERNEL_V80':ROOT/'build/camera-plus-20261017-r13/kernel-v80',
    'SHADOW_KERNEL_V77':ROOT/'build/camera-shadow-beta6-20261017-r3/stage/kernel-v77',
    'SHADOW_KERNEL_V80':ROOT/'build/camera-shadow-beta6-20261017-r3/stage/kernel-v80',
}

def sha(raw):return hashlib.sha256(raw).hexdigest()
def canonical(path):return path.read_bytes().replace(b'\r\n',b'\n')
def load_recipe():
    spec=importlib.util.spec_from_file_location('release_recipe',AUTH/'release.py')
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module);return module

def relative_base(name):
    for prefix in ('native/',''):
        suffix=name[len(prefix):] if name.startswith(prefix) else ''
        if suffix.startswith(('src/','tests/','include/')):
            path=ROOT/'runtime-recomp'/suffix
            if path.is_file():return path
    return None

def export():
    if DATA.exists():raise ValueError('Never replace a frozen capsule')
    DATA.mkdir();roles={};objects={}
    recipe=load_recipe();stages={k:ROOT/'build'/v for k,v in recipe.STAGES.items()}
    stages['SDK2_BASE']=ROOT/'build/sdk2-beta7-20261018-r18/stage'
    for role,directory in stages.items():
        entries={}
        for path in sorted(directory.rglob('*')):
            if not path.is_file():continue
            name=path.relative_to(directory).as_posix()
            if '__pycache__' in path.parts or path.suffix=='.pyc':continue
            if name.startswith(('kernel-v77/','kernel-v80/')):continue
            if role in ('SDK2','SDK2_BASE') and not (name.startswith(('native/','modding/reference-')) or name=='sdk2-engine-identity.txt'):continue
            if path.name.endswith('receipt.json'):continue
            if path.suffix.lower() in ('.z64','.n64','.v64','.eep','.mpk','.dkr-probe','.o2r','.otr'):
                raise ValueError('Private game data in native layer')
            raw=path.read_bytes()
            # All release source text is canonical LF; binary inputs are never
            # silently recoded or treated as editable source.
            text=raw.decode('utf-8')
            base=relative_base(name)
            record={'sha256':sha(raw)}
            if base:
                original=canonical(base)
                record.update(base=base.relative_to(ROOT).as_posix(),base_sha256=sha(original))
                if original==raw:entries[name]=record;continue
                patch=''.join(difflib.unified_diff(original.decode('utf-8').splitlines(True),text.splitlines(True),fromfile='before',tofile='after'))
                object_raw=patch.encode();record['patch']=sha(object_raw)+'.patch'
                objects[record['patch']]=object_raw
            else:
                record['object']=sha(raw)+'.txt';objects[record['object']]=raw
            entries[name]=record
        # Existing overlay existence checks retain their purpose. Integrity is
        # supplied by the one release manifest instead of stale absolute paths.
        for receipt in directory.glob('*receipt.json'):
            raw=b'{"schema":1,"integrity":"release-source-capsule"}\n'
            name=sha(raw)+'.txt';objects[name]=raw
            entries[receipt.name]={'sha256':sha(raw),'object':name}
        roles[role]=entries
    prepared={key:{p.relative_to(directory).as_posix():sha(p.read_bytes()) for p in directory.rglob('*') if p.is_file() and '__pycache__' not in p.parts} for key,directory in PREPARED.items()}
    overlays={}
    chain=('camera-plus','launcher-polish','camera-shadow','sdk2','mod-store','shared-saves','vanilla-water','viewport-flow','title-text','credits-viewport','presentation-polish','performance-surgical','liquid-streaming')
    for feature in chain:
        path=ROOT/'patches'/feature/'NativeOverlay.cmake'
        text=path.read_text(encoding='utf-8')
        text,count=re.subn(r'execute_process\(.*?RESULT_VARIABLE\s+(\w+)\)',
            lambda m:'execute_process(COMMAND "${Python3_EXECUTABLE}" -B "${DKRPORT_ROOT}/patches/release-prep/v1.0.7/capsule.py" verify --output "${DKR_RELEASE_CAPSULE}" RESULT_VARIABLE '+m[1]+')',text,flags=re.S)
        if count<1:raise ValueError('Overlay verification anchor missing: '+feature)
        text=text.replace('${DKRPORT_ROOT}/patches/camera-plus/SceneKernel.cmake','${DKR_RELEASE_CAPSULE}/overlays/camera-plus/SceneKernel.cmake')
        overlays[feature+'/NativeOverlay.cmake']=text
    text=(ROOT/'patches/camera-plus/SceneKernel.cmake').read_text()
    text=re.sub(r'execute_process\(.*?RESULT_VARIABLE\s+(\w+)\)',lambda m:'execute_process(COMMAND "${Python3_EXECUTABLE}" -B "${DKRPORT_ROOT}/patches/release-prep/v1.0.7/capsule.py" verify --output "${DKR_RELEASE_CAPSULE}" RESULT_VARIABLE '+m[1]+')',text,flags=re.S)
    text=text.replace('${CMAKE_CURRENT_FUNCTION_LIST_DIR}/native','${DKRPORT_ROOT}/patches/camera-plus/native')
    overlays['camera-plus/SceneKernel.cmake']=text
    for name,raw in objects.items():(DATA/name).write_bytes(raw)
    manifest={'schema':1,'version':'1.0.7','roles':roles,'prepared':prepared,'overlays':overlays,
        'policy':'Accepted first-party source bytes; prepared guest/draw inputs are hash-only external requirements. Checked-out dependencies/generated guest sources remain read-only.'}
    (DATA/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    print('Exported source-only release deltas:',len(objects),'objects;',sum(len(x) for x in roles.values()),'layer files')

def apply_delta(original,patch):
    old=original.decode('utf-8').splitlines(True);lines=patch.decode('utf-8').splitlines(True)
    result=[];cursor=0;at=2
    while at<len(lines):
        match=re.match(r'@@ -(\d+)(?:,(\d+))? \+(\d+)(?:,(\d+))? @@',lines[at])
        if not match:raise ValueError('Invalid source delta hunk')
        start=int(match[1])-(int(match[1])!=0);result+=old[cursor:start];cursor=start;at+=1
        while at<len(lines) and not lines[at].startswith('@@ '):
            line=lines[at];at+=1
            if line.startswith((' ','-')):
                if cursor>=len(old) or old[cursor]!=line[1:]:raise ValueError('Release delta base changed')
                if line[0]==' ':result.append(old[cursor])
                cursor+=1
            elif line.startswith('+'):result.append(line[1:])
            else:raise ValueError('Unknown source delta record')
    return ''.join(result+old[cursor:]).encode('utf-8')

def stage(output,prepared):
    if output.exists():raise ValueError('Choose a NEW release capsule output')
    manifest=json.loads((DATA/'manifest.json').read_text())
    records={}
    def put(relative,raw,expected):
        if sha(raw)!=expected:raise ValueError('Reconstructed source differs: '+relative)
        path=output/relative;path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(raw);records[relative]=expected
    for role,entries in manifest['roles'].items():
        for name,entry in entries.items():
            if 'base' in entry:
                raw=canonical(ROOT/entry['base'])
                if sha(raw)!=entry['base_sha256']:raise ValueError('Checked source changed: '+entry['base'])
                if 'patch' in entry:raw=apply_delta(raw,(DATA/entry['patch']).read_bytes())
            else:raw=(DATA/entry['object']).read_bytes()
            put('roles/'+role+'/'+name,raw,entry['sha256'])
    for key,entries in manifest['prepared'].items():
        directory=prepared[key]
        role='CAMERA_SHADOW/kernel-v'+key[-2:] if key.startswith('SHADOW') else key
        prefix='roles/'+role if key.startswith('SHADOW') else 'prepared/'+role
        for name,expected in entries.items():put(prefix+'/'+name,(directory/name).read_bytes(),expected)
    for name,text in manifest['overlays'].items():put('overlays/'+name,text.encode(),sha(text.encode()))
    (output/'CAPSULE-RECEIPT.json').write_text(json.dumps({'manifest_sha256':sha((DATA/'manifest.json').read_bytes()),'files':records},indent=2)+'\n')
    verify(output)

def verify(output):
    receipt=json.loads((output/'CAPSULE-RECEIPT.json').read_text())
    raw=(DATA/'manifest.json').read_bytes()
    if receipt['manifest_sha256']!=sha(raw):raise ValueError('Release manifest changed')
    manifest=json.loads(raw)
    expected_files={'roles/'+role+'/'+name:entry['sha256'] for role,entries in manifest['roles'].items() for name,entry in entries.items()}
    for key,entries in manifest['prepared'].items():
        prefix='roles/CAMERA_SHADOW/kernel-v'+key[-2:] if key.startswith('SHADOW') else 'prepared/'+key
        expected_files.update({prefix+'/'+name:expected for name,expected in entries.items()})
    expected_files.update({'overlays/'+name:sha(text.encode()) for name,text in manifest['overlays'].items()})
    if receipt['files']!=expected_files:raise ValueError('Capsule membership differs from checked-in release manifest')
    actual=set()
    for path in output.rglob('*'):
        if path.is_symlink():raise ValueError('Release capsule cannot contain links')
        if path.is_file() and path.name!='CAPSULE-RECEIPT.json':actual.add(path.relative_to(output).as_posix())
    if actual!=set(expected_files):raise ValueError('Unexpected or missing release capsule file')
    for name,expected in expected_files.items():
        path=output/name
        if path.is_symlink() or sha(path.read_bytes())!=expected:raise ValueError('Release capsule changed: '+name)
    print('Release source capsule verified; no historical stage lookup or target execution.')

def complete(output,replacement):
    """Complete a never-delivered capsule without rewriting existing source.

    Only missing address-reference files may be supplied. Existing compiled
    source bytes must already match the final manifest exactly.
    """
    verify(replacement)
    receipt=json.loads((replacement/'CAPSULE-RECEIPT.json').read_text())
    for name,expected in receipt['files'].items():
        path=output/name
        if path.exists() and sha(path.read_bytes())!=expected:raise ValueError('Cannot complete different source: '+name)
    for name in receipt['files']:
        target=output/name
        if not target.exists():target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(replacement/name,target)
    shutil.copy2(replacement/'CAPSULE-RECEIPT.json',output/'CAPSULE-RECEIPT.json')
    verify(output)

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('action',choices=('export','stage','verify','complete'))
    p.add_argument('--output',type=Path)
    p.add_argument('--replacement',type=Path)
    for name in PREPARED:p.add_argument('--'+name.lower().replace('_','-'),type=Path)
    a=p.parse_args()
    if a.action=='export':export()
    elif a.action=='verify':verify(a.output)
    elif a.action=='complete':complete(a.output,a.replacement)
    else:stage(a.output,{name:getattr(a,name.lower()) for name in PREPARED})
if __name__=='__main__':main()
