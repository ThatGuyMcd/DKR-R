"""Run new synthetic render fixtures and all retained memory/online checks."""
from pathlib import Path
import argparse,hashlib,json,os,re,subprocess,sys
sys.dont_write_bytecode=True
AUTH=Path(__file__).resolve().parent;ROOT=AUTH.parents[1]
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('bin','output'):p.add_argument('--'+name,type=Path,required=True)
    a=p.parse_args()
    if a.output.exists():raise ValueError('Choose a fresh checks folder')
    a.output.mkdir(parents=True);checks={}
    for name,count in (('DKRRenderStabilityChecks',30),('DKRLiquidResidencyChecks',23)):
        exe=a.bin/(name+('.exe' if os.name=='nt' else ''))
        run=subprocess.run([str(exe.resolve())],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=60,
            creationflags=subprocess.CREATE_NO_WINDOW if os.name=='nt' else 0)
        (a.output/(name+'.log')).write_bytes(run.stdout)
        if run.returncode or not re.search(str(count).encode()+rb' .*checks',run.stdout):raise ValueError('Render regression failed: '+run.stdout.decode(errors='replace'))
        checks[name]={'passed_checks':count,'exit_code':run.returncode,'sha256':hashlib.sha256(exe.read_bytes()).hexdigest()}
    subprocess.run([sys.executable,'-B',str(ROOT/'patches/memory-stability/run_checks.py'),
        '--bin',str(a.bin),'--retained-bin',str(a.bin),'--output',str(a.output/'memory')],check=True)
    report={'game_or_rom_or_rsp_executed':False,'render':checks,'concurrent_workloads':4000,
        'retained_memory':json.loads((a.output/'memory/CHECKS.json').read_text())}
    (a.output/'CHECKS.json').write_text(json.dumps(report,indent=2)+'\n')
    print('New render selection/residency and retained memory/online regressions passed.')
