"""Read-only composition/build-contract audit. Does not execute game code."""
from pathlib import Path
import argparse
import hashlib
import html
import json
import os
import re

def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def local(p):
    p=p.replace('\\','/')
    if os.name!='nt' and re.match(r'^[A-Za-z]:/',p):return Path('/mnt/'+p[0].lower()+p[2:])
    return Path(p)
def ids(p):
    text=html.unescape(p.read_text(errors='replace')).replace('\\"','"')
    result=dict(re.findall(r'DKR_OWNED_BUILD_ID_V(77|80)="([0-9a-f]{64})"',text))
    if set(result)!={'77','80'}:raise ValueError('Missing build identities: '+str(p))
    return result
def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--stage',type=Path,required=True)
    p.add_argument('--windows-project',type=Path)
    p.add_argument('--linux-ninja',type=Path)
    a=p.parse_args();receipt=json.loads((a.stage/'shared-saves-receipt.json').read_text())
    for path,expected in receipt['files'].items():
        if sha(a.stage/path)!=expected:raise ValueError('Stage changed: '+path)
    for path,expected in receipt['inputs'].items():
        if sha(local(path))!=expected:raise ValueError('An accepted baseline input changed: '+path)
    game=a.stage/'native/src/game'
    launch=(game/'mods/legacy_mod_launch.cpp').read_text()
    if 'out->save_subfolder.clear();' not in launch or 'Preparing separate modded-session saves' in launch:raise ValueError('Offline save forks remain')
    manager=(game/'runtime_saves_ui.inl').read_text()
    if 'codec::normalise_editable_fields(image);' in manager:raise ValueError('Editor still normalises on open')
    ui=(game/'runtime_ui.cpp').read_text()
    if 'Legacy mod selections keep separate saves.' in ui:raise ValueError('Outdated save instructions remain')
    configure=ui[ui.index('void dkr::runtime::ui::configure'):]
    configure=configure[:configure.index('dkr::runtime::magic_codes::configure')]
    if 'repair_adventure_checksums(' in configure:raise ValueError('Automatic checksum repair remains')
    backend=(game/'shared_save_backend.inl').read_text()
    for required in ('std::copy(original.begin()+0x80','read-back verified','revision()!=expected','recover_unlocked','journal["state"]="committed"'):
        if required not in backend:raise ValueError('Missing save guard: '+required)
    startup=(game/'game_main.cpp').read_text()
    if startup.index('shared::prepare_development(')>startup.index('saves::prepare_runtime_save_context('):raise ValueError('Development seed must precede runtime ownership')
    for required in ('if(exists&&!refresh)','ReadAdventure(target,verified)','queue_development_copy','development-original-','source==previous'):
        if required not in backend:raise ValueError('Missing development save guard: '+required)
    owned=(a.stage/'native/tests/replay_probe/probe_driver.cpp').read_text()
    if 'dkr::runtime::dkr::' in owned:raise ValueError('Invalid qualification in staged code')
    for required in ("{'D','K','R','C'}",'confirmed_records_.assign','records::install(*mods_','records::valid(course_records)'):
        if required not in owned:raise ValueError('Missing confirmed-record boundary: '+required)
    identities={}
    for platform,path in (('windows',a.windows_project),('linux',a.linux_ninja)):
        if path:identities[platform]=ids(path)
    if len(identities)==2 and identities['windows']!=identities['linux']:raise ValueError('Windows/Linux rollback contracts differ')
    print(json.dumps({'inspection':'Read-only source, immutable input and build-contract audit; no runtime execution',
        'staged_files':len(receipt['files']),'accepted_inputs_unchanged':len(receipt['inputs']),
        'version':receipt['version'],'rollback_build_ids':identities},indent=2))
if __name__=='__main__':main()
