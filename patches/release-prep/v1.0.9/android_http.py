"""Preserve Android platform HTTPS and forward the directory instance header."""
from pathlib import Path
import argparse,hashlib,json,re

def compose(source,output):
    text=source.read_text(encoding='utf-8')
    matches=list(re.finditer(r'^#include "([^"\n]+/mod_store_android_http[.]inl)"',text,re.M))
    if len(matches)!=1:raise ValueError('Expected one Android HTTPS include')
    match=matches[0];original=Path(match[1]);http=original.read_text(encoding='utf-8')
    anchor='        header("Origin","https://positron-portal.com");'
    if http.count(anchor)!=1:raise ValueError('Android API header anchor changed')
    http=http.replace(anchor,anchor+'\n        if(!r.online_instance.empty())header("X-DKRR-Instance",r.online_instance);',1)
    text=text[:match.start()]+'#include "'+(output/'mod_store_android_http.inl').as_posix()+'"'+text[match.end():]
    return original,{'mod_store.cpp':text.encode('utf-8'),'mod_store_android_http.inl':http.encode('utf-8')}

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--source',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    original,files=compose(a.source,a.output)
    for name,data in files.items():
        target=a.output/name
        if not target.exists() or target.read_bytes()!=data:target.write_bytes(data)
    ledger=a.output/'swaps.json';rows=json.loads(ledger.read_text())
    if len(rows)!=1:raise ValueError('Expected the preceding snapshot adapter only')
    rows.append({'target':'DKRPortGame','path':a.source.as_posix(),'new':(a.output/'mod_store.cpp').as_posix(),
        'input_sha256':hashlib.sha256(a.source.read_bytes()).hexdigest(),'output_sha256':hashlib.sha256(files['mod_store.cpp']).hexdigest()})
    ledger.write_text(json.dumps(rows,indent=2)+'\n')
    (a.output/'transport.json').write_text(json.dumps({'source':original.as_posix(),'input_sha256':hashlib.sha256(original.read_bytes()).hexdigest(),
        'output_sha256':hashlib.sha256(files['mod_store_android_http.inl']).hexdigest(),'scope':'API-only instance header; existing Android TLS, credential and redirect policy retained'},indent=2)+'\n')
    print('Android platform HTTPS forwards the verified online instance header only to the existing API.')
