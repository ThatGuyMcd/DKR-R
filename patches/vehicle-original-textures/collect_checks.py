"""Reuse unchanged data-check binaries, then run the retained suite once."""
from pathlib import Path
import argparse,hashlib,json,os,shutil,subprocess,sys
sys.dont_write_bytecode=True
ROOT=Path(__file__).resolve().parents[2]
def sha(path):
    with path.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('bin','admission','output'):p.add_argument('--'+name,type=Path,required=True)
    a=p.parse_args()
    if a.output.exists():raise ValueError('Choose a fresh checks folder')
    report=json.loads((a.admission/'CHECKS.json').read_text());suffix='.exe' if os.name=='nt' else ''
    for key,name in (('before','DKRVehicleOriginalBeforeChecks'),('after','DKRVehicleOriginalChecks')):
        if sha(a.bin/(name+suffix))!=report['binary_sha256'][key]:raise ValueError('Re-run admission against current fixture binaries: '+name)
    if report['sanitizer'] and sha(a.bin/'DKRVehicleOriginalSanitizerChecks')!=report['sanitizer']['executable_sha256']:
        raise ValueError('Re-run the changed sanitizer fixture')
    shutil.copytree(a.admission,a.output)
    subprocess.run([sys.executable,'-B',str(ROOT/'patches/graphics-handoff/run_checks.py'),'--bin',str(a.bin),'--output',str(a.output/'retained')],check=True)
    report['retained']=json.loads((a.output/'retained/CHECKS.json').read_text())
    report['vehicle_checks_reused_unchanged_binaries']=True
    (a.output/'CHECKS.json').write_text(json.dumps(report,indent=2)+'\n')
    print('Unchanged vehicle-data checks combined with fresh retained graphics/memory/online regressions.')
