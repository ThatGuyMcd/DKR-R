"""Run ROM-free contributor checks on a native Linux filesystem copy."""
from pathlib import Path
import argparse,hashlib,json,shutil,subprocess,sys
from release import ROOT,files
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--checkout',type=Path,required=True);p.add_argument('--log',type=Path,required=True);a=p.parse_args()
    if a.checkout.exists():raise ValueError('Choose a fresh native test checkout')
    subprocess.run(['git','clone','--quiet','--no-hardlinks',str(ROOT),str(a.checkout)],check=True)
    rows=[]
    for name in files():
        source=ROOT/name;target=a.checkout/name
        target.parent.mkdir(parents=True,exist_ok=True)
        # Preserve native checkout execute bits for existing shell entrypoints.
        target.write_bytes(source.read_bytes());rows.append({'path':name,'sha256':hashlib.sha256(target.read_bytes()).hexdigest()})
    a.log.parent.mkdir(parents=True,exist_ok=True)
    with a.log.open('w') as out:
        subprocess.run([sys.executable,'-B','scripts/check.py','--suite','tooling','--suite','metadata','--suite','assets'],cwd=a.checkout,stdout=out,stderr=subprocess.STDOUT,check=True)
    a.log.with_suffix('.json').write_text(json.dumps({'game_or_rom_executed':False,'native_filesystem_checkout':str(a.checkout),'source_files':rows,'exit_code':0},indent=2)+'\n')
    print('Native-filesystem Linux contributor, metadata and asset checks passed.')
