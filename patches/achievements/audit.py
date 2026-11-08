"""Read-only catalogue, integration and linked-byte inspection. Never runs the game or tests."""
from pathlib import Path
import argparse
from collections import Counter
import hashlib
import json
import re
AUTH=Path(__file__).resolve().parent
ROOT=AUTH.parents[1]
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def require(value,message):
 if not value:raise ValueError(message)
def catalogue():
 text=(AUTH/'native/achievements.hpp').read_text(encoding='utf-8')
 courses=[(int(a),int(b),c,int(d),int(e)) for a,b,c,d,e in re.findall(r'\{(\d+),(\d+),"([^"]+)",(\d+),(\d+)\}',text)]
 require(len(courses)==20 and len({row[0] for row in courses})==20,'Need all 20 distinct original time-trial courses')
 entries=[]
 def add(id,rule,**values):entries.append({'id':id,'rule':rule,**values})
 add('first-balloon','New adventure balloon: total crosses 1')
 add('banana-ten','Active human racer crosses 10 held bananas')
 add('tt-amulet','Adventure T.T. amulet reaches four pieces')
 add('wizpig-amulet','Adventure Wizpig amulet reaches four pieces')
 for i,name in enumerate(('Ancient Lake','Crescent Island','Snowball Valley','Boulder Canyon')):add('key-'+str(i),'New key bit',course=name,mask=1<<(i+1))
 for total in (39,47):add('balloons-'+str(total),'New adventure balloon crosses threshold',target=total)
 for i,name in enumerate(('Dino Domain','Sherbet Island','Snowflake Mountain','Dragon Forest','Future Fun Land')):add('trophy-'+str(i),'Original adventure trophy reaches gold',world=name,shift=i*2)
 add('wizpig-one','New first Wizpig victory',mask=1)
 add('wizpig-two','New second Wizpig victory',mask=32)
 add('mirrored-wizpig','New second Wizpig victory in Adventure Two',mask=32)
 add('mirrored-47','New Adventure Two balloon crosses 47',target=47)
 for id,vehicle,name,tt,developer in courses:
  add('tt-'+str(id),'Completed original three-lap non-mirrored time trial strictly under the original staff ghost time',course=name,course_id=id,vehicle=vehicle,native_ticks=tt)
  add('rare-'+str(id),'Completed original three-lap non-mirrored time trial strictly under developer credits time',course=name,course_id=id,vehicle=vehicle,developer_centiseconds=developer)
  add('silver-'+str(id),'Same original Adventure Two race: eight coins and first place',course=name,course_id=id)
 for i,name in enumerate(('Boost','Rocket','Trap','Shield','Magnet')):add('power-'+str(i),'Active human obtains level two (zero-based) with positive ammunition',power=name,power_type=i)
 for i,name in enumerate(('Krunch','Bumper','Tiptup','Conker','Timber','Banjo','Drumstick','Pipsy','T.T.','Diddy Kong')):add('driver-'+str(i),'Five newly earned adventure balloons without leaving Adventure, changing save, or closing the session',base_driver=name,base_driver_id=i)
 require(len(entries)==94 and len({e['id'] for e in entries})==94,'Catalogue coverage is not exactly 94 unique goals')
 return entries
def inspect(stage,binary=None):
 entries=catalogue();swaps=json.loads((stage/'achievement-swaps.json').read_text(encoding='utf-8'))
 require(Counter(row['target'] for row in swaps)==Counter({'DKRPortGame':4,'DKRExperimentalOwnedGameV77':1,'DKRExperimentalOwnedGameV80':1,'DKRRecompiledFuncsV77':1,'DKRRecompiledFuncsV80':1}),'Incomplete integration target coverage')
 for row in swaps:
  require(not Path(row['new']).resolve().is_relative_to(ROOT),'A staged output is inside the checkout')
  require(sha(Path(row['path']))==row['input_sha256'],'An upstream input changed')
  require(sha(Path(row['new']))==row['output_sha256'],'A staged output changed')
 for path in (AUTH/'native').iterdir():require(sha(path)==sha(stage/path.name),'An authored achievement file was not staged: '+path.name)
 core=(stage/'achievements.cpp').read_text(encoding='utf-8');ui=(stage/'achievement_ui.inl').read_text(encoding='utf-8');driver=(stage/'probe_driver.cpp').read_text(encoding='utf-8')
 for kind in ('Balloons','Key','Amulet','Trophy','Boss','Silver','TimeTrial','Developer','Power','Driver'):
  require('case Kind::'+kind+':' in core,'Missing evaluator '+kind)
 for marker in ('class FileLease','MoveFileExW','::fsync','8192','r.address==old.address','r.player<0','r.coins==8&&r.position==1','std::int64_t(r.total_ticks)*100','previous_.balloons<d.target','driver_[human->character]','Tracking paused for developer tools','restart.exchange(false)'):
  require(marker in core,'Missing tracking/storage guard: '+marker)
 require('ram_[' in core and not re.search(r'ram_\[[^;]+\]\s*=(?!=)',core),'Observer writes guest memory')
 require('capture(ram_.data(),ram_.size(),DKR_PROBE_REVISION)' in driver,'Owned snapshot is not from the correct RAM owner')
 require(driver.index('if(scene_sessions_)dkr::runtime::achievements::submit')>driver.index('++confirmed_count_'),'Online awards are not confirmation-only')
 for marker in ('DrawAchievementsPage','DrawAchievementToast','DrawPinnedAchievement','RequestCustomTextKeyboard("Find an achievement"','DrawAchievementBadge','AchievementChime','SetKeyboardFocusHere','ImGui::Button("##open"','BeginPaddockModalWindow'):
  require(marker in ui,'Missing controller/presentation feature: '+marker)
 for rev in (77,80):
  rows=[r for r in swaps if r['target']=='DKRRecompiledFuncsV'+str(rev)]
  require(len(rows)==1 and Path(rows[0]['new']).read_text(encoding='utf-8').count('dkr_achievements_observe(rdram,'+str(rev)+');')==1,'Missing unique original-frame observer')
 if binary:
  data=binary.read_bytes()
  for marker in (b'1.0.9-achievements-playtest.1',b'collection=94 local-only v1',b'ACHIEVEMENTS',b'ACHIEVEMENT UNLOCKED',b'Recognise existing progress',b'Mirror Master',b'Challenge objectives inspired by Delmaru'):
   require(marker in data,'Achievement feature missing from linked executable: '+repr(marker))
 return {'schema':1,'catalogue_version':'dkr-r-94-v1','count':94,'entries':entries,'swaps':swaps,'source_files':{p.name:sha(p) for p in sorted((AUTH/'native').iterdir())},'binary_sha256':sha(binary) if binary else None,'validation':'Compilation/static inspection only. No game, mod, test or AppImage executed. Owner gameplay acceptance pending.'}
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--stage',type=Path,required=True);p.add_argument('--binary',type=Path);p.add_argument('--output',type=Path);a=p.parse_args();report=inspect(a.stage,a.binary)
 if a.output:a.output.write_text(json.dumps(report,indent=2)+'\n')
 else:print(json.dumps(report,indent=2))
