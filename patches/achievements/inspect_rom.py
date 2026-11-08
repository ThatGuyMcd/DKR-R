"""Read original ROM metadata to check numeric achievement targets; no game execution or ROM export."""
from pathlib import Path
import argparse
import hashlib
import json
import re
import struct
import tomllib

AUTH=Path(__file__).resolve().parent
ROOT=AUTH.parents[1]
def inspect(path):
 data=path.read_bytes()
 if data[:4]!=bytes.fromhex('80371240'):raise ValueError('Use the canonical big-endian ROM')
 identity=hashlib.sha1(data).hexdigest()
 starts={'0cb115d8716dbbc2922fda38e533b9fe63bb9670':969568,'6d96743d46f8c0cd0edb0ec5600b003c89b93755':970976}
 start=starts.get(identity)
 if start is None:raise ValueError('Not an original supported USA ROM')
 count=struct.unpack_from('>I',data,start)[0]
 if count!=50:raise ValueError('Unreviewed original asset LUT')
 base=start+(count+2)*4
 offsets=struct.unpack_from('>'+str(count+1)+'I',data,start+4)
 table=data[base+offsets[48]:base+offsets[49]]
 ghost_base=base+offsets[49]
 values=[]
 for index in range(0,len(table),8):
  map_id,vehicle,offset=struct.unpack_from('>BB2xI',table,index)
  if map_id==255:break
  ticks=struct.unpack_from('>H',data,ghost_base+offset+4)[0]
  values.append({'course_id':map_id,'vehicle':vehicle,'tt_ticks':ticks})
 if len(values)!=20:raise ValueError('Need all 20 original staff ghosts')
 revision='us.v77' if start==969568 else 'us.v80'
 symbols=tomllib.loads((ROOT/'modding'/('reference-'+revision.replace('.','-'))/'data.toml').read_text(encoding='utf-8'))['section']
 def rom_address(vram):
  for section in symbols:
   if section['vram']<=vram<section['vram']+section['size']:return section['rom']+vram-section['vram']
  raise ValueError('Credits pointer is outside the original data section')
 credits=next(s['vram'] for section in symbols for s in section.get('symbols',[]) if s['name']=='gCreditsBestTimesArray')
 # get_misc_asset returns word offsets, not byte offsets. Credits explicitly
 # index MAIN_TRACKS_IDS; that order differs from the Track Select grid.
 lo,hi=struct.unpack_from('>2I',data,base+offsets[16]+28*4)
 main_ids=data[base+offsets[15]+lo*4:base+offsets[15]+hi*4]
 if len(main_ids)<21 or main_ids[20]!=255:raise ValueError('Unreviewed main-course order')
 developer={}
 for course,ptr in zip(main_ids[:20],struct.unpack_from('>20I',data,rom_address(credits))):
  at=rom_address(ptr);stamp=data[at:at+8].split(b'\0',1)[0].decode('ascii')
  if not re.fullmatch(r'\d:\d\d:\d\d',stamp):raise ValueError('Unreviewed credits time')
  minutes,seconds,hundredths=map(int,stamp.split(':'));developer[course]=minutes*6000+seconds*100+hundredths
 authored={int(id):(int(vehicle),int(tt),int(dev)) for id,vehicle,tt,dev in re.findall(r'\{(\d+),(\d+),"[^"]+",(\d+),(\d+)\}',(AUTH/'native/achievements.hpp').read_text(encoding='utf-8'))}
 for row in values:
  row['developer_centiseconds']=developer[row['course_id']]
  if authored.get(row['course_id'])!=(row['vehicle'],row['tt_ticks'],row['developer_centiseconds']):raise ValueError('Catalogue differs from original game: '+str(row))
 return {'schema':1,'revision':revision,'staff_ghosts':values,'developer_source':'Original credits pointers paired with ASSET_MISC_MAIN_TRACKS_IDS, as menu_credits_loop does.','catalogue_matches':True,'validation':'Read-only original asset-header/credits metadata inspection. No game, mod or test execution; no ROM artwork or code exported.'}
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--rom',type=Path,required=True);p.add_argument('--output',type=Path);a=p.parse_args();result=inspect(a.rom)
 if a.output:a.output.write_text(json.dumps(result,indent=2)+'\n')
 print(json.dumps(result,indent=2))
