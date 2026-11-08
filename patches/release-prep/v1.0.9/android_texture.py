"""Use the accepted wide-file-clock formatter for Android texture cache keys."""
from pathlib import Path
import argparse,hashlib,json

def compose(source,output):
    gallery=source.parent/'texture_gallery.inl';text=gallery.read_text(encoding='utf-8')
    anchor='std::to_string(stamp.modified.time_since_epoch().count())'
    if text.count(anchor)!=1:raise ValueError('Texture preview timestamp anchor changed')
    text=text.replace(anchor,'dkr::release::file_stamp(stamp.modified)',1)
    body=source.read_text(encoding='utf-8');include='#include "texture_gallery.inl"'
    if body.count(include)!=1:raise ValueError('Texture gallery include anchor changed')
    helper=source.parent.parent/'release-store/android_store_compat.hpp'
    if not helper.is_file():raise ValueError('Accepted Android file-clock helper missing')
    body='#include "'+helper.as_posix()+'"\n'+body.replace(include,'#include "'+(output/'texture_gallery.inl').as_posix()+'"',1)
    return gallery,helper,{'texture_dump.cpp':body.encode('utf-8'),'texture_gallery.inl':text.encode('utf-8')}

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--source',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    gallery,helper,files=compose(a.source,a.output)
    for name,data in files.items():
        target=a.output/name
        if not target.exists() or target.read_bytes()!=data:target.write_bytes(data)
    ledger=a.output/'swaps.json';rows=json.loads(ledger.read_text())
    if len(rows)!=4:raise ValueError('Expected preceding platform adapters')
    rows.append({'target':'DKRPortGame','path':a.source.as_posix(),'new':(a.output/'texture_dump.cpp').as_posix(),
        'input_sha256':hashlib.sha256(a.source.read_bytes()).hexdigest(),'output_sha256':hashlib.sha256(files['texture_dump.cpp']).hexdigest()})
    ledger.write_text(json.dumps(rows,indent=2)+'\n')
    (a.output/'texture-inputs.json').write_text(json.dumps({p.as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in (gallery,helper)},indent=2)+'\n')
    print('Android texture previews use the existing seconds/subsecond file-clock formatter without narrowing the wide epoch count.')
