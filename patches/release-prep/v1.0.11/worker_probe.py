"""Probe only the packaged worker's empty capability request, optionally in QEMU."""
from pathlib import Path
import argparse,json,os,sys
AUTH=Path(__file__).resolve().parent;ROOT=AUTH.parents[2]
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--emulator',type=Path);p.add_argument('--sysroot',type=Path);a,rest=p.parse_known_args()
    prefix=[str(a.emulator)] if a.emulator else []
    if a.sysroot:os.environ['QEMU_LD_PREFIX']=str(a.sysroot)
    path=ROOT/'patches/lobby-stability/probe_worker.py';code=path.read_text()
    old='subprocess.run([str(worker), str(request)]'
    if code.count(old)!=1:raise ValueError('Worker probe entry changed')
    code=code.replace(old,'subprocess.run([*PREFIX, str(worker), str(request)]',1)
    sys.argv=[str(path),*rest]
    exec(compile(code,str(path),'exec'),{'__name__':'__main__','__file__':str(path),'PREFIX':prefix})
