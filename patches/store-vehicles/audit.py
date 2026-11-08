"""Vehicle Store source/binary/archive receipts only. No game/test execution."""
from pathlib import Path
import argparse
import hashlib
import importlib.util
import json
import re
import sys
sys.dont_write_bytecode=True
AUTH=Path(__file__).resolve().parent
ROOT=AUTH.parents[1]
VERSION='1.0.9-spectator-playtest.5'
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def need(ok,message):
 if not ok:raise ValueError(message)
def load(name,path):
 s=importlib.util.spec_from_file_location(name,path);m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m
def inspect(stage,binary=None,baseline=None):
 base=load('vehicle_store_rollback_audit',ROOT/'patches/rollback-only/audit.py');base.VERSION=VERSION
 retained=base.inspect(stage.parent/'rollback-only',binary,baseline)
 rows=json.loads((stage/'swaps.json').read_text());includes=json.loads((stage/'includes.json').read_text())
 need(len(rows)==3 and {Path(r['new']).name for r in rows}=={'runtime_ui.cpp','mod_store.cpp','mod_store_install.cpp'},'Unexpected Vehicle Store source replacements')
 for row in rows+includes:
  old,new=Path(row['path']),Path(row['new'])
  need(not new.resolve().is_relative_to(ROOT),'Vehicle Store output escaped the external stage')
  need(sha(old)==row['input_sha256'] and sha(new)==row['output_sha256'],'Vehicle Store source receipt changed: '+new.name)
 service=(stage/'mod_store.cpp').read_text();installer=(stage/'mod_store_install.cpp').read_text();ui=(stage/'runtime_ui.cpp').read_text()
 need(service.count('".dkrkart", ".dkrhover", ".dkrplane"')==2,'Download/resume format allowlists do not both accept vehicles')
 need('prepare_vehicle_store_upload(source,p.root/"vehicle-uploads",stop)' in service,'Standalone vehicle uploads do not use the compatible ZIP wrapper')
 for marker in ('prepare_store_vehicle(proposed.file,transaction/"payload",stop)', 'protect("mods/vehicles/library.json")',
                'publish_store_vehicles(config,prepared,old.value("members",Json::array()),other_owners,stop)',
                'remove_store_vehicle(config,member,stop)', 'historical_legacy_archive'):
  need(marker in installer,'Vehicle transaction/compatibility gate is missing: '+marker)
 need('prepared.native.value("vehicle",false)' in installer and 'kind == "vehicle"' in installer,'Vehicle receipts are missing native members')
 need('if(s=="mods/vehicles/library.json") return true;' in installer and 'leaf_after("mods/vehicles/portraits/")' in installer,'Vehicle recovery paths were not allowlisted narrowly')
 need('"vehicle:"+digest' in installer,'Vehicle receipt identity is not checked against its content-addressed path')
 need('g_vehicle_library' not in installer,'Background installer depends on frontend objects')
 need('#include "gui/rt64_inspector.h"' in ui,'Renderer include order was bypassed')
 inls={Path(r['new']).name:Path(r['new']).read_text() for r in includes}
 for leaf in ('mod_store_ui.inl','mod_store_management_ui.inl','store_player_library.inl','store_player_activity.inl','store_staff_pages.inl','vehicle_ui.inl'):
  need(leaf in inls,'Vehicle Store UI integration is missing: '+leaf)
 store=inls['mod_store_ui.inl'];manager=inls['mod_store_management_ui.inl']
 need('"Vehicles"' in store and '"Legacy / ROM hacks"' not in store and '"Legacy", "Magic Codes"' not in store,'Legacy category labels remain visible')
 need(all('Legacy / ROM hacks' not in content for content in inls.values()),'A Store subpage still uses the old category label')
 need('{"", "tracks", "characters", "legacy", "magic", "textures"}' in store,'Published category IDs were changed')
 need('for (const auto& car : StoreVehicleInventory())' in manager and 'vehicles != last_vehicles' in manager and 'StoreRefreshVehicles();' in manager,'Native inventory/refresh can lose Installed status')
 need('!StoreVehiclesBusy()' in manager and 'StoreVehiclesBusy()' in store,'Vehicle import and Store commit can run together')
 cpp=(AUTH/'native/store_vehicle_adapter.cpp').read_text()
 for marker in ('staging.prepare_import(source)','staging.import_prepared(batch, cars::ImportPolicy::KeepBoth)',
                'library.import_prepared(batch, cars::ImportPolicy::KeepBoth)','owned_elsewhere(digest, other_owners)',
                'library.enable(p->digest, enabled)','library.remove(digest)','if(zip.size()>cars::MaxPackageBytes)',
                'u32(0x04034b50)','u32(0x02014b50)','u32(0x06054b50)'):
  need(marker in cpp,'Shared importer/update/upload safeguard is missing: '+marker)
 contract=hashlib.sha256();native=[]
 for p in sorted([AUTH/'pipeline.py',AUTH/'transforms.py',AUTH/'ReleaseOverlay.cmake',*(AUTH/'native').iterdir()],key=lambda p:p.name):
  if not p.is_file():continue
  native.append({'name':p.name,'sha256':sha(p)})
  contract.update(p.name.encode());contract.update(p.read_bytes().replace(b'\r\n',b'\n'))
  if p.parent.name=='native':
   content=(stage/p.name).read_text()
   if p.name=='store_vehicle_adapter.hpp':content=re.sub(r'#include "[^"\n]+mod_store_install.hpp"','#include "mod_store_install.hpp"',content)
   need(content==p.read_text(),'Native Store adapter differs from authored source: '+p.name)
 if binary:
  data=binary.read_bytes()
  for marker in (b'Validating vehicle models, portraits and driver seats',b'Updating the vehicle library',
                 b'Choose Vehicles before uploading a vehicle export.',b'The vehicle library did not publish every prepared car.'):
   need(marker in data,'Vehicle Store linked feature marker missing: '+repr(marker))
  need(b'Legacy / ROM hacks' not in data,'Old Store category label remains linked')
 return {'schema':1,'version':VERSION,'vehicle_store_contract':contract.hexdigest(),'authored':native,'swaps':rows,'includes':includes,
  'retained':retained,'validation':'Compilation/linking and static source/binary/archive inspection only. No game, mod, test executable or AppImage was launched.'}
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--stage',type=Path,required=True);p.add_argument('--binary',type=Path);p.add_argument('--baseline',type=Path);p.add_argument('--output',type=Path)
 a=p.parse_args();r=inspect(a.stage,a.binary,a.baseline)
 if a.output:a.output.write_text(json.dumps(r,indent=2)+'\n')
 else:print(json.dumps(r,indent=2))
