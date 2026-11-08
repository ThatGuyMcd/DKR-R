"""Static package placement inspection; no game, imports or mod execution."""
from pathlib import Path
from io import BytesIO
import argparse
import json
import math
import zipfile

def inspect(path):
    data=path.read_bytes()
    with zipfile.ZipFile(BytesIO(data)) as outer:
        names=[n for n in outer.namelist() if n.lower().endswith('.dkrkart')]
        if names:data=outer.read(names[0])
    with zipfile.ZipFile(BytesIO(data)) as package:
        settings=json.loads(package.read('kart.json'))['settings']
        lod=json.loads(package.read('lods/lod0.json'))
        meshes={m['id']:m for m in lod['meshes']}
        wheels=[]
        for role in ('front_left','front_right','rear_left','rear_right'):
            mesh=meshes[settings['wheels'][role]]
            lo=[min(v[a] for v in mesh['vertices']) for a in range(3)]
            hi=[max(v[a] for v in mesh['vertices']) for a in range(3)]
            centre=[(lo[a]+hi[a])*.5 for a in range(3)]
            wheels.append(centre)
            print(json.dumps({'package':path.name,'role':role,'centre_model_units':[v*settings['scale'] for v in centre],
                              'world_y_range':[lo[1]*settings['scale']*.25,hi[1]*settings['scale']*.25]}))
        dx=(wheels[0][0]+wheels[1][0]-wheels[2][0]-wheels[3][0])*.5
        dz=(wheels[0][2]+wheels[1][2]-wheels[2][2]-wheels[3][2])*.5
        length=math.hypot(dx,dz);angle=math.radians(settings['yaw'])
        print(json.dumps({'package':path.name,'front_direction_before_yaw':[dx/length,dz/length],
                          'front_direction_native':[(dx*math.cos(angle)+dz*math.sin(angle))/length,(-dx*math.sin(angle)+dz*math.cos(angle))/length],
                          'seat_world_before_placement':[v*.25 for v in settings['seat']]}))

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('files',type=Path,nargs='+')
    for path in p.parse_args().files:inspect(path)
