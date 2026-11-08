"""Static car-ownership audit of native and prepared characters. No game/mod execution."""
from pathlib import Path
import argparse
from collections import Counter
import hashlib
import json
import math
import re
import struct
import zlib

AUTH=Path(__file__).resolve().parent
HEADERS=(2,3,4,5,6,7,8,9,1,0)
NAMES=('Krunch','Bumper','Tiptup','Conker','Timber','Banjo','Drumstick','Pipsy','T.T.','Diddy')
u32=lambda b,p:struct.unpack_from('>I',b,p)[0]
u16=lambda b,p:struct.unpack_from('>H',b,p)[0]

class Bank:
    def __init__(self,path):
        data=path.read_bytes();digest=hashlib.sha256(data).hexdigest()
        lut={'dcf54c82a58f6b38603b5865e90c3baabfe553a4d141a1ea2b282170a2e98876':969568,
             '7de1a8fb2a9558cfc3d9ad4497df698c1e89cf7095ac1531557df2af40ba8bcf':970976}[digest]
        self.digest=digest
        self.sections=[data[lut+208+u32(data,lut+4+4*i):lut+208+u32(data,lut+8+4*i)] for i in range(50)]
    def record(self,s,i):
        a,b=u32(self.sections[s-1],i*4),u32(self.sections[s-1],i*4+4)
        if a>b or b>len(self.sections[s]):raise ValueError('Invalid stock range')
        return self.sections[s][a:b]

def models(h):
    n=h[0x55];at=u32(h,0x10)
    if h[0x53] or not 1<=n<=32 or at+4*n>len(h):raise ValueError('Invalid car LOD list')
    return [u32(h,at+i*4) for i in range(n)]

def model(packed):
    raw=zlib.decompress(packed[5:],-15)
    if len(raw)>4*1024*1024 or len(raw)<0x80:raise ValueError('Invalid model size')
    nv,nt,nb=(u16(raw,p) for p in (0x24,0x26,0x28))
    vp,tp,bp=(u32(raw,p) for p in (4,8,0x38))
    if not 1<=nv<=8192 or nt>8192 or nb>1024 or vp+nv*10>len(raw) or tp+nt*16>len(raw) or bp+(nb+1)*12>len(raw):raise ValueError('Model exceeds its bounds')
    faces=[None]*nt;owners=[None]*nt;ids=[None]*nt;geometry=[None]*nt
    animated=u16(raw,0x4a);am=u32(raw,0x4c)
    if animated and am+nv*2>len(raw):raise ValueError('Invalid animation map')
    animation=[struct.unpack_from('>h',raw,am+i*2)[0] for i in range(nv)] if animated else [-1]*nv
    for b in range(nb):
        at=bp+b*12;vs,ve,ts,te=(u16(raw,at+p) for p in (2,14,4,16))
        if vs>ve or ve>nv or ts>te or te>nt:raise ValueError('Invalid batch')
        for t in range(ts,te):
            if owners[t] is not None:raise ValueError('Overlapping triangle ownership')
            at=tp+t*16;indices=[vs+raw[at+1+c] for c in range(3)]
            if any(i>=ve for i in indices):raise ValueError('Invalid vertex index')
            corners=[raw[vp+i*10:vp+i*10+6]+raw[at+4+c*4:at+8+c*4] for c,i in enumerate(indices)]
            faces[t]=bytes([raw[at]&0x40])+b''.join(corners);owners[t]=b;ids[t]=indices
            geometry[t]=b''.join(sorted(raw[vp+i*10:vp+i*10+6] for i in indices))
    if None in owners:raise ValueError('Unowned triangle')
    return {'raw':raw,'faces':faces,'geometry':geometry,'animation':animation,'owners':owners,'indices':ids,'vertices':nv,'triangles':nt,'batches':nb}

def native_mask(m,mid):
    masks={int(k):[int(x) for x in v.split(',')] for k,v in re.findall(r'\{(\d+),\{([\d,]+)\}\}',(AUTH/'native/vehicle_resources.cpp').read_text())}
    hidden=[b in masks[mid] for b in m['owners']]
    keeps={26:((151,157),(159,165)),27:((111,116),(118,123)),28:((93,99),),40:((15,21),),35:((11,13),),41:((18,22),)}
    for a,b in keeps.get(mid,()):hidden[a:b]=[False]*(b-a)
    return hidden

def ownership_digest(raw):
    normalized=bytearray(raw);tx=u32(raw,0)
    for t in range(u16(raw,0x22)):struct.pack_into('>I',normalized,tx+t*8,0)
    return hashlib.sha256(normalized).hexdigest()

def main(a):
    bank=Bank(a.rom)
    blob_paths={p.name:p for d in a.library.rglob('blobs') if d.is_dir() for p in d.iterdir() if p.is_file()}
    blobs={};cached_models={}
    def blob(digest):
        if digest not in blobs:
            p=blob_paths.get(digest)
            if p is None:raise ValueError('Missing immutable blob '+digest)
            b=p.read_bytes()
            if hashlib.sha256(b).hexdigest()!=digest:raise ValueError('Blob digest mismatch')
            blobs[digest]=b
        return blobs[digest]
    def parsed(record):
        key=hashlib.sha256(record).hexdigest()
        if key not in cached_models:cached_models[key]=model(record)
        return cached_models[key]
    donors=[];geometry_donors=[]
    for base,header in enumerate(HEADERS):
        group=[]
        for mid in models(bank.record(34,header))[:5]:
            m=parsed(bank.record(29,mid));hidden=native_mask(m,mid)
            group.append((mid,Counter(f for f,h in zip(m['faces'],hidden) if h),Counter(f for f,h in zip(m['geometry'],hidden) if h)))
            geometry_donors.append((Counter(f for f,h in zip(m['geometry'],hidden) if h),{f for f,h in zip(m['geometry'],hidden) if not h}))
        donors.append(group)
    reviewed={}
    for digest,nv,nt,ranges in re.findall(r'\{"([a-f0-9]{64})",\{(\d+),(\d+),\{(.*?)\}\}\}',(AUTH/'native/vehicle_character_masks.hpp').read_text()):
        reviewed[digest]=(int(nv),int(nt),[(int(x),int(y)) for x,y in re.findall(r'\{(\d+),(\d+)\}',ranges)])
    rows=[]
    def inspect(name,base,header,record,identity,source,animations=None):
        row={'name':name,'base':NAMES[base],'base_id':base,'id':identity,'source':source,'lods':[]}
        try:
            scale=struct.unpack_from('>f',header,12)[0]
            if not math.isfinite(scale) or not 0<scale<=16:raise ValueError('Invalid native scale')
            mids=models(header);row['model_ids']=mids;row['native_scale']=scale
            for lod in range(5):
                mid=mids[min(lod,len(mids)-1)];m=parsed(record(29,mid));available=Counter(m['faces'])
                matches=[d for d,required,_ in donors[base] if all(available[f]==n for f,n in required.items())]
                best=max(donors[base],key=lambda item:sum(min(available[f],n) for f,n in item[1].items()))
                required=best[1]
                available_geometry=Counter(m['geometry'])
                best_geometry=max([(b,d,g) for b,group in enumerate(donors) for d,_,g in group],key=lambda item:sum(min(available_geometry[f],n) for f,n in item[2].items()))
                own_geometry=max(donors[base],key=lambda item:sum(min(available_geometry[f],n) for f,n in item[2].items()))
                static=[i for i,tri in enumerate(m['indices']) if all(m['animation'][v]<0 for v in tri)]
                xyz=[struct.unpack_from('>hhh',m['raw'],u32(m['raw'],4)+i*10) for i in range(m['vertices'])]
                mode='exact-native' if matches else 'unsupported';hidden=None
                if a.after:
                    review=reviewed.get(ownership_digest(m['raw']))
                    if review:
                        nv,nt,ranges=review
                        if (nv,nt)!=(m['vertices'],m['triangles']):raise ValueError('Reviewed counts changed')
                        hidden=[i for x,y in ranges for i in range(x,y)]
                        if len(hidden)!=len(set(hidden)) or any(i<0 or i>=nt for i in hidden) or len(hidden)>=nt:raise ValueError('Invalid reviewed mask')
                        mode='reviewed-ownership'
                    elif not matches:
                        for required_geometry,retained in geometry_donors:
                            if all(f not in retained and available_geometry[f]==n for f,n in required_geometry.items()):
                                mode='canonical-native';hidden=[i for i,f in enumerate(m['geometry']) if f in required_geometry];break
                row['lods'].append({'lod':lod,'model':mid,'vertices':m['vertices'],'triangles':m['triangles'],
                    'sha256':hashlib.sha256(m['raw']).hexdigest(),'matches':matches,'closest_donor':best[0],
                    'required':sum(required.values()),'found':sum(min(available[f],n) for f,n in required.items()),
                    'missing':sum(max(0,n-available[f]) for f,n in required.items()),
                    'ambiguous':sum(available[f]>n for f,n in required.items()),
                    'geometry':{'donor_base':NAMES[best_geometry[0]],'donor_model':best_geometry[1],
                        'found':sum(min(available_geometry[f],n) for f,n in best_geometry[2].items()),'required':sum(best_geometry[2].values()),
                        'own_found':sum(min(available_geometry[f],n) for f,n in own_geometry[2].items()),
                        'own_required':sum(own_geometry[2].values())},'static_triangles':len(static),
                    'mode':mode,'hidden':None if hidden is None else len(hidden),'retained':None if hidden is None else m['triangles']-len(hidden)})
                if a.overview and not matches:
                    from PIL import Image,ImageDraw
                    a.overview.mkdir(parents=True,exist_ok=True)
                    file=a.overview/(hashlib.sha256(m['raw']).hexdigest()+'.png')
                    if not file.exists():
                        image=Image.new('RGB',(1600,1000),(15,24,35));draw=ImageDraw.Draw(image)
                        static_set=set(static);native_keys=set().union(*(set(g) for _,_,g in donors[base]))
                        for view,(x,y) in enumerate(((2,1),(0,1),(0,2))):
                            for i,tri in enumerate(m['indices']):
                                colour=(130,160,200) if i not in static_set else (245,160,65)
                                if m['geometry'][i] in native_keys:colour=(65,210,115)
                                if hidden is not None:colour=(245,130,65) if i in hidden else (75,190,225)
                                ps=[(260+view*530+xyz[v][x]*scale*6,390-xyz[v][y]*scale*6) for v in tri]
                                draw.polygon(ps,fill=colour,outline=(30,45,60))
                            draw.line((view*530,390,(view+1)*530,390),fill='white')
                            draw.line((260+view*530,60,260+view*530,650),fill='white')
                        draw.text((10,10),name+' LOD'+str(lod)+' model '+str(mid)+' / blue=animated; orange=static; green=exact stock car geometry',fill='white')
                        for b in range(m['batches']):
                            tris=[i for i,o in enumerate(m['owners']) if o==b]
                            kept=sum(i in static_set for i in tris);known=sum(m['geometry'][i] in native_keys for i in tris)
                            text=f'batch {b}: triangles {min(tris) if tris else 0}-{max(tris) if tris else 0}, static {kept}/{len(tris)}, native-car {known}'
                            draw.text((10+(b//18)*530,660+(b%18)*18),text,fill='white')
                        image.save(file)
            row['compatible']=all(v['mode']!='unsupported' for v in row['lods'])
        except (ValueError,KeyError,IndexError,struct.error,zlib.error) as e:
            row['compatible']=False;row['error']=str(e)
        rows.append(row)
    for base,name in enumerate(NAMES):inspect(name,base,bank.record(34,HEADERS[base]),bank.record,'native:'+str(base),'native')
    seen=set()
    for f in sorted(a.library.glob('prepared-characters/*/*/character.json')):
        doc=json.loads(f.read_text());identity=doc['id']
        if identity in seen:continue
        seen.add(identity);records={(r['section'],r['record']):r['hash'] for r in doc['records']}
        def record(s,i,records=records):
            digest=records.get((s,i));original=bank.record(s,i)
            if digest is None or hashlib.sha256(original).hexdigest()==digest:return original
            return blob(digest)
        try:header=record(34,doc['headers'][0])
        except (ValueError,KeyError,IndexError) as e:
            rows.append({'name':doc['name'],'id':identity,'compatible':False,'error':str(e),'source':str(f)});continue
        inspect(doc['name'],doc['behaviour'],header,record,identity,str(f),{r['model']:r['ids'] for r in doc['animations']})
    report={'inspection':'Read-only static asset/ownership audit; no game, mod or tests executed.',
            'bank_sha256':bank.digest,'characters':len(rows),'compatible':sum(r['compatible'] for r in rows),'rows':rows}
    a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({k:v for k,v in report.items() if k!='rows'}))
    for r in rows:
        if not r['compatible']:print(json.dumps({'name':r['name'],'id':r['id'],'error':r.get('error'),'lods':r.get('lods',[])}))

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--rom',type=Path,required=True);p.add_argument('--library',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--overview',type=Path);p.add_argument('--after',action='store_true')
    main(p.parse_args())
