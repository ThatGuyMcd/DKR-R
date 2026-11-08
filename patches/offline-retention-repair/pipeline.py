"""Compose offline retention above the retained race-start and online repairs."""
from pathlib import Path
import argparse
import hashlib
import json
import re
import sys
import lobby_layout
sys.dont_write_bytecode=True
AUTH=Path(__file__).resolve().parent
ROOT=AUTH.parents[1]


def sha(data):return hashlib.sha256(data).hexdigest()
def once(text,a,b):
    if text.count(a)!=1:raise ValueError('Offline keep repair anchor changed: '+a[:160])
    return text.replace(a,b,1)
def write(path,data):
    path.parent.mkdir(parents=True,exist_ok=True)
    if not path.is_file() or path.read_bytes()!=data:path.write_bytes(data)


def compose(rows,output):
    expected={('DKRLegacyImportLibrary','online_mod_profile.cpp'),('DKRPortGame','runtime_ui.cpp')}
    if len(rows)!=2 or {(row['target'],Path(row['path']).name) for row in rows}!=expected:
        raise ValueError('Expected the final first-party online profile and launcher sources')
    profile=next(row for row in rows if row['target']=='DKRLegacyImportLibrary')
    source=Path(profile['path']);raw=source.read_bytes();text=raw.decode().replace('\r\n','\n')
    if any(x.lower() in ('extern','recompiledfuncs','recompiledpatches') for x in source.parts):raise ValueError('Protected source cannot be composed')
    namespace=text.index('namespace dkr::mods::online {')
    first_end=text.index('Bundle export_host(',namespace)
    tree_start=text.index('namespace {\nvoid ensure_directory(')
    retain_start=text.index('void retain_profile(')
    # Delegate the existing bounded path/tree/job helpers unchanged in the new
    # first-party unit. This is not a submodule or recompiled source input.
    helper=text[:namespace]+text[namespace:first_end]+text[tree_start:retain_start]
    helper+='\n'+(AUTH/'native/offline_keep.inl').read_text()+'\n}\n'
    start=text.index('    std::set<std::filesystem::path> libraries;',retain_start)
    end=text.index('    // Track Lab identity,',start)
    text=text[:start]+'''    retain_legacy_sources(sources,root,stop);
    struct Copy {std::filesystem::path source,target,staged;};std::vector<Copy> copies;
    std::uint64_t bytes=0;unsigned files=0;
'''+text[end:]
    text=once(text,'trees.size(),copies.size(),files,static_cast<unsigned long long>(bytes)',
                    'std::size_t(0),copies.size(),files,static_cast<unsigned long long>(bytes)')
    text='#include "'+(output/'offline_keep.hpp').as_posix()+'"\n'+text
    def rewrite(content,origin):
        def one(m):
            if Path(m[1]).name=='offline_keep.hpp':return '#include "'+(output/'offline_keep.hpp').as_posix()+'"'
            p=Path(m[1]);p=p if p.is_absolute() else origin.parent/p
            return '#include "'+p.resolve().as_posix()+'"' if p.is_file() else m[0]
        return re.sub(r'^#include "([^"\n]+)"',one,content,flags=re.M)
    outputs={'online_mod_profile.cpp':rewrite(text,source).encode(),
             'offline_keep.cpp':rewrite(helper,source).encode(),
             'offline_keep.hpp':rewrite((AUTH/'native/offline_keep.hpp').read_text(),source).encode()}
    swaps=[{**profile,'new':str(output/source.name),'input_sha256':sha(raw),'output_sha256':sha(outputs[source.name])}]
    ui=next(row for row in rows if row['target']=='DKRPortGame');ui_source=Path(ui['path'])
    if any(x.lower() in ('extern','recompiledfuncs','recompiledpatches') for x in ui_source.parts):raise ValueError('Protected UI input')
    ui_raw=ui_source.read_bytes();ui_text=ui_raw.decode().replace('\r\n','\n')
    matches=list(re.finditer(r'^#include "([^"\n]*runtime_online_ui[.]inl)"',ui_text,re.M))
    if len(matches)!=1:raise ValueError('Expected one final online UI include')
    ui_include=Path(matches[0][1]);ui_include=ui_include if ui_include.is_absolute() else ui_source.parent/ui_include
    online=lobby_layout.compose(ui_include.read_text().replace('\r\n','\n'),output)
    outputs['runtime_online_ui.inl']=rewrite(online,ui_include).encode()
    outputs['lobby_sidebar.inl']=(AUTH/'native/lobby_sidebar.inl').read_bytes()
    ui_text=ui_text.replace(matches[0][0],'#include "'+(output/'runtime_online_ui.inl').as_posix()+'"',1)
    outputs['runtime_ui.cpp']=rewrite(ui_text,ui_source).encode()
    swaps.append({**ui,'new':str(output/ui_source.name),'input_sha256':sha(ui_raw),'output_sha256':sha(outputs[ui_source.name])})
    return swaps,outputs


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--inputs',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();out=a.output.resolve()
    if out.is_relative_to(ROOT):raise ValueError('Generated offline retention must stay outside the checkout')
    swaps,outputs=compose(json.loads(a.inputs.read_text()),out)
    for name,data in outputs.items():write(out/name,data)
    write(out/'swaps.json',(json.dumps(swaps,indent=2)+'\n').encode())
    print('Selected, deduplicated inactive offline retention staged; prior race/online fixes retained.')
