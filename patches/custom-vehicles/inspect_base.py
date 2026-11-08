"""Read-only inspection of locally owned native car model structure."""
from pathlib import Path
import argparse
import hashlib
import json
import struct
import zlib


def inspect(path, overview=None, metadata=None, hash_header=False):
    data = path.read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    lut = {'dcf54c82a58f6b38603b5865e90c3baabfe553a4d141a1ea2b282170a2e98876': 969568,
           '7de1a8fb2a9558cfc3d9ad4497df698c1e89cf7095ac1531557df2af40ba8bcf': 970976}[digest]
    u32 = lambda b, p: struct.unpack_from('>I', b, p)[0]
    u16 = lambda b, p: struct.unpack_from('>H', b, p)[0]
    sections = [data[lut+208+u32(data,lut+4+4*i):lut+208+u32(data,lut+8+4*i)] for i in range(50)]
    def record(section, index):
        table = sections[section-1]
        return sections[section][u32(table,4*index):u32(table,4*index+4)]
    names = ('KRUNCH','BUMPER','TIPTUP','CONKER','TIMBER','BANJO','DRUMSTICK','PIPSY','TT','DIDDY')
    headers = (2,3,4,5,6,7,8,9,1,0)
    hashes = {}
    texture_names = json.loads(metadata.read_text())['files']['order'] if metadata else []
    if overview:
        from PIL import Image, ImageDraw
        overview.mkdir(parents=True, exist_ok=True)
    for name, header_id in zip(names, headers):
        header = record(34, header_id)
        model_table = u32(header,0x10)
        models = [u32(header,model_table+4*i) for i in range(header[0x55])]
        result = []
        for model_id in models:
            if model_id == 0xffffffff:
                continue
            packed = record(29,model_id)
            raw = zlib.decompress(packed[5:],-15)
            hashes[model_id] = hashlib.sha256(raw).hexdigest()
            nv, nt, nb, na = (u16(raw,p) for p in (0x24,0x26,0x28,0x4a))
            va, ba, ia = (u32(raw,p) for p in (4,0x38,0x4c))
            ta, tx = u32(raw,8), u32(raw,0)
            xyz = [struct.unpack_from('>hhh',raw,va+10*i) for i in range(nv)]
            indices = [struct.unpack_from('>h',raw,ia+2*i)[0] for i in range(nv)] if na and ia+nv*2 <= len(raw) else []
            batches = []
            for b in range(nb):
                p = ba + b*12
                v, ve = u16(raw,p+2), u16(raw,p+14)
                texture_id=u32(raw,tx+raw[p]*8) if raw[p] < u16(raw,0x22) else -1
                batches.append({'batch':b,'texture_slot':raw[p], 'texture_id':texture_id,
                    'material':texture_names[texture_id] if 0<=texture_id<len(texture_names) else 'untextured', 'vertices': [v,ve],
                    'triangles':[u16(raw,p+4),u16(raw,p+16)],
                    'animated':sum(1 for i in indices[v:ve] if i >= 0),
                    'y': [min(struct.unpack_from('>h',raw,va+10*i+2)[0] for i in range(v,ve)),
                          max(struct.unpack_from('>h',raw,va+10*i+2)[0] for i in range(v,ve))]})
            result.append({'model':model_id, 'vertices':nv, 'triangles':nt, 'animated':na,
                           'batches':batches})
            if overview:
                image=Image.new('RGB',(1700,900),(18,24,32));draw=ImageDraw.Draw(image)
                palette=[(240,100,80),(100,210,100),(90,150,250),(245,210,80),(190,110,230),(80,220,220),
                         (240,150,70),(180,200,100),(170,160,220),(80,180,140),(210,120,160),(150,200,240)]
                for view,(a,c) in enumerate(((0,1),(2,1),(0,2))):
                    origin=(270+view*550,350)
                    def project(i):return (int(origin[0]+xyz[i][a]*1.7),int(origin[1]-xyz[i][c]*1.7))
                    for batch in batches:
                        b=batch['batch'];first,last=batch['triangles'];base=batch['vertices'][0]
                        for tri in range(first,last):
                            points=[project(base+raw[ta+16*tri+1+i]) for i in range(3)]
                            draw.polygon(points,fill=palette[b%len(palette)],outline=(30,35,40))
                        xys=[project(i) for i in range(*batch['vertices'])]
                        if xys:draw.text((sum(p[0] for p in xys)/len(xys),sum(p[1] for p in xys)/len(xys)),str(b),fill='white',stroke_width=1,stroke_fill='black')
                    draw.text((view*550+30,450),('front XY','side ZY','top XZ')[view],fill='white')
                draw.text((20,15),f'{name} LOD{len(result)-1} model {model_id}: static orthographic asset inspection, not a running game',fill='white')
                for i,b in enumerate(batches):
                    text=f"{b['batch']:2} tex {b['texture_id']:4}: {b['material'].replace('ASSET_TEX3D_OBJECTS_','')} / animated {b['animated']}/{b['vertices'][1]-b['vertices'][0]} / tris {b['triangles']}"
                    draw.text((20+(i//22)*850,490+(i%22)*18),text,fill=palette[i%len(palette)])
                image.save(overview/f'{name}-lod{len(result)-1}.png')
        if not hash_header: print(json.dumps({'driver':name, 'header':header_id, 'models':result}))
    if hash_header:
        print('#pragma once\n// SHA-256 only: no ROM/model bytes. Verified native model ownership profiles.\n#include <map>\n#include <string_view>\nnamespace dkr::mods::vehicles {\ninline const std::map<unsigned,std::string_view> BaseModelHashes{')
        for model_id, digest in sorted(hashes.items()):print('    {' + str(model_id) + ',"' + digest + '"},')
        print('};\n}')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('rom',type=Path)
    parser.add_argument('--overview',type=Path)
    parser.add_argument('--metadata',type=Path)
    parser.add_argument('--hash-header',action='store_true')
    args=parser.parse_args()
    inspect(args.rom,args.overview,args.metadata,args.hash_header)
