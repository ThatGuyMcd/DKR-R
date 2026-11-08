"""Run only the actual dispatch extracts and synthetic setup/party fixtures."""
from pathlib import Path
import argparse,hashlib,json,os,re,subprocess
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('bin','output'):p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--emulator',type=Path);p.add_argument('--sysroot',type=Path)
    a=p.parse_args()
    if a.output.exists():raise ValueError('Choose a fresh checks directory')
    a.output.mkdir(parents=True);report={};suffix='.exe' if os.name=='nt' else ''
    prefix=[str(a.emulator)] if a.emulator else [];env=os.environ.copy()
    if a.sysroot:env['QEMU_LD_PREFIX']=str(a.sysroot)
    targets=[f'DKROnlineMenu{r}{s}Checks' for r in (77,80) for s in ('Before','')]+['DKROnlineSetupChecks','DKROnlinePartyChecks','DKRDirectoryLifecycleChecks']
    for name in targets:
        exe=a.bin/(name+suffix);run=subprocess.run([*prefix,str(exe.resolve())],env=env,capture_output=True,timeout=180,
            creationflags=subprocess.CREATE_NO_WINDOW if os.name=='nt' else 0)
        (a.output/(name+'.log')).write_bytes(run.stdout+run.stderr)
        negative='Before' in name
        if run.returncode!=(1 if negative else 0):raise ValueError('Regression failed: '+name+'\n'+(run.stdout+run.stderr).decode(errors='replace'))
        match=re.search(rb'(\d+) (?:actual menu-dispatch|online setup/cache/logging|party runtime regression|directory lifecycle) checks',run.stdout)
        minimum=21 if name=='DKRDirectoryLifecycleChecks' else 25
        if not negative and (not match or int(match[1])<minimum):raise ValueError('Fixture did not finish: '+name)
        report[name]={'exit_code':run.returncode,'passed_checks':int(match[1]) if match else 0,'executable_sha256':hashlib.sha256(exe.read_bytes()).hexdigest()}
    (a.output/'CHECKS.json').write_text(json.dumps({'version':(Path(__file__).resolve().parents[2]/'VERSION').read_text().strip(),'fixtures':report,
        'game_rom_rsp_gpu_live_multiplayer_executed':False},indent=2)+'\n')
    print('Actual before/after menu guards and synthetic setup/party regressions passed.')
