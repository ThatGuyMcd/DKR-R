"""Inspect installed portrait artifacts as data through the compiled production loader."""
from pathlib import Path
import argparse,hashlib,json,os,re,subprocess
def local(p):
    return Path('\\\\?\\'+str(p.resolve())) if os.name=='nt' else p
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('exe','root','output'):p.add_argument('--'+name,type=Path,required=True)
    a=p.parse_args();rows=[]
    paths=sorted((a.root/'prepared-characters').glob('*/*/character.json'))
    if not paths or len(paths)>1024:raise ValueError('Choose a bounded installed character dataset')
    for path in paths:
        data=local(path).read_bytes();document=json.loads(data);folder=path.parent
        result=subprocess.run([str(a.exe.resolve()),'--portrait',str(a.root.resolve()),document['id'],hashlib.sha256(data).hexdigest(),
            folder.parent.name,folder.name,document['revision']],capture_output=True,timeout=15,
            creationflags=subprocess.CREATE_NO_WINDOW if os.name=='nt' else 0)
        match=re.fullmatch(rb'(\d+) (\d+) ([a-f0-9]{64})\s*',result.stdout)
        if result.returncode or not match:raise ValueError('Portrait data decode failed for '+document['name']+': '+result.stderr.decode(errors='replace'))
        rows.append({'manifest_sha256':hashlib.sha256(data).hexdigest(),'width':int(match[1]),'height':int(match[2]),'rgba_sha256':match[3].decode()})
    a.output.write_text(json.dumps({'artifacts':rows,'count':len(rows),'game_or_rom_executed':False},indent=2)+'\n')
    print('Installed character portraits decoded as data:',len(rows))
