"""Run only the named synthetic admission/handler/retained test executables."""
from pathlib import Path
import argparse, hashlib, json, os, re, subprocess, sys
AUTH=Path(__file__).resolve().parent
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('before','after','handler','retention','race-before','race-after','output'):p.add_argument('--'+name,type=Path,required=True)
    a=p.parse_args()
    expected={'before':'DKRSpectatorAdmissionBeforeChecks','after':'DKRSpectatorAdmissionChecks','handler':'DKRSpectatorHandlerChecks'}
    if a.output.exists():raise ValueError('Choose a fresh synthetic evidence folder')
    for name,stem in expected.items():
        if getattr(a,name).stem!=stem:raise ValueError('Only explicit synthetic fixtures may execute')
    a.output.mkdir(parents=True);receipt={'schema':1,'game_or_rom_or_rsp_executed':False,'synthetic_wav_and_owner_hooks_only':True}
    for name in expected:
        binary=getattr(a,name).resolve()
        r=subprocess.run([str(binary)],cwd=a.output,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=60,
            creationflags=subprocess.CREATE_NO_WINDOW if os.name=='nt' else 0)
        if len(r.stdout)>1024*1024:raise ValueError('Synthetic output budget exceeded')
        (a.output/(name+'.log')).write_bytes(r.stdout)
        receipt[name]={'exit_code':r.returncode,'binary_sha256':sha(binary)}
        if name=='before':
            if r.returncode!=1 or b'Custom music was not prepared at a confirmed loading boundary.' not in r.stdout:
                raise ValueError('Previous production music path did not reproduce the cold observer failure')
        else:
            matched=re.search(rb'(\d+) spectator (?:admission|handler) checks passed',r.stdout)
            if r.returncode!=0 or not matched:raise ValueError(name+' checks failed; inspect '+name+'.log')
            receipt[name]['passed_checks']=int(matched[1])
    receipt['test_sources']={p.name:sha(p) for p in sorted((AUTH/'tests').glob('*.cpp'))}
    (a.output/'ADMISSION-RECEIPT.json').write_text(json.dumps(receipt,indent=2)+'\n')
    subprocess.run([sys.executable,'-B',str(AUTH.parent/'offline-retention-repair/run_checks.py'),
        '--retention',str(a.retention),'--race-before',str(a.race_before),'--race-after',str(a.race_after),
        '--output',str(a.output/'retained')],check=True)
    print('Old resident code reproduces the spectator failure; repaired music:',receipt['after']['passed_checks'],
        'checks; party handler:',receipt['handler']['passed_checks'],'checks; playtest.3 regressions retained.')
