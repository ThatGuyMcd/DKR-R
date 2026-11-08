"""Static source, receipt and linked-byte inspection. Never shadows stdlib inspect."""
from pathlib import Path
import argparse
import hashlib
import json
AUTH=Path(__file__).resolve().parent
ROOT=AUTH.parents[1]
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def require(ok,why):
    if not ok:raise ValueError(why)
def inspect(stage,binary=None):
    swaps=json.loads((stage/'dump-swaps.json').read_text())
    require({(r['target'],Path(r['path']).name) for r in swaps}=={('DKRPortGame','runtime_ui.cpp'),('rt64','rt64_rdp_tmem.cpp')},'Unexpected texture-dump replacement targets')
    for row in swaps:
        require(sha(Path(row['path']))==row['input_sha256'],'A read-only source input changed')
        require(sha(Path(row['new']))==row['output_sha256'],'A staged observation/UI source changed')
        require(not Path(row['new']).resolve().is_relative_to(ROOT),'A replacement escaped the isolated build')
    for path in (AUTH/'native').iterdir():
        require(sha(stage/path.name)==sha(path),'Unstaged texture authoring input: '+path.name)
    ui=(stage/'runtime_ui.cpp').read_text();observer=(stage/'rt64_rdp_tmem.cpp').read_text()
    prior=next(Path(r['path']).read_text() for r in swaps if r['target']=='rt64')
    require(ui.count('#include "texture_dump_ui.inl"')==1,'Texture section not composed exactly once')
    dump_ui=(stage/'texture_dump_ui.inl').read_text()
    for marker in ('DetailSectionSign("TEXTURE DUMPING##texture-dump",g_dump_open,width)',
                   'DrawTexturesPageWithoutDump(width);','DumpPanel::Export','DumpPanel::Capture','DumpPanel::Dumps',
                   'SettingsColumns columns','SettingsCheck(', 'SettingsButton(label,size',
                   'RequestCustomTextKeyboard("Find a texture"','DrawTextEntryKeyboard();ImGui::EndPopup();'):
        require(marker in dump_ui,'Missing shared-style dumping UI contract: '+marker)
    require('"TEXTURE PACKS' not in dump_ui and '"CRT OVERLAYS' not in dump_ui,
            'Dumping must not replace the existing pack/CRT sections')
    for marker in ('BeginPaddockModalWindow("Texture browser##dump"','ImGuiListClipper clipper',
                   'TrimDumpPreviews();','item.preview_key','"Go to page"','ImGuiKey_GamepadR1','dumping::close_gallery()'):
        require(marker in dump_ui,'Missing fast rounded browser UI: '+marker)
    require(observer.count('queueGPUUploadTMEM(')==prior.count('queueGPUUploadTMEM('),'GPU upload count changed')
    require(observer.count('dumpTexture(')==prior.count('dumpTexture('),'Existing developer dump path changed')
    require('if(gate&&gate(nativeHash))' in observer,'Capture does extra work before its unique-texture gate')
    require('sink(tile,TMEM);' in observer,'Original texture observation missing')
    core=(stage/'texture_dump.cpp').read_text()
    for marker in ('std::try_to_lock','std::condition_variable','read_character_artifact','read_track_artifact','custom_tracks::tracks()',
                   'car_assets::Library','read_png','source_rice','frame<count','MoveFileExW','is_symlink','read_manifest',
                   'Source_Artwork/','std::make_shared<View>(s.view)','Texture-Starter.zip','MZ_ZIP_FLAG_WRITE_ZIP64'):
        require(marker in core,'Missing planned texture service feature: '+marker)
    require('RDRAM' not in core,'Texture writer must not read guest memory')
    require('source_artwork' in core and 'geometry.json' in core,'Source/native artwork separation lost')
    browser=(stage/'texture_gallery.inl').read_text()
    for marker in ('read_manifest(root,false)','manifest_stamp!=stamp_','member(root_,item.path)',
                   'preview_capacity=128','previews_.insert_or_assign','[texture-browser] page=',
                   'gallery_stamp(path)!=stamp','item.preview_error=e.what()'):
        require(marker in browser,'Missing read-only cached gallery contract: '+marker)
    require('Workspace w(' not in browser and 'atomic_file(' not in browser,'Browser must not construct a writer or write workspace files')
    for marker in ('gallery_request=GalleryRequest','gallery_serial==browser->serial','view.gallery_loading=false',
                   'bool validate_members=true','if(validate_members)for('):
        require(marker in core,'Missing browser isolation/cancellation safety: '+marker)
    if binary:
        data=binary.read_bytes()
        for marker in (b'[texture-dump] presentation-only PNG capture enabled',b'TEXTURE DUMPING',b'CAPTURE NEXT GAME',b'Texture-Starter.zip',b'dkr-r-texture-dump',b'[texture-browser] page=',b'Go to page'):
            require(marker in data,'Texture feature missing from linked binary: '+repr(marker))
    return {'schema':1,'sources':[{'name':p.name,'sha256':sha(p)} for p in sorted((AUTH/'native').iterdir())],
        'replacements':swaps,'binary_sha256':sha(binary) if binary else None,
        'validation':'Static inspection only; no game, texture export job, mod, test or AppImage executed.'}
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--stage',type=Path,required=True);p.add_argument('--binary',type=Path);p.add_argument('--output',type=Path);a=p.parse_args();report=inspect(a.stage,a.binary)
    if a.output:a.output.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2) if not a.output else 'Texture dump sources and linked markers inspected; no target execution.')
