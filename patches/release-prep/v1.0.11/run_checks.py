"""Run named ROM-free release fixtures, natively or through the ARM64 emulator."""
from pathlib import Path
import argparse,hashlib,json,os,re,subprocess,sys
sys.dont_write_bytecode=True
AUTH=Path(__file__).resolve().parent;ROOT=AUTH.parents[2]
TARGETS={
 'handoff':('DKRGraphicsHandoffChecks',0,rb'37 graphics handoff checks; 8000 concurrent transfers'),
 'render':('DKRRenderStabilityChecks',0,rb'30 .*checks'),
 'water':('DKRLiquidResidencyChecks',0,rb'28 production residency checks passed'),
 'allocator77':('DKRMemoryStability77Checks',0,rb'184812 memory stability checks passed'),
 'allocator80':('DKRMemoryStability80Checks',0,rb'184812 memory stability checks passed'),
 'owned77':('DKRMemoryOwned77Checks',0,rb'13 owned heap entry checks passed'),
 'owned80':('DKRMemoryOwned80Checks',0,rb'13 owned heap entry checks passed'),
 'before':('DKRSpectatorAdmissionBeforeChecks',1,rb'Custom music was not prepared at a confirmed loading boundary.'),
 'after':('DKRSpectatorAdmissionChecks',0,rb'345 spectator admission checks passed'),
 'handler':('DKRSpectatorHandlerChecks',0,rb'45 spectator handler checks passed'),
 'retention':('DKROfflineRetentionChecks',0,rb'(?:68|69) offline retention checks passed'),
 'race-before':('DKRRaceReleaseBeforeChecks',1,rb'FIRST POST-LOAD RELEASE WAS DISCARDED'),
 'race-after':('DKRRaceReleaseChecks',0,rb'96 party runtime regression checks passed'),
 'transfer':('DKRStabilityTransferChecks',0,rb'checks passed')}
def sha(p):
    with p.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('bin','input','legacy','output'):p.add_argument('--'+n,type=Path,required=True)
    for n in ('emulator','sysroot','sanitizer'):p.add_argument('--'+n,type=Path)
    a=p.parse_args()
    if a.output.exists():raise ValueError('Choose a fresh check folder')
    a.output.mkdir(parents=True);prefix=[str(a.emulator)] if a.emulator else [];env=os.environ.copy()
    if a.sysroot:env['QEMU_LD_PREFIX']=str(a.sysroot)
    suffix='.exe' if os.name=='nt' else '';results={}
    for label,(name,status,marker) in TARGETS.items():
        exe=(a.bin/(name+suffix)).resolve();command=prefix+[str(exe)]
        if label=='retention':command+=[str((a.output/'retention-fixture').resolve())]
        r=subprocess.run(command,env=env,capture_output=True,timeout=180,
            creationflags=subprocess.CREATE_NO_WINDOW if os.name=='nt' else 0)
        (a.output/(label+'.log')).write_bytes(r.stdout+r.stderr)
        if r.returncode!=status or not re.search(marker,r.stdout+r.stderr):raise ValueError('Release fixture failed: '+label)
        results[label]={'exit_code':r.returncode,'executable_sha256':sha(exe)}
    # Reuse the exact accepted vehicle checks; only the launcher prefix changes
    # for cross-compiled fixture execution. The tested C++ units are unchanged.
    path=ROOT/'patches/vehicle-original-textures/run_checks.py';code=path.read_text()
    old='return subprocess.run([str(exe.resolve()),*map(str,args)],capture_output=True,timeout=90,'
    if code.count(old)!=1:raise ValueError('Vehicle fixture entry changed')
    code=code.replace(old,'return subprocess.run([*PREFIX,str(exe.resolve()),*map(str,args)],env=ENV,capture_output=True,timeout=180,',1)
    argv=sys.argv[:];sys.argv=[str(path),'--bin',str(a.bin),'--input',str(a.input),'--legacy',str(a.legacy),'--output',str(a.output/'vehicles'),'--skip-retained']
    if a.sanitizer:sys.argv+=['--sanitizer',str(a.sanitizer)]
    try:exec(compile(code,str(path),'exec'),{'__file__':str(path),'__name__':'__main__','PREFIX':prefix,'ENV':env})
    finally:sys.argv=argv
    vehicle=json.loads((a.output/'vehicles/CHECKS.json').read_text())
    report={'schema':1,'game_rom_rsp_gpu_or_live_multiplayer_executed':False,'emulated_arm64':bool(prefix),'checks':results,'vehicle':vehicle}
    (a.output/'CHECKS.json').write_text(json.dumps(report,indent=2)+'\n')
    print('Release graphics, memory, online and vehicle data fixtures passed; ARM64 emulated:',bool(prefix))
