"""Reuse the vehicle card preview hook for background-decoded character portraits."""
from pathlib import Path
import argparse,hashlib,json,re,sys
sys.dont_write_bytecode=True
AUTH=Path(__file__).resolve().parent;ROOT=AUTH.parents[1]
def sha(data):return hashlib.sha256(data).hexdigest()
def once(text,old,new):
    if text.count(old)!=1:raise ValueError('Portrait anchor differs: '+old[:120])
    return text.replace(old,new,1)
def write(path,data):
    path.parent.mkdir(parents=True,exist_ok=True)
    if not path.is_file() or path.read_bytes()!=data:path.write_bytes(data)
def compose(rows,out):
    if len(rows)!=2 or {Path(r['path']).name for r in rows}!={'runtime_ui.cpp','runtime_mod_library_ui.inl'}:raise ValueError('Portrait source closure differs')
    files={p.name:p.read_bytes() for p in (AUTH/'native').glob('*')};swaps=[]
    for row in rows:
        source=Path(row['path']);raw=source.read_bytes();text=raw.decode().replace('\r\n','\n')
        if source.suffix=='.cpp':
            text='#include "'+(out/'character_portraits.hpp').as_posix()+'"\n'+text
            text=once(text,'#include "runtime_mod_library_ui.inl"','#include "'+(out/'runtime_mod_library_ui.inl').as_posix()+'"')
            text=once(text,'    ReleaseModStoreTextures();','    ReleaseCharacterPortraits();\n    ReleaseModStoreTextures();')
            text=once(text,'    g_mod_store_renderer = renderer;','    g_mod_store_renderer = renderer;\n    g_character_portrait_renderer = renderer;')
        else:
            text=once(text,'// ------------------------------------------------------------ library data',
                '#include "'+(out/'character_portraits_ui.inl').as_posix()+'"\n\n// ------------------------------------------------------------ library data')
            text=once(text,'        view.info = "Custom racer";',
                '        view.info = "Custom racer";\n        view.preview_height=100;view.preview=[item](float w,float h){DrawCharacterPortrait(item,w,h);};')
        def relative(m):
            path=Path(m[1]);path=path if path.is_absolute() else source.parent/path
            return '#include "'+path.resolve().as_posix()+'"' if path.is_file() else m[0]
        text=re.sub(r'^#include "([^"\n]+)"',relative,text,flags=re.M)
        files[source.name]=text.encode();swaps.append({**row,'new':str(out/source.name),'input_sha256':sha(raw),'output_sha256':sha(text.encode()),'compiled':source.suffix=='.cpp'})
    return swaps,files
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--inputs',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    out=a.output.resolve()
    if out.is_relative_to(ROOT):raise ValueError('Portrait outputs must remain outside checkout')
    rows,files=compose(json.loads(a.inputs.read_text()),out)
    for name,data in files.items():write(out/name,data)
    write(out/'swaps.json',(json.dumps(rows,indent=2)+'\n').encode())
    print('Character portraits staged through the existing vehicle card preview hook.')
