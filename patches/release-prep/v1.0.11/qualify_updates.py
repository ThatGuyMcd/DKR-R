"""Use an exact approved preceding package for isolated Windows updater checks."""
from pathlib import Path
import argparse,hashlib,json,sys
AUTH=Path(__file__).resolve().parent;ROOT=AUTH.parents[2]
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__,add_help=False);p.add_argument('--previous',type=Path,required=True);p.add_argument('--previous-receipt',type=Path,required=True);a,rest=p.parse_known_args()
    receipt=json.loads(a.previous_receipt.read_text());checksum=hashlib.sha256(a.previous.read_bytes()).hexdigest()
    previous=receipt['version']
    if previous not in {'1.0.12','1.0.14-beta.3'} or receipt['artifact']['sha256']!=checksum or receipt['artifact']['name']!=a.previous.name:raise ValueError('Previous package differs from an approved source receipt')
    path=ROOT/'scripts/test_update_package.py';code=path.read_text(encoding='utf-8')
    def once(old,new):
        global code
        if code.count(old)!=1:raise ValueError('Updater harness anchor changed: '+old[:80])
        code=code.replace(old,new,1)
    once('        with zipfile.ZipFile(args.package) as z:\n            prefix = artifact["root"] + "/"',
        '        with zipfile.ZipFile('+repr(str(a.previous.resolve()))+') as z:\n            prefix = '+repr('DKR-R-'+previous+'-Windows-x64/'))
    once('value["version"] = "1.0.6"','value["version"] = '+repr(previous))
    once('"1.0.6" if args.case == "corrupt-stage" else manifest["version"]',repr(previous)+' if args.case == "corrupt-stage" else manifest["version"]')
    sys.argv=[str(path),*rest];exec(compile(code,str(path),'exec'),{'__file__':str(path),'__name__':'__main__'})
