"""Read-only ROM/prepared-character geometry inspection; no game/mod execution."""
from pathlib import Path
import argparse
import hashlib
import json
import re
import struct
import zlib

AUTH = Path(__file__).resolve().parent
u32 = lambda b,p:struct.unpack_from('>I',b,p)[0]
u16 = lambda b,p:struct.unpack_from('>H',b,p)[0]

def model(packed):
    raw=zlib.decompress(packed[5:],-15)
    vp,tp,bp=(u32(raw,p) for p in (4,8,0x38))
    xyz=[struct.unpack_from('>hhh',raw,vp+10*i) for i in range(u16(raw,0x24))]
    triangles=[]
    for batch in range(u16(raw,0x28)):
        at=bp+batch*12;vs=u16(raw,at+2)
        for t in range(u16(raw,at+4),u16(raw,at+16)):
            at=tp+t*16;ids=[vs+raw[at+1+i] for i in range(3)]
            signature=bytes([raw[at]&0x40])+b''.join(raw[vp+i*10:vp+i*10+6]+raw[at+4+c*4:at+8+c*4] for c,i in enumerate(ids))
            triangles.append((batch,ids,signature))
    return raw,xyz,triangles

def main(args):
    data=args.rom.read_bytes()
    lut={'dcf54c82a58f6b38603b5865e90c3baabfe553a4d141a1ea2b282170a2e98876':969568,
         '7de1a8fb2a9558cfc3d9ad4497df698c1e89cf7095ac1531557df2af40ba8bcf':970976}[hashlib.sha256(data).hexdigest()]
    sections=[data[lut+208+u32(data,lut+4+4*i):lut+208+u32(data,lut+8+4*i)] for i in range(50)]
    def stock(s,i):return sections[s][u32(sections[s-1],i*4):u32(sections[s-1],i*4+4)]
    masks={int(m[0]):[int(v) for v in m[1].split(',')] for m in re.findall(r'\{(\d+),\{([\d,]+)\}\}',(AUTH/'native/vehicle_resources.cpp').read_text())}
    # A few mixed batches keep native driver faces, exactly as the adapter.
    keeps={26:((151,157),(159,165)),27:((111,116),(118,123)),28:((93,99),),40:((15,21),),35:((11,13),),41:((18,22),)}
    names=('Krunch','Bumper','Tiptup','Conker','Timber','Banjo','Drumstick','Pipsy','TT','Diddy')
    heads=(2,3,4,5,6,7,8,9,1,0)
    donors=[]
    for base,hid in enumerate(heads):
        h=stock(34,hid);mid=u32(h,u32(h,16));raw,xyz,ts=model(stock(29,mid))
        hidden={t[2] for i,t in enumerate(ts) if t[0] in masks[mid] and not any(a<=i<b for a,b in keeps.get(mid,()))}
        donors.append(hidden)
    candidates=[(name,base,stock(34,heads[base]),lambda s,i:stock(s,i)) for base,name in enumerate(names)]
    # Historic caches can omit duplicate blobs; resolve by content hash only.
    blobs={p.name:p for folder in (args.prepared,args.prepared.parent/'reviews') for p in folder.glob('*/*/blobs/*') if p.is_file()}
    blobs.update({p.name:p for p in (args.prepared.parent/'reviews').glob('*/blobs/*') if p.is_file()})
    for f in args.prepared.glob('*/*/character.json'):
        doc=json.loads(f.read_text())
        if not any(n in doc['name'].lower() for n in ('rayman','waluigi')):continue
        records={(r['section'],r['record']):r['hash'] for r in doc['records']}
        def record(s,i,records=records):
            digest=records.get((s,i));p=blobs.get(digest)
            if digest and p:
                b=p.read_bytes()
                if hashlib.sha256(b).hexdigest()!=digest:raise ValueError('bad blob')
                return b
            if digest and hashlib.sha256(stock(s,i)).hexdigest()!=digest:raise ValueError('Missing immutable input '+digest)
            return stock(s,i)
        candidates.append((doc['name'],doc['behaviour'],record(34,doc['headers'][0]),record))
    for name,base,header,record in candidates:
        scale=struct.unpack_from('>f',header,12)[0];mid=u32(header,u32(header,16))
        raw,xyz,ts=model(record(29,mid));body=[ids for _,ids,sig in ts if sig not in donors[base]]
        ids=sorted({i for tri in body for i in tri});points=[xyz[i] for i in ids]
        lo=[min(p[a] for p in points) for a in range(3)];hi=[max(p[a] for p in points) for a in range(3)]
        # Native head-pivot values at model header 0x54 are examined, not assumed.
        print(json.dumps({'name':name,'base':names[base],'header_scale':scale,'body_bounds':[[v*scale for v in lo],[v*scale for v in hi]],'body_triangles':len(body),'model':mid,'model_sha256':hashlib.sha256(raw).hexdigest(),'model_header_tail':raw[0x54:0x80].hex()}))
        if args.overview:
            from PIL import Image,ImageDraw
            args.overview.mkdir(parents=True,exist_ok=True)
            image=Image.new('RGB',(1000,650),(15,24,35));draw=ImageDraw.Draw(image)
            for view,(a,b) in enumerate(((2,1),(0,1))):
                for tri in body:
                    ps=[(250+view*500+xyz[i][a]*scale*8,420-xyz[i][b]*scale*8) for i in tri]
                    draw.polygon(ps,fill=(120,170,215),outline=(35,70,100))
                draw.line((view*500,420,(view+1)*500,420),fill='yellow')
                draw.line((250+view*500,0,250+view*500,650),fill='yellow')
            draw.text((10,10),name+' / driver-only static asset, side ZY | front XY',fill='white')
            image.save(args.overview/(re.sub('[^a-zA-Z0-9_-]','_',name)+'.png'))

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--rom',type=Path,required=True);p.add_argument('--prepared',type=Path,required=True);p.add_argument('--overview',type=Path)
    main(p.parse_args())
