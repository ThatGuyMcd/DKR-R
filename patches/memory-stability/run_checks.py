"""Execute only the synthetic allocator fixtures and retained ROM-free checks."""
from pathlib import Path
import argparse,hashlib,importlib.util,json,os,re,subprocess,sys
sys.dont_write_bytecode=True
AUTH=Path(__file__).resolve().parent;ROOT=AUTH.parents[1]
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('bin','retained-bin','output'):p.add_argument('--'+name,type=Path,required=True)
    a=p.parse_args()
    if a.output.exists():raise ValueError('Choose a fresh checks folder')
    a.output.mkdir(parents=True);results={}
    for rev in (77,80):
        exe=a.bin/('DKRMemoryStability'+str(rev)+'Checks'+('.exe' if os.name=='nt' else ''))
        run=subprocess.run([str(exe.resolve())],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=90,
            creationflags=subprocess.CREATE_NO_WINDOW if os.name=='nt' else 0)
        (a.output/f'allocator{rev}.log').write_bytes(run.stdout)
        if run.returncode!=0:raise ValueError('Memory regression failed: '+run.stdout.decode(errors='replace'))
        for marker in (b'largest=3072',b'requests=6800,16800 rejected safely',b'requests=6800,16800 accepted',b'5000 cycles; no game or ROM executed.'):
            if marker not in run.stdout:raise ValueError('Memory evidence missing: '+str(marker))
        checks=re.search(rb'(\d+) memory stability checks passed',run.stdout)
        if not checks:raise ValueError('Memory fixture count missing')
        results[str(rev)]={'passed_checks':int(checks[1]),'sha256':hashlib.sha256(exe.read_bytes()).hexdigest(),'exit_code':0}
        entry=a.bin/('DKRMemoryOwned'+str(rev)+'Checks'+('.exe' if os.name=='nt' else ''))
        run=subprocess.run([str(entry.resolve())],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=20,
            creationflags=subprocess.CREATE_NO_WINDOW if os.name=='nt' else 0)
        (a.output/f'owned{rev}.log').write_bytes(run.stdout)
        if run.returncode or b'13 owned heap entry checks passed' not in run.stdout:raise ValueError('Owned fallback regression failed')
        results['owned'+str(rev)]={'passed_checks':13,'sha256':hashlib.sha256(entry.read_bytes()).hexdigest(),'exit_code':0}
    subprocess.run([sys.executable,'-B',str(ROOT/'patches/release-prep/v1.0.10/run_checks.py'),
        '--bin',str(a.retained_bin),'--output',str(a.output/'retained')],check=True)
    (a.output/'CHECKS.json').write_text(json.dumps({'game_or_rom_or_rsp_executed':False,
        'allocator':results,'retained':json.loads((a.output/'retained/CHECKS.json').read_text())},indent=2)+'\n')
    print('Actual allocator and retained regressions passed:',json.dumps(results))
