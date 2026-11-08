"""Build the release and its explicitly named data/CPU regression fixtures."""
from pathlib import Path
import argparse,subprocess
from run_checks import TARGETS
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('build','log'):p.add_argument('--'+n,type=Path,required=True)
    p.add_argument('--cmake',default='cmake');p.add_argument('--jobs',type=int,default=3);p.add_argument('--sanitizer',action='store_true');a=p.parse_args()
    names=['DKRPortGame',*[v[0] for v in TARGETS.values()],'DKRVehicleOriginalChecks','DKRVehicleOriginalBeforeChecks']
    names += [f'DKROnlineMenu{r}{s}Checks' for r in (77,80) for s in ('Before','')]
    names += ['DKROnlineSetupChecks','DKROnlinePartyChecks','DKRDirectoryLifecycleChecks',
        'DKRCharacterPortraitChecks','DKRProfileChecks','DKRModStoreHistoryChecks']
    if a.sanitizer:names.append('DKRVehicleOriginalSanitizerChecks')
    a.log.parent.mkdir(parents=True,exist_ok=True)
    with a.log.open('w') as out:subprocess.run([a.cmake,'--build',str(a.build),'--config','RelWithDebInfo','--target',*names,'--parallel',str(a.jobs)],stdout=out,stderr=subprocess.STDOUT,check=True)
    print('Release and named synthetic fixtures compiled; no game execution:',a.build)
