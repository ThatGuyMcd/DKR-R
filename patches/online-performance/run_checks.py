"""Run only the actual dispatch extracts and synthetic setup/party fixtures."""
from pathlib import Path
import argparse,hashlib,json,os,re,subprocess
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('bin','output'):p.add_argument('--'+name,type=Path,required=True)
    a=p.parse_args()
    if a.output.exists():raise ValueError('Choose a fresh checks directory')
    a.output.mkdir(parents=True);report={};suffix='.exe' if os.name=='nt' else ''
    targets=[f'DKROnlineMenu{r}{s}Checks' for r in (77,80) for s in ('Before','')]+['DKROnlineSetupChecks','DKROnlinePartyChecks']
    for name in targets:
        exe=a.bin/(name+suffix);run=subprocess.run([str(exe.resolve())],capture_output=True,timeout=90,
            creationflags=subprocess.CREATE_NO_WINDOW if os.name=='nt' else 0)
        (a.output/(name+'.log')).write_bytes(run.stdout+run.stderr)
        negative='Before' in name
        if run.returncode!=(1 if negative else 0):raise ValueError('Regression failed: '+name+'\n'+(run.stdout+run.stderr).decode(errors='replace'))
        match=re.search(rb'(\d+) (?:actual menu-dispatch|online setup/cache/logging|party runtime regression) checks',run.stdout)
        if not negative and (not match or int(match[1])<25):raise ValueError('Fixture did not finish: '+name)
        report[name]={'exit_code':run.returncode,'passed_checks':int(match[1]) if match else 0,'executable_sha256':hashlib.sha256(exe.read_bytes()).hexdigest()}
    (a.output/'CHECKS.json').write_text(json.dumps({'version':'1.0.11-online-playtest.1','fixtures':report,
        'game_rom_rsp_gpu_live_multiplayer_executed':False},indent=2)+'\n')
    print('Actual before/after menu guards and synthetic setup/party regressions passed.')
