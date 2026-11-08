"""Seal signed packages and generate exact manual GitHub upload instructions."""
from pathlib import Path
import argparse,hashlib,json,shutil,subprocess,sys
AUTH=Path(__file__).resolve().parent;ROOT=AUTH.parents[2];VERSION=(ROOT/'VERSION').read_text().strip()
def sha(p):
    with p.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('work','output'):p.add_argument('--'+n,type=Path,required=True)
    a=p.parse_args();subprocess.run([sys.executable,'-B',str(AUTH/'update_assets.py'),'verify','--packages',str(a.output),'--output',str(a.output)],check=True)
    manifest=json.loads((a.output/'DKR-R-update.json').read_text());head=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip()
    if manifest['commit']!=head:raise ValueError('Signed commit differs from source HEAD')
    for case in ('normal','corrupt-stage','interrupted','migrate-layout'):
        path=a.work/'windows'/('updater-'+case+'.log')
        if not path.is_file() or b'PASS:' not in path.read_bytes() or b'Traceback' in path.read_bytes():raise ValueError('Updater qualification failed: '+case)
        shutil.copy2(path,a.output/'Evidence'/path.name)
    (a.output/'UPLOAD.txt').write_text('DKR-R v'+VERSION+' - GitHub upload\n\nRelease title: DKR-R v'+VERSION+' Release Build\nTag: Version'+VERSION+'\nSource commit: '+head+'\n\n'
        'Create a draft release at that exact commit. Upload:\n'
        'DKR-R-'+VERSION+'-Windows-x64.zip\nDKR-R-'+VERSION+'-Linux-x86_64.AppImage\n'
        'DKR-R-'+VERSION+'-Linux-aarch64.AppImage\nDKR-R-'+VERSION+'-SteamDeck-x86_64.AppImage\n'
        'DKR-R-update.json\nDKR-R-update.sig\nSHA256SUMS.txt\n\n'
        'Paste RELEASE-NOTES.txt as the description. The changelog TXT can also\n'
        'be attached. Evidence is for review; do not upload keys or source backups.\n'
        'Finish the final gameplay/device/update checks, verify uploaded hashes,\n'
        'then publish as a stable release. Publishing makes auto updates available.\n'
        'Do not edit signed JSON or rename/recompress the application packages.\n')
    with (a.output/'VALIDATION.txt').open('a') as f:f.write('\nPASS: Existing trusted Ed25519 key signs the exact packages and source commit.\nPASS: Isolated Windows update, corrupt-stage rejection, interrupted recovery\n      and layout migration preserve synthetic user data; ROM-free self-tests only.\nSource commit: '+head+'\n')
    paths=[p for p in sorted(a.output.rglob('*')) if p.is_file() and p.name!='SHA256SUMS.txt']
    hashes={p.relative_to(a.output).as_posix():sha(p) for p in paths}
    (a.output/'SHA256SUMS.txt').write_text(''.join(value+'  '+name+'\n' for name,value in hashes.items()))
    for name,value in hashes.items():
        if sha(a.output/name)!=value:raise ValueError('Handoff copy changed: '+name)
    print('Signed stable handoff sealed:',len(paths),'files;',a.output)
