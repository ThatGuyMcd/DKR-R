"""Compile a manually reviewed mesh-component selection to digest-bound face masks.

Input is the static inspector's model/component inventory, NOT a running game.
Only digest/count/range metadata is emitted; no model, texture or animation bytes.
Component numbers below were reviewed in side/front/top and isolated-mesh views.
No material name, character name, position cutoff or animation heuristic decides
ownership at runtime. Relocatable 3D texture IDs alone are zeroed before hashing.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
from audit_characters import Bank,model,u32,u16

# Original unrelocated model SHA -> explicit car components. Everything else
# stays visible, including Yoshi's tail and Haunter's complete distant proxy.
REVIEW={
 '8ffdfdf96e062e12daeeb4b84552247722ef96d9e93df8486be1ee9d6059670d':list(range(12,18)),
 'cc108aa2f79f4f59966c1aee844b2483f2ee51e1f87ae39fe1cc77e74dc8b5b2':list(range(15,22)),
 '7eab6298cbc4f77be15c41462e323808c7e3c9abfb83346133271a8256069bc3':list(range(14,20)),
 '4cb773309c9177b263f71c2eb40459caaac92e63a8c7aaa0c44a9bdbe52e570b':list(range(10,18)),
 '5842a9e01900761ced8ddd780d4b800fef0abfde4c08fdf521535fa2db29b0c5':list(range(10,18)),
 '3fb75a5fb1a00a50b7d6200d463319678343b7c359999e9ac4917e6b83be8cef':list(range(10,18)),
 '32ef11a573277f27eebbd7728b90fe521e3b5622539de5511be6f325a2430e96':list(range(8,16)),
 '66ec2e27e5ed9625d607e6d629caba8cc18c30533768f054e96f44222e14b314':[0,1,2,3,4,6,7,8],
 '4eff4f99308c41979f7ad72b1bfed7616068f3b48a6667e9160fdb6f967c7b3d':[0,1,2,3,4,6,7,8],
 'b3795843a7319fd5da061291336647f1fdfffbda33a59b3de17a3754d7c178a7':[0,1,2,3,4,6,7,8,9],
 '2c0f126449c7d765984d7964f8ba068fb1798fa2fa0c33ff9bf3217fec7b62b6':[7,9,11,12,15,22,23],
 '920445296ed207d8c3395187eee2c8d34492e1b85ee32c256d256e80cfac7155':[7,9,11,12,15,22,23],
 '099747cee7125b82d73244d07417b9aecbbb91e18fdfb015d127296deffaafb6':[7,9,11,12,15,22,23],
 '1f3a48cec420c3d848fa342fbac424a0214d732f626c95ac7da5c423b028d274':[7,9,11,12,15,22],
 '5d70abf0a3267e85df5a5430280317de5cc20d38e0bee674168f9f5de3a937d6':[1,2,3,4,5,6],
 '7c7388f14eb940928c135c1c8ffc3439ff77faf3114a4480321e497f6ca54ef8':list(range(13))+[22],
 '7645f878095c44e8670df8139f362a3935623b68b82da09a17ff62e5d748dcc4':list(range(13))+[19],
 '7d3a5f78e26fd2558c9b92004fec91cb6e94eacc7fd9152b0421dfd7f6360efb':list(range(13)),
 '017bf96e4cfbe78ff5606afea89f8e17255e3d53a3c07c7568eb97ce8fce1fd0':list(range(13)),
 '9e9612008e3c7e4d4e5471ccde29cc8044a05dd8b5ca644a48dc403aff915208':list(range(9)),
 '0b98a540d65084861140cd5a6945dd56818017d8cb870b4bce906e83f5d71c3a':[0,1,2,3,6],
 '3f869a12a48c7f150447d49efa89e26c7c2612fe9b4e5336f6c6223592cffc57':list(range(2,11)),
 '53954fb2764207322fa866725538e6dd401e415370dcb3b7696c621e36b3a776':list(range(2,10)),
 'edfa394f0ff5b8d04e5fb8fedad9205fd84fcc8ba322ac201351c9f6f707b952':list(range(2,10)),
 '214eae73908fe8347df2d9d227a761f0d7965574302ab5dffaa6871ac0de0767':list(range(2,8)),
 'af2e634ea45dbc1ff34ecc64d8ce40b93e2fac8427d35f22306e4c92c9ea28b2':list(range(3,12)),
 'dfdaeabe38a674e26d4f6f267016b493d302e0c6ba20955bdc9e7179f32b94a5':list(range(3,12)),
 '01d96ca19b58e2e6b8ae99e1652787872f7e667c3621be6d8f7e37620cedd445':list(range(3,11)),
 '7ac8d3c7736b4f8c4c4efd6865602e8e92a43748f9276e26d6bb700e7ad07791':list(range(3,11)),
 '1e3ba550a6afa81d2c3dbd0984c4468e09667627ed740ae2a89f1934f34dbbef':list(range(3,9)),
 '0f822e66f7d8416639c7649673ea625cf648f33df4a5f148d9654f2172707eba':[0,9,10,11,12,13,14],
 '50203f6753b61c19c959cba66ea27efd4870577c8eddf2ed6ed7f1a244754162':[0,4,5,6,7,8,9],
 '0c4fa51902f0743b5944979f1fb26bcb89142cdb3586ba1c95bcb30d19527239':[],
}

def ownership_digest(raw):
    normalized=bytearray(raw);tx=u32(raw,0)
    for t in range(u16(raw,0x22)):struct.pack_into('>I',normalized,tx+t*8,0)
    return hashlib.sha256(normalized).hexdigest()

def main(a):
    inventory=json.loads(a.components.read_text());bank=Bank(a.rom)
    paths={p.name:p for d in a.library.rglob('blobs') if d.is_dir() for p in d.iterdir() if p.is_file()}
    needed={};rows=[]
    for f in a.library.glob('prepared-characters/*/*/character.json'):
        doc=json.loads(f.read_text())
        for r in doc['records']:
            if r['section']!=29:continue
            packed=paths[r['hash']].read_bytes() if r['hash'] in paths else bank.record(29,r['record'])
            if hashlib.sha256(packed).hexdigest()!=r['hash']:raise ValueError('Missing immutable input')
            m=model(packed);raw_digest=hashlib.sha256(m['raw']).hexdigest()
            if raw_digest in REVIEW:needed[raw_digest]=m
    for digest,selected in REVIEW.items():
        m=needed[digest];entry=inventory[digest];hidden=sorted(t for c in selected for t in entry['components'][c]['triangles'])
        if len(hidden)!=len(set(hidden)) or len(hidden)>=m['triangles']:raise ValueError('Overlapping mask or no driver retained')
        ranges=[]
        for t in hidden:
            if ranges and t==ranges[-1][1]:ranges[-1][1]+=1
            else:ranges.append([t,t+1])
        rows.append({'name':entry['name'],'lod':entry['lod'],'source_sha256':digest,
                     'ownership_sha256':ownership_digest(m['raw']),'vertices':m['vertices'],
                     'triangles':m['triangles'],'hide':ranges,'hidden':len(hidden),'retained':m['triangles']-len(hidden)})
    header='#pragma once\n// Static reviewed ownership metadata only; no game/model/texture bytes.\n// SHA covers all decompressed model bytes except relocatable texture IDs.\n#include <map>\n#include <string>\n#include <utility>\n#include <vector>\nnamespace dkr::mods::vehicles {\nstruct ReviewedCharacterMask { unsigned vertices,triangles; std::vector<std::pair<unsigned,unsigned>> ranges; };\ninline const std::map<std::string,ReviewedCharacterMask> ReviewedCharacterMasks{\n'
    for r in rows:
        header+='    // '+r['name']+' LOD'+str(r['lod'])+'; keep '+str(r['retained'])+' driver faces.\n'
        header+='    {"'+r['ownership_sha256']+'",{'+str(r['vertices'])+','+str(r['triangles'])+',{'+','.join('{'+str(x)+','+str(y)+'}' for x,y in r['hide'])+'}}},\n'
    a.output.write_text(header+'};\n}\n')
    a.output.with_suffix('.json').write_text(json.dumps(rows,indent=2)+'\n')
    print('Reviewed '+str(len(rows))+' distinct model variants; no game/mod execution.')

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for arg in ('rom','library','components','output'):p.add_argument('--'+arg,type=Path,required=True)
    main(p.parse_args())
