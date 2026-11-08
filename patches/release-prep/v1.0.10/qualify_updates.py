"""Exercise the existing Windows updater harness from the actual v1.0.9 package."""
from pathlib import Path
import argparse,hashlib,sys
AUTH=Path(__file__).resolve().parent;ROOT=AUTH.parents[2]
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__,add_help=False)
    p.add_argument('--previous',type=Path,required=True);a,rest=p.parse_known_args()
    # Pin the exact published v1.0.9 bytes, not a new executable with an old label.
    with a.previous.open('rb') as f:checksum=hashlib.file_digest(f,'sha256').hexdigest()
    if checksum!='02e6b1071ff447f33ac8cc82ad9ff9d462bd6575f76be1da1a19f6eb12ff903c':
        raise ValueError('Previous package differs from the published v1.0.9 Windows release')
    path=ROOT/'scripts/test_update_package.py';code=path.read_text()
    def once(old,new):
        global code
        if code.count(old)!=1:raise ValueError('Updater harness anchor changed: '+old[:80])
        code=code.replace(old,new,1)
    anchor='        with zipfile.ZipFile(args.package) as z:\n            prefix = artifact["root"] + "/"'
    once(anchor,'        with zipfile.ZipFile('+repr(str(a.previous.resolve()))+') as z:\n            prefix = "DKR-R-1.0.9-Windows-x64/"')
    once('value["version"] = "1.0.6"','value["version"] = "1.0.9"')
    once('("1.0.6" if args.case == "corrupt-stage" else manifest["version"])',
         '("1.0.9" if args.case == "corrupt-stage" else manifest["version"])')
    sys.argv=[str(path),*rest]
    exec(compile(code,str(path),'exec'),{'__file__':str(path),'__name__':'__main__'})
