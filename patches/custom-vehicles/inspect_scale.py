"""Static ROM/package unit inspection. Never imports or executes a mod/game."""
from pathlib import Path
from io import BytesIO
import hashlib
import json
import re
import struct
import zipfile
import zlib

AUTH = Path(__file__).resolve().parent

def inspect_native(path):
    data = path.read_bytes()
    lut = {'dcf54c82a58f6b38603b5865e90c3baabfe553a4d141a1ea2b282170a2e98876':969568,
           '7de1a8fb2a9558cfc3d9ad4497df698c1e89cf7095ac1531557df2af40ba8bcf':970976}[hashlib.sha256(data).hexdigest()]
    u32 = lambda b,p:struct.unpack_from('>I',b,p)[0]
    u16 = lambda b,p:struct.unpack_from('>H',b,p)[0]
    sections = [data[lut+208+u32(data,lut+4+4*i):lut+208+u32(data,lut+8+4*i)] for i in range(50)]
    def record(s,i):
        table=sections[s-1]
        return sections[s][u32(table,4*i):u32(table,4*i+4)]
    masks = {int(m[0]):[int(v) for v in m[1].split(',')] for m in re.findall(r'\{(\d+),\{([\d,]+)\}\}',(AUTH/'native/vehicle_resources.cpp').read_text())}
    for name,header_id in zip(('KRUNCH','BUMPER','TIPTUP','CONKER','TIMBER','BANJO','DRUMSTICK','PIPSY','TT','DIDDY'),(2,3,4,5,6,7,8,9,1,0)):
        header=record(34,header_id);native_scale=struct.unpack_from('>f',header,12)[0]
        model_id=u32(header,u32(header,16));raw=zlib.decompress(record(29,model_id)[5:],-15)
        vp,bp,tp=u32(raw,4),u32(raw,0x38),u32(raw,8)
        points=[]
        for batch in masks[model_id]:
            at=bp+12*batch;first_vertex=u16(raw,at+2)
            for triangle in range(u16(raw,at+4),u16(raw,at+16)):
                for corner in range(3):
                    index=first_vertex+raw[tp+triangle*16+1+corner]
                    points.append(struct.unpack_from('>hhh',raw,vp+10*index))
        lo=[min(p[a] for p in points) for a in range(3)];hi=[max(p[a] for p in points) for a in range(3)]
        print(json.dumps({'native':name,'scale':native_scale,'vehicle_raw_bounds':[lo,hi],
                          'vehicle_world_extents':[(hi[a]-lo[a])*native_scale for a in range(3)]}))

def inspect_package(path):
    data=path.read_bytes()
    with zipfile.ZipFile(BytesIO(data)) as outer:
        names=[n for n in outer.namelist() if n.lower().endswith('.dkrkart')]
        if names:data=outer.read(names[0])
    with zipfile.ZipFile(BytesIO(data)) as archive:
        manifest=json.loads(archive.read('kart.json'));geometry=json.loads(archive.read('lods/lod0.json'))
        points=[p for m in geometry['meshes'] for p in m['vertices']]
        scale=manifest['settings']['scale'];lo=[min(p[a] for p in points)*scale for a in range(3)];hi=[max(p[a] for p in points)*scale for a in range(3)]
        print(json.dumps({'package':path.name,'manifest':manifest,'post_scale_bounds':[lo,hi],
                          'readme':archive.read('README.txt').decode(errors='replace') if 'README.txt' in archive.namelist() else ''}))

if __name__=='__main__':
    import argparse
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('rom',type=Path);p.add_argument('packages',type=Path,nargs='+');a=p.parse_args()
    inspect_native(a.rom)
    for path in a.packages:inspect_package(path)
