"""Run the production portrait-loader data fixtures without starting a launcher."""
from pathlib import Path
import argparse,hashlib,json,os,re,subprocess
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('exe','output'):p.add_argument('--'+name,type=Path,required=True)
    a=p.parse_args()
    if a.output.exists():raise ValueError('Choose a fresh portrait report')
    run=subprocess.run([str(a.exe.resolve())],capture_output=True,timeout=30,
        creationflags=subprocess.CREATE_NO_WINDOW if os.name=='nt' else 0)
    a.output.with_suffix('.log').write_bytes(run.stdout+run.stderr)
    match=re.search(rb'(\d+) character portrait checks passed',run.stdout)
    if run.returncode or not match or int(match[1])<14:raise ValueError('Portrait checks failed: '+(run.stdout+run.stderr).decode(errors='replace'))
    a.output.write_text(json.dumps({'exit_code':run.returncode,'passed_checks':int(match[1]),
        'executable_sha256':hashlib.sha256(a.exe.read_bytes()).hexdigest(),'game_or_rom_executed':False},indent=2)+'\n')
    print('Character portrait data and worker checks passed:',int(match[1]))
