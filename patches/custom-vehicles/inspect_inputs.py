"""Read supplied ZIP/JSON data only; does not import, launch or execute a mod."""
from pathlib import Path
from io import BytesIO
import argparse
import hashlib
import json
import zipfile


def inspect(path):
    raw=path.read_bytes()
    with zipfile.ZipFile(BytesIO(raw)) as outer:
        entries=[n for n in outer.namelist() if n.lower().endswith('.dkrkart')]
        packages=[(Path(n).name,outer.read(n)) for n in entries] if entries else [(path.name,raw)]
    for name,data in packages:
        with zipfile.ZipFile(BytesIO(data)) as archive:
            manifest=json.loads(archive.read('kart.json'))
            geometry=json.loads(archive.read('geometry.json'))
            textures=geometry['textures']
            worst=0
            lod_counts=[]
            for lod in range(6):
                document=json.loads(archive.read(f'lods/lod{lod}.json'))
                lod_counts.append(sum(len(mesh['faces']) for mesh in document['meshes']))
                for mesh in document['meshes']:
                    texture=mesh.get('texture',-1)
                    if texture<0:continue
                    t=textures[texture]
                    for uv in mesh['uv']:worst=max(worst,abs(uv[0])*t['width']*32,abs(uv[1])*t['height']*32)
            print(json.dumps({'name':name,'sha256':hashlib.sha256(data).hexdigest(),
                'bytes':len(data),'lod_triangles':lod_counts,
                'texture_dimensions':[[t['width'],t['height']] for t in textures],
                'mesh_colors':[m.get('color') for m in geometry['meshes']],
                'max_native_uv':worst,'native_uv_limit':32700,
                'anchor_kind':manifest.get('driver_anchor_kind','adapter defaults'),
                'inspection':'ZIP/JSON data only; no game/mod execution'}))


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('files',type=Path,nargs='+')
    for path in parser.parse_args().files:inspect(path)
