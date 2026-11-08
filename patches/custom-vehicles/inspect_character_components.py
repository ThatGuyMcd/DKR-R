"""Static connected-mesh inspection for explicit, digest-bound ownership reviews."""
from pathlib import Path
import argparse
import hashlib
import json
import struct
from audit_characters import Bank, model, u32

def main(a):
    from PIL import Image,ImageDraw
    bank=Bank(a.rom)
    paths={p.name:p for d in a.library.rglob('blobs') if d.is_dir() for p in d.iterdir() if p.is_file()}
    report=json.loads(a.audit.read_text());result={}
    a.output.mkdir(parents=True,exist_ok=True)
    for row in report['rows']:
        if row['compatible'] or row['source']=='native':continue
        doc=json.loads(Path(row['source']).read_text())
        records={(r['section'],r['record']):r['hash'] for r in doc['records']}
        for lod in row['lods']:
            digest=lod['sha256']
            if digest in result:continue
            mid=lod['model'];packed=paths[records[29,mid]].read_bytes() if records[29,mid] in paths else bank.record(29,mid)
            if hashlib.sha256(packed).hexdigest()!=records[29,mid]:raise ValueError('Missing exact input')
            m=model(packed);vp=u32(m['raw'],4)
            xyz=[struct.unpack_from('>hhh',m['raw'],vp+10*i) for i in range(m['vertices'])]
            parent=list(range(m['triangles']));seen={}
            def root(i):
                while parent[i]!=i:parent[i]=parent[parent[i]];i=parent[i]
                return i
            for t,tri in enumerate(m['indices']):
                for v in tri:
                    p=xyz[v]
                    if p in seen:parent[root(t)]=root(seen[p])
                    else:seen[p]=t
            groups={}
            for t in range(m['triangles']):groups.setdefault(root(t),[]).append(t)
            groups=sorted(groups.values(),key=lambda ts:ts[0])
            components=[]
            for ts in groups:
                pts=[xyz[v] for t in ts for v in m['indices'][t]]
                components.append({'triangles':ts,'bounds':[[min(p[c] for p in pts) for c in range(3)],[max(p[c] for p in pts) for c in range(3)]]})
            result[digest]={'name':row['name'],'lod':lod['lod'],'model':mid,'components':components}
            image=Image.new('RGB',(1600,350+((len(groups)+3)//4)*230),(15,24,35));draw=ImageDraw.Draw(image)
            draw.text((10,10),f"{row['name']} LOD{lod['lod']} {digest}",fill='white')
            for i,comp in enumerate(components):
                # Three large side/front/top overviews; separate side panel per component.
                colour=((i*71)%170+70,(i*43)%170+70,(i*103)%170+70)
                for view,(x,y) in enumerate(((2,1),(0,1),(0,2))):
                    for t in comp['triangles']:
                        pts=[(260+view*530+xyz[v][x]*row['native_scale']*5,220-xyz[v][y]*row['native_scale']*5) for v in m['indices'][t]]
                        draw.polygon(pts,fill=colour,outline=(25,35,45))
                ox=200+(i%4)*400;oy=530+(i//4)*230
                for t in comp['triangles']:
                    pts=[(ox+xyz[v][2]*row['native_scale']*4,oy-xyz[v][1]*row['native_scale']*4) for v in m['indices'][t]]
                    draw.polygon(pts,fill=colour,outline=(25,35,45))
                draw.text((10+(i%4)*400,355+(i//4)*230),f"Component {i}: {len(comp['triangles'])} faces / {comp['triangles'][0]}..{comp['triangles'][-1]}",fill='white')
                draw.text((10+(i%4)*400,375+(i//4)*230),str(comp['bounds']),fill='white')
            image.save(a.output/(digest+'.png'))
    (a.output/'components.json').write_text(json.dumps(result,indent=2)+'\n')
    for digest,r in result.items():print(r['name'],r['lod'],digest,[(i,len(c['triangles']),c['bounds']) for i,c in enumerate(r['components'])])

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for arg in ('rom','library','audit','output'):p.add_argument('--'+arg,type=Path,required=True)
    main(p.parse_args())
