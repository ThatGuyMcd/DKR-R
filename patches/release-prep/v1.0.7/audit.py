"""Read-only whole-tree release inventory; archive decisions are explicit."""
from pathlib import Path
import argparse
import collections
import hashlib
import json
import os
import re
import subprocess
import sys
sys.dont_write_bytecode=True
ROOT=Path(__file__).resolve().parents[3]
PROTECTED=('extern/','runtime-recomp/RecompiledFuncs/','runtime-recomp/RecompiledPatches/','.git/')
GENERATED={'.deps','build','build-logs','dist','artifacts','squashfs-root'}
LEGAL=re.compile(r'license|licence|copying|copyright|notice|third.party',re.I)
def sha(path):
    h=hashlib.sha256()
    with path.open('rb') as stream:
        for part in iter(lambda:stream.read(1024*1024),b''):h.update(part)
    return h.hexdigest()
def git(*args):return subprocess.check_output(['git','-C',str(ROOT),*args])
def scan(out,windows):
    if out.exists() or not out.resolve().is_relative_to(ROOT/'build'):raise ValueError('Choose a NEW report folder under build')
    out.mkdir(parents=True)
    tracked=set(git('ls-files','--cached','--full-name','-z').decode().split('\0'))
    dirty=git('status','--porcelain=v1','-uall').decode(errors='replace')
    (out/'git-status.txt').write_text(dirty)
    (out/'tracked-diff-since-1.0.6.txt').write_bytes(git('diff','--stat','Version1.0.6','--','.',':!extern',':!runtime-recomp/RecompiledFuncs',':!runtime-recomp/RecompiledPatches'))
    folders=collections.defaultdict(lambda:{'files':0,'bytes':0,'tracked':0,'links':0})
    docs=[];caches=set();errors=[];count=0;total=0
    with (out/'FULL-FILE-INVENTORY.jsonl').open('w',encoding='utf-8') as stream:
        pending=[ROOT]
        while pending:
            directory=pending.pop()
            try:
                entries=list(os.scandir(directory))
            except OSError as error:errors.append({'path':str(directory),'error':str(error)});continue
            for entry in entries:
                path=Path(entry.path);relative=path.relative_to(ROOT).as_posix()
                if path==out:continue
                try:
                    info=entry.stat(follow_symlinks=False)
                    link=entry.is_symlink() or bool(getattr(info,'st_file_attributes',0)&0x400)
                    directory_entry=entry.is_dir(follow_symlinks=False)
                except OSError as error:errors.append({'path':str(path),'error':str(error)});continue
                protected=any((relative+'/').startswith(prefix) for prefix in PROTECTED)
                if directory_entry and not link:
                    pending.append(path)
                    if path.name=='__pycache__' and not protected:caches.add(relative)
                    continue
                size=info.st_size if not directory_entry else 0
                generated=relative.split('/')[0] in GENERATED or relative.startswith(('packaging/android/app/build/','packaging/android/.gradle/','runtime-recomp/build/'))
                category='protected' if protected else 'generated' if generated else 'tracked-source' if relative in tracked else 'untracked-source-or-local'
                row={'path':relative,'bytes':size,'mtime_ns':info.st_mtime_ns,'link':link,'category':category,'tracked':relative in tracked}
                stream.write(json.dumps(row,ensure_ascii=False)+'\n');count+=1;total+=size
                pieces=relative.split('/')
                keys={'.'}
                for depth in (1,2):
                    if len(pieces)>depth:keys.add('/'.join(pieces[:depth]))
                for key in keys:
                    record=folders[key]
                    record['files']+=1;record['bytes']+=size;record['tracked']+=relative in tracked;record['links']+=link
                if not protected and category not in ('generated',) and not link and path.suffix.lower() in ('.md','.txt'):
                    text=path.read_text(encoding='utf-8-sig',errors='replace')
                    docs.append({'path':relative,'bytes':size,'sha256':sha(path),'tracked':relative in tracked,
                        'title':next((line.strip('# > ') for line in text.splitlines() if line.strip()),''),
                        'readme':'readme' in path.name.lower(),'legal':bool(LEGAL.search(path.name)),
                        'historical':bool(re.search(r'(?:BETA\d|PLAYTEST|20\d{6}|HISTORY|VALIDATION)',path.name,re.I)),
                        'headings':[line for line in text.splitlines() if line.startswith('#')][:30],
                        'version_mentions':sorted(set(re.findall(r'1\.0\.[567](?:[+ -]beta[. ]?\d+)?',text)))})
    candidates=[]
    # Only unquestionably disposable, untracked local residue is eligible NOW.
    # Frozen build/dependency/source stages remain on HOLD, not a delete list.
    selected=[p for p in ('extract.log','imgui.ini','squashfs-root') if (ROOT/p).exists()]
    # Frozen generated source receipts sometimes include copied bytecode. Even
    # harmless caches there stay on HOLD until that exact receipt is retired.
    selected+=sorted(p for p in caches if p.split('/')[0] not in GENERATED and
        not p.startswith(('packaging/android/','runtime-recomp/build/')) and
        not any((p+'/').startswith(x+'/') for x in selected))
    for relative in selected:
        path=ROOT/relative
        if any(name==relative or name.startswith(relative+'/') for name in tracked):continue
        if any((relative+'/').startswith(prefix) for prefix in PROTECTED):continue
        files=[];links=False
        items=path.rglob('*') if path.is_dir() else [path]
        for item in items:
            if item.is_symlink() or bool(getattr(item.lstat(),'st_file_attributes',0)&0x400):links=True;break
            if item.is_file():files.append({'path':item.relative_to(ROOT).as_posix(),'bytes':item.stat().st_size,'sha256':sha(item)})
        if links:continue # A reparse point is a HOLD, never traversed for moving.
        candidates.append({'source':str(path),'relative':relative,'reason':'Untracked local log/UI state or disposable generated extraction/Python bytecode; not a release source input.','files':files,'bytes':sum(x['bytes'] for x in files)})
    package_docs=[]
    if windows:
        for path in sorted(windows.rglob('*.md')):
            name=path.relative_to(windows).as_posix()
            action='KEEP_LEGAL' if LEGAL.search(path.name) else 'REMOVE_FROM_RELEASE_PACKAGE' if path.name in {'README.md','ONLINE-STABILITY.md','LEGACY-MOD-COMPATIBILITY-BETA7.md','MODERN-CAMERA-README.md'} else 'REVISE_AND_CONSOLIDATE'
            package_docs.append({'path':name,'bytes':path.stat().st_size,'sha256':sha(path),'action':action})
    summary={'date':'2026-10-20','scope':str(ROOT),'files':count,'bytes':total,'tracked_paths':len(tracked)-int('' in tracked),
        'folders':dict(sorted(folders.items())),'scan_errors':errors,'source_docs':len(docs),'source_readmes':sum(d['readme'] for d in docs),
        'protected_policy':'Inventory only. No .git, checked-out dependencies or generated guest functions may be moved/edited.',
        'disposable_archive_targets':len(candidates),'disposable_archive_bytes':sum(c['bytes'] for c in candidates),
        'generated_build_policy':'HOLD: retained build stages include active absolute-path inputs. Consolidate reproducible release pipeline before bulk archiving.'}
    (out/'TREE-SUMMARY.json').write_text(json.dumps(summary,indent=2)+'\n')
    (out/'DOCUMENTATION-INVENTORY.json').write_text(json.dumps(docs,indent=2,ensure_ascii=False)+'\n')
    (out/'WINDOWS-MARKDOWN.json').write_text(json.dumps(package_docs,indent=2)+'\n')
    manifest={'schema':1,'source_root':str(ROOT),'backup_root':'F:/DKR Dist dump/v1.0.7-prep-20261020','targets':candidates,'status':'LISTED_BEFORE_MOVE'}
    (out/'REMOVE-LIST.json').write_text(json.dumps(manifest,indent=2)+'\n')
    lines=['REMOVAL / BACKUP LIST - 20 October 2026','',f"Scope: {ROOT}",f"Backup: {manifest['backup_root']}",'','Marked for recoverable relocation NOW (not permanent deletion):']
    for target in candidates:
        lines+=['',target['source'],target['reason']]
        lines += ['  '+f['path']+' | '+str(f['bytes'])+' bytes | SHA256 '+f['sha256'] for f in target['files']]
    lines+=['','Windows package Markdown decisions (future RELEASE packaging only; current test ZIP and accepted packages are not modified):']
    lines += [f"{d['action']}: {d['path']} ({d['bytes']} bytes)" for d in package_docs]
    lines+=['','HOLD / NOT MARKED FOR REMOVAL: .git, extern, generated guest functions, all authored source/patches, current/frozen build inputs, release templates, symbols, saves, settings, installed mods, credentials, signing keys, licences, contributor records.','Full folder/file evidence: TREE-SUMMARY.json and FULL-FILE-INVENTORY.jsonl.','Nothing was moved by this audit command.']
    (out/'REMOVE-LIST.txt').write_text('\n'.join(lines)+'\n',encoding='utf-8')
    print(json.dumps({k:v for k,v in summary.items() if k!='folders'},indent=2))
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True);p.add_argument('--windows',type=Path)
    a=p.parse_args();scan(a.output,a.windows)
if __name__=='__main__':main()
