"""Support original-resolution car artwork through bounded preparation only."""
from pathlib import Path
import argparse,hashlib,json,re,sys
sys.dont_write_bytecode=True
AUTH=Path(__file__).resolve().parent;ROOT=AUTH.parents[1]
VERSION='1.0.10-vehicle-playtest.1'
NAMES={('DKRCustomVehicles','vehicle_package.cpp'),('DKRPortGame','vehicle_rt64.cpp')}
def sha(data):return hashlib.sha256(data).hexdigest()
def once(text,old,new):
    if text.count(old)!=1:raise ValueError('Original car texture anchor changed: '+old[:100])
    return text.replace(old,new,1)
def write(path,data):
    path.parent.mkdir(parents=True,exist_ok=True)
    if not path.is_file() or path.read_bytes()!=data:path.write_bytes(data)
def compose(rows,out):
    if len(rows)!=2 or {(r['target'],Path(r['path']).name) for r in rows}!=NAMES:raise ValueError('Car texture source closure differs')
    package=next(Path(r['path']) for r in rows if r['target']=='DKRCustomVehicles')
    original_headers=package.parent
    files={}
    for name in ('package_compatibility.hpp','driver_anchor_units.hpp'):
        text=(AUTH/'native'/name).read_text()
        for header in ('vehicle_package.hpp','vehicle_seat_alignment.hpp'):
            path=original_headers/header
            if not path.is_file():raise ValueError('Original car ABI header missing: '+header)
            text=text.replace('#include "'+header+'"','#include "'+path.resolve().as_posix()+'"')
        files[name]=text.encode()
    swaps=[]
    for row in rows:
        source=Path(row['path']);raw=source.read_bytes();text=raw.decode().replace('\r\n','\n')
        if source.name=='vehicle_package.cpp':
            text=once(text,'#include "magic_code_package.hpp"','#include "magic_code_package.hpp"\n#include "'+(out/'package_compatibility.hpp').as_posix()+'"')
            text=once(text,'    if(!profile.empty()&&profile!="native-4kib-rgba32-safe-v1")bad("unsupported car texture profile.");',
                '    const bool original_resolution=profile=="original-resolution-v1";\n'
                '    if(!profile.empty()&&profile!="native-4kib-rgba32-safe-v1"&&!original_resolution)bad("unsupported car texture profile.");\n'
                '    original_textures::seating_contract(manifest,*p);\n    original_textures::effect_contract(manifest);')
            text=once(text,'        if(texels*2>4096)bad(', '        if(!original_resolution&&texels*2>4096)bad(')
            text=once(text,'        if(!profile.empty()&&texels*4>4096)bad(', '        if(profile=="native-4kib-rgba32-safe-v1"&&texels*4>4096)bad(')
            text=once(text,'    p->portrait=image(zip.require(portrait),512,image_budget);\n    return p;',
                '    p->portrait=image(zip.require(portrait),512,image_budget);\n'
                '    if(original_resolution)original_textures::prepare(*p);\n    return p;')
        else:
            text='#include "'+(out/'driver_anchor_units.hpp').as_posix()+'"\n'+text
            text=once(text,'const auto anchor=seating::anchor(car,driver_->base);',
                'const auto anchor=original_textures::model_anchor(car,driver_->base,driver_->native_scale);')
            for axis in ('x','y','z'):
                text=once(text,'-anchor.'+axis+'/driver_->native_scale','-anchor.'+axis)
            text=once(text,'seat-anchor-units=studio-to-world seating=%s','seat-anchor-units=%s seating=%s')
            text=once(text,'DriverSeatAdvance,car.per_driver()?',
                'DriverSeatAdvance,(car.per_driver()&&car.authored_anchors)?"raw-native-v1":"studio-to-world",car.per_driver()?')
        def relative(m):
            path=Path(m[1]);path=path if path.is_absolute() else source.parent/path
            return '#include "'+path.resolve().as_posix()+'"' if path.is_file() else m[0]
        text=re.sub(r'^#include "([^"\n]+)"',relative,text,flags=re.M)
        name=row['target']+'/'+source.name;files[name]=text.encode()
        swaps.append({**row,'new':str(out/name),'input_sha256':sha(raw),'output_sha256':sha(text.encode())})
    return swaps,files
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--inputs',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    out=a.output.resolve()
    if out.is_relative_to(ROOT):raise ValueError('Car preparation outputs must remain outside checkout')
    rows,files=compose(json.loads(a.inputs.read_text()),out)
    for name,data in files.items():write(out/name,data)
    write(out/'swaps.json',(json.dumps(rows,indent=2)+'\n').encode())
    print('Bounded original-pixel car tiles and explicit native-anchor contract staged; native memory limits retained.')
