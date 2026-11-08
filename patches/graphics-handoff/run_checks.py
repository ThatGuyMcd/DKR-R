"""Execute only ROM-free synthetic fixtures, including the retained suite."""
from pathlib import Path
import argparse,hashlib,json,os,re,subprocess,sys
sys.dont_write_bytecode=True
AUTH=Path(__file__).resolve().parent;ROOT=AUTH.parents[1]
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('bin','output'):p.add_argument('--'+name,type=Path,required=True)
    a=p.parse_args()
    if a.output.exists():raise ValueError('Choose a fresh checks folder')
    a.output.mkdir(parents=True)
    exe=a.bin/('DKRGraphicsHandoffChecks'+('.exe' if os.name=='nt' else ''))
    run=subprocess.run([str(exe.resolve())],capture_output=True,timeout=60,creationflags=subprocess.CREATE_NO_WINDOW if os.name=='nt' else 0)
    (a.output/'handoff.log').write_bytes(run.stdout+run.stderr)
    if run.returncode or not re.search(rb'37 graphics handoff checks; 8000 concurrent transfers',run.stdout):raise ValueError('Handoff regression failed: '+(run.stdout+run.stderr).decode(errors='replace'))
    subprocess.run([sys.executable,'-B',str(ROOT/'patches/render-stability/run_checks.py'),
        '--bin',str(a.bin),'--output',str(a.output/'render')],check=True)
    result={'game_or_rom_or_rsp_executed':False,'handoff_checks':37,'concurrent_transfers':8000,'exit_code':run.returncode,
        'executable_sha256':hashlib.sha256(exe.read_bytes()).hexdigest(),'retained':json.loads((a.output/'render/CHECKS.json').read_text())}
    (a.output/'CHECKS.json').write_text(json.dumps(result,indent=2)+'\n')
    print('Producer handoff, rendering, allocator and online synthetic checks passed.')
