"""Supply an unchanged frozen relative include to the Android Store stage."""
from pathlib import Path
import argparse,hashlib,json,shutil
AUTH=Path(__file__).resolve().parent
ROOT=AUTH.parents[2]

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('source','generated','output'):p.add_argument('--'+name,type=Path,required=True)
    a=p.parse_args();headers=[]
    for ui in a.generated.rglob('mod_store_ui.inl'):
        if '#include "store_account_page.inl"' not in ui.read_text():continue
        target=ui.parent/'store_account_page.inl'
        if target.resolve().is_relative_to(ROOT):raise ValueError('Generated account input escaped external build')
        if target.exists() and target.read_bytes()!=a.source.read_bytes():raise ValueError('Conflicting retained account input')
        shutil.copy2(a.source,target)
        headers.append({'source':a.source.as_posix(),'path':target.as_posix(),'sha256':hashlib.sha256(target.read_bytes()).hexdigest()})
    if len(headers)!=1:raise ValueError('Expected one retained Android account page')
    # The Android Store adapter previously staged implementation units only.
    # Newer vehicle includes name the staged headers explicitly. Supply their
    # exact frozen public declarations alongside those implementation units.
    for name in ('mod_store.hpp','mod_store_install.hpp'):
        source=a.source.parent/name
        if not source.is_file():raise ValueError('Missing frozen Store declaration: '+name)
        target=a.generated.parent/'release-store'/name
        if target.resolve().is_relative_to(ROOT):raise ValueError('Store declaration escaped external build')
        if target.exists() and target.read_bytes()!=source.read_bytes():raise ValueError('Conflicting staged Store declaration')
        shutil.copy2(source,target)
        headers.append({'source':source.as_posix(),'path':target.as_posix(),'sha256':hashlib.sha256(target.read_bytes()).hexdigest()})
    a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(json.dumps(headers,indent=2)+'\n')
    print('Frozen account-page bytes supplied beside the Android Store relative include.')
