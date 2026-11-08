"""Run only named ROM-free regression fixtures, optionally through ARM64 QEMU."""
from pathlib import Path
import argparse,hashlib,json,os,re,subprocess
NAMES={'before':('DKRSpectatorAdmissionBeforeChecks',1,b'Custom music was not prepared at a confirmed loading boundary.'),
    'after':('DKRSpectatorAdmissionChecks',0,rb'(\d+) spectator admission checks passed'),
    'handler':('DKRSpectatorHandlerChecks',0,rb'(\d+) spectator handler checks passed'),
    'retention':('DKROfflineRetentionChecks',0,rb'(\d+) offline retention checks passed'),
    'race-before':('DKRRaceReleaseBeforeChecks',1,b'FIRST POST-LOAD RELEASE WAS DISCARDED'),
    'race-after':('DKRRaceReleaseChecks',0,rb'(\d+) party runtime regression checks passed'),
    'transfer':('DKRStabilityTransferChecks',0,None)}
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('bin','output'):p.add_argument('--'+n,type=Path,required=True)
    p.add_argument('--emulator',type=Path);p.add_argument('--sysroot',type=Path);a=p.parse_args()
    if a.output.exists():raise ValueError('Choose a fresh fixture evidence folder')
    a.output.mkdir(parents=True);r={'game_or_rom_or_rsp_executed':False,'emulated_arm64':bool(a.emulator),'checks':{}}
    env=os.environ.copy()
    if a.sysroot:env['QEMU_LD_PREFIX']=str(a.sysroot)
    for label,(stem,status,marker) in NAMES.items():
        exe=a.bin/(stem+('.exe' if os.name=='nt' else ''))
        command=([str(a.emulator)] if a.emulator else [])+[str(exe.resolve())]
        if label=='retention':command+=[str((a.output/'retention-fixture').resolve())]
        run=subprocess.run(command,cwd=a.output,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=90,
            creationflags=subprocess.CREATE_NO_WINDOW if os.name=='nt' else 0)
        (a.output/(label+'.log')).write_bytes(run.stdout)
        if run.returncode!=status:raise ValueError('Unexpected fixture result: '+label)
        if marker and not re.search(marker,run.stdout):raise ValueError('Expected evidence missing: '+label)
        m=re.search(rb'(\d+) .*checks passed',run.stdout)
        r['checks'][label]={'exit_code':run.returncode,'binary_sha256':hashlib.sha256(exe.read_bytes()).hexdigest(),
            'passed_checks':int(m[1]) if m and status==0 else None}
    (a.output/'CHECKS.json').write_text(json.dumps(r,indent=2)+'\n')
    print('ROM-free release regressions passed:',json.dumps(r['checks']))
