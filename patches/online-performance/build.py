"""Build the playtest and CPU/data fixtures without executing a game."""
from pathlib import Path
import argparse,importlib.util,subprocess
ROOT=Path(__file__).resolve().parents[2]
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('build','log'):p.add_argument('--'+n,type=Path,required=True)
    p.add_argument('--cmake',default='cmake');p.add_argument('--jobs',type=int,default=3);p.add_argument('--sanitizer',action='store_true');p.add_argument('--portraits',action='store_true');a=p.parse_args()
    s=importlib.util.spec_from_file_location('retained_checks',ROOT/'patches/release-prep/v1.0.11/run_checks.py')
    m=importlib.util.module_from_spec(s);s.loader.exec_module(m)
    names=['DKRPortGame',*[v[0] for v in m.TARGETS.values()],'DKRVehicleOriginalChecks','DKRVehicleOriginalBeforeChecks',
        *[f'DKROnlineMenu{r}{s}Checks' for r in (77,80) for s in ('Before','')],'DKROnlineSetupChecks','DKROnlinePartyChecks']
    if a.sanitizer:names.append('DKRVehicleOriginalSanitizerChecks')
    if a.portraits:names.append('DKRCharacterPortraitChecks')
    a.log.parent.mkdir(parents=True,exist_ok=True)
    with a.log.open('w') as out:subprocess.run([a.cmake,'--build',str(a.build),'--config','RelWithDebInfo','--target',*names,'--parallel',str(a.jobs)],stdout=out,stderr=subprocess.STDOUT,check=True)
    print('Playtest and named CPU/data fixtures compiled:',a.build)
