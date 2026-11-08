"""Validate real package data, synthetic tiling and unchanged legacy admission."""
from pathlib import Path
import argparse,hashlib,io,json,os,re,subprocess,sys,zipfile
sys.dont_write_bytecode=True
AUTH=Path(__file__).resolve().parent;ROOT=AUTH.parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def need(ok,why):
    if not ok:raise ValueError(why)
def run(exe,args):
    return subprocess.run([str(exe.resolve()),*map(str,args)],capture_output=True,timeout=90,
        creationflags=subprocess.CREATE_NO_WINDOW if os.name=='nt' else 0)
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('bin','input','legacy','output'):p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--sanitizer',type=Path);p.add_argument('--skip-retained',action='store_true');a=p.parse_args()
    need(not a.output.exists(),'Choose a fresh checks folder');a.output.mkdir(parents=True)
    suffix='.exe' if os.name=='nt' else '';after=a.bin/('DKRVehicleOriginalChecks'+suffix);before=a.bin/('DKRVehicleOriginalBeforeChecks'+suffix)
    bad=run(before,[a.input]);(a.output/'before.log').write_bytes(bad.stdout+bad.stderr)
    need(bad.returncode==1 and b'unsupported car texture profile' in bad.stderr,'Before control did not reproduce the original rejection')
    good=run(after,[a.input]);(a.output/'after.log').write_bytes(good.stdout+good.stderr)
    need(good.returncode==0 and b'safe-tiles=18' in good.stdout and b'7642 vehicle compatibility checks passed' in good.stdout,'Updated preparation failed: '+(good.stdout+good.stderr).decode(errors='replace'))
    fingerprint=run(after,['--fingerprint',a.input]);need(fingerprint.returncode==0 and re.fullmatch(rb'[a-f0-9]{64}\s*',fingerprint.stdout),'New package fingerprint failed')
    legacy=[]
    for source in sorted(a.legacy.glob('*.dkrkart')):
        old=run(before,['--fingerprint',source]);new=run(after,['--fingerprint',source])
        need(old.returncode==new.returncode==0 and old.stdout==new.stdout and re.fullmatch(rb'[a-f0-9]{64}\s*',old.stdout),'Legacy package changed: '+source.name)
        legacy.append({'file':source.name,'source_sha256':sha(source),'prepared_sha256':new.stdout.decode().strip()})
    need(len(legacy)>=9,'The retained vehicle sample set is incomplete')
    with zipfile.ZipFile(a.input) as z:members={i.filename:z.read(i) for i in z.infolist()};manifest=json.loads(members['kart.json'])
    cases={
        'unknown-profile':('texture_profile','unreviewed-profile-v9',b'unsupported car texture profile'),
        'native-profile-overflow':('texture_profile','native-4kib-rgba32-safe-v1',b'4 KiB'),
        'unknown-seating':('seating_contract.revision',2,b'unsupported explicit seating contract'),
        'bad-anchor-hash':('seating_contract.anchor_table.sha256','0'*64,b'anchor table does not match'),
        'wrong-anchor-units':('seating_contract.units.native_model_to_studio',2.0,b'unsupported seating coordinate units'),
        'legacy-seat-mix':('seating_contract.driver.legacy_forward_20_applies',True,b'unsupported driver transform'),
        'unknown-effects':('effect_anchors.revision',2,b'unsupported effect anchor revision'),
        'unsupported-custom-boost':('effect_anchors.boost.enabled',True,b'custom boost anchors are not supported yet'),
    }
    variants=a.output/'variants';variants.mkdir();negative={}
    for name,(field,value,marker) in cases.items():
        document=json.loads(json.dumps(manifest));target=document;keys=field.split('.')
        for key in keys[:-1]:target=target[key]
        target[keys[-1]]=value;path=variants/(name+'.dkrkart')
        with zipfile.ZipFile(path,'x',zipfile.ZIP_DEFLATED) as z:
            for filename,data in members.items():z.writestr(filename,json.dumps(document).encode() if filename=='kart.json' else data)
        result=run(after,['--fingerprint',path]);(a.output/(name+'.log')).write_bytes(result.stdout+result.stderr)
        need(result.returncode==1 and marker in result.stderr,'Negative admission changed: '+name)
        negative[name]={'exit_code':result.returncode,'expected_guard':marker.decode()}
    sanitizer=None
    if a.sanitizer:
        result=run(a.sanitizer,[a.input]);(a.output/'sanitizers.log').write_bytes(result.stdout+result.stderr)
        need(result.returncode==0 and b'7642 vehicle compatibility checks passed' in result.stdout,'Vehicle sanitizer check failed')
        sanitizer={'executable_sha256':sha(a.sanitizer),'exit_code':0,'scope':'Synthetic tiling and supplied archive parsing as data; game, ROM, RSP and GPU not executed.'}
    if not a.skip_retained:
        subprocess.run([sys.executable,'-B',str(ROOT/'patches/graphics-handoff/run_checks.py'),'--bin',str(a.bin),'--output',str(a.output/'retained')],check=True)
    report={'schema':1,'game_rom_rsp_gpu_or_live_multiplayer_executed':False,'actual_mod_archive_parsed_as_data':True,
        'input_sha256':sha(a.input),'prepared_sha256':fingerprint.stdout.decode().strip(),'checks':7642,
        'before_exit_code':bad.returncode,'after_exit_code':good.returncode,'legacy':legacy,'negative':negative,'sanitizer':sanitizer,
        'retained':json.loads((a.output/'retained/CHECKS.json').read_text()) if not a.skip_retained else None,
        'binary_sha256':{'before':sha(before),'after':sha(after)}}
    (a.output/'CHECKS.json').write_text(json.dumps(report,indent=2)+'\n')
    print('Vehicle preparation verified: 7642 checks,',len(legacy),'unchanged legacy packages,',len(negative),'negative admissions; no game execution.')
