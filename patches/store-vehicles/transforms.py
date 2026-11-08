"""Vehicle Store adapters; prior importers and published category IDs stay intact."""
from pathlib import Path
AUTH=Path(__file__).resolve().parent

def once(text, before, after):
    if text.count(before)!=1:raise ValueError('Vehicle Store anchor changed: '+before[:145])
    return text.replace(before,after,1)

def service(text):
    text=once(text,'resume_id, epoch, credential](std::stop_token stop) {',
        'resume_id, epoch, credential](std::stop_token stop) mutable {')
    for marker in ('formats','allowed'):
        start=text.index('const std::set<std::string> '+marker+' = {".zip"')
        end=text.index('};',start)
        block=text[start:end]
        text=text[:start]+block.replace('".7z"','".7z", ".dkrkart", ".dkrhover", ".dkrplane"')+text[end:]
    anchor='        const auto size = std::filesystem::file_size(source);'
    return once(text,anchor,'''        if(kind=="download" && vehicle_store_extension(lower(source.extension().string()))) {
            if(!vehicle_store_category(category)) throw Error("Choose Vehicles before uploading a vehicle export.");
            source=prepare_vehicle_store_upload(source,p.root/"vehicle-uploads",stop);
        }
'''+anchor)

def installer(text):
    text=once(text,'    const auto code = leaf_after("mods/custom-magic-codes/packages/");','''    const auto car = leaf_after("mods/vehicles/packages/");
    for(const auto* suffix : {".dkrkart", ".dkrhover", ".dkrplane"})
        if(car.ends_with(suffix) && hash_string(car.substr(0, car.size()-std::char_traits<char>::length(suffix)))) return true;
    const auto portrait = leaf_after("mods/vehicles/portraits/");
    if(portrait.ends_with(".rgba") && hash_string(portrait.substr(0,portrait.size()-5))) return true;
    if(s=="mods/vehicles/library.json") return true;
    const auto code = leaf_after("mods/custom-magic-codes/packages/");''')
    text=once(text,'    const auto kind = text(member, "kind", 40);\n    return (kind == "texture"',
        '''    const auto kind = text(member, "kind", 40);
    if(kind=="vehicle") {
        const auto digest=text(member,"digest",64);
        if(!hash_string(digest)||text(member,"id",256)!="vehicle:"+digest||text(member,"key",512)!="vehicle:"+digest) return false;
        for(const auto* suffix:{".dkrkart",".dkrhover",".dkrplane"})
            if(path=="mods/vehicles/packages/"+digest+suffix) return true;
        return false;
    }
    return (kind == "texture"''')
    text=once(text,'    bool tracks = false, textures = false, legacy_tracks = false, characters = false, code = false;',
        '    bool tracks = false, textures = false, legacy_tracks = false, characters = false, code = false, vehicles = false;')
    text=once(text,'        code |= kind == "code";', '        code |= kind == "code";\n        vehicles |= kind == "vehicle" || kind == "vehicle-portrait";')
    text=once(text,'    if (code)\n        protect("mods/custom-magic-codes/library.json");',
        '    if (vehicles) protect("mods/vehicles/library.json");\n    if (code)\n        protect("mods/custom-magic-codes/library.json");')
    anchor='    if (extension == ".nrm" || extension == ".dkrcode") {'
    text=once(text,anchor,'''    const bool legacy_patch=extension==".xdelta"||extension==".bps"||extension==".ips";
    bool historical_legacy_archive=false;
    if(vehicle_store_category(proposed.category)&&extension==".zip")
        for(const auto& receipt:registry["records"]) for(const auto* edition:{&receipt,receipt.contains("previous")?&receipt["previous"]:&receipt})
            if(text(*edition,"digest",64)==proposed.digest)
                for(const auto& m:edition->value("members",Json::array()))
                    historical_legacy_archive |= text(m,"kind",40).starts_with("legacy-");
    if(vehicle_store_extension(extension)||(vehicle_store_category(proposed.category)&&!legacy_patch&&!historical_legacy_archive)) {
        if(!vehicle_store_category(proposed.category)) throw Failure("This vehicle is listed under the wrong Store category. Ask its publisher to choose Vehicles.");
        progress(.05F,"Validating vehicle models, portraits and driver seats");
        prepared=prepare_store_vehicle(proposed.file,transaction/"payload",stop);
    } else if (extension == ".nrm" || extension == ".dkrcode") {''')
    # Old explicit ROM patch downloads remain supported; vehicle ZIPs no longer
    # fall through to ROM patch preparation or require a source ROM.
    text=once(text,'proposed.category == "characters" || proposed.category == "legacy" || extension == ".xdelta"',
        'proposed.category == "characters" || historical_legacy_archive || extension == ".xdelta"')
    text=once(text,'(category == "characters" && kind != "legacy-character") ||',
        '(category == "characters" && kind != "legacy-character") ||\n                    (vehicle_store_category(category) && kind != "vehicle") ||')
    text=once(text,'        const bool possible = name_key(text(local, "name", 200)) == name_key(proposed.title);',
        '''        bool possible = name_key(text(local, "name", 200)) == name_key(proposed.title);
        if(text(local,"kind",40)=="vehicle") for(const auto& incoming:prepared.native.value("members",Json::array()))
            possible |= text(incoming,"kind",40)=="vehicle" && incoming.value("type",0U)==local.value("type",0U) &&
                name_key(text(incoming,"name",240))==name_key(text(local,"name",240));''')
    text=once(text,'{"canRunCopies", proposed.category == "textures"}',
        '{"canRunCopies", proposed.category == "textures" || prepared.native.value("vehicle",false)}')
    text=once(text,'const bool stored = (mode == "add" && proposed.category != "textures") ||',
        'const bool stored = (mode == "add" && proposed.category != "textures" && !prepared.native.value("vehicle",false)) ||')
    # Both immutable vehicle artifacts are guarded like the code packages.
    text=once(text,'(kind == "legacy-group" || kind == "legacy-review" || kind == "code") &&',
        '(kind == "legacy-group" || kind == "legacy-review" || kind == "code" || kind == "vehicle" || kind == "vehicle-portrait") &&')
    anchor='        // Code admission uses the existing verifier and trust model.'
    text=once(text,anchor,'''        if(prepared.native.value("vehicle",false)) {
            progress(.92F,"Updating the vehicle library");
            Json other_owners=Json::array();
            for(const auto& r:registry["records"]) if(text(r,"instance",32)!=instance&&!r.value("stored",false))
                for(const auto& m:r.value("members",Json::array())) other_owners.push_back(m);
            members=publish_store_vehicles(config,prepared,old.value("members",Json::array()),other_owners,stop);
        }
'''+anchor)
    text=once(text,'if (prepared.native.contains("members") && !prepared.native.contains("codeId")) {',
        'if (prepared.native.contains("members") && !prepared.native.contains("codeId") && !prepared.native.value("vehicle",false)) {')
    text=once(text,'(kind == "texture" && category == "textures") ||',
        '(kind == "texture" && category == "textures") ||\n                                  (kind == "vehicle" && vehicle_store_category(category)) ||')
    text=once(text,'            if (kind == "code") {\n                dkr::mods::code::Library library(config / "mods/custom-magic-codes", version);',
        '''            if (kind == "vehicle") {
                remove_store_vehicle(config,member,stop);
            } else if (kind == "code") {
                dkr::mods::code::Library library(config / "mods/custom-magic-codes", version);''')
    return text

def ui(text):
    declarations='''bool StoreVehiclesBusy();
std::shared_ptr<const void> StoreVehiclesSnapshot();
void StoreRefreshVehicles();
dkr::store::Json StoreVehicleInventory();
'''
    text=once(text,'#include "mod_store_ui.inl"',declarations+'#include "mod_store_ui.inl"')
    text=once(text,'#include "vehicle_ui.inl"','#include "vehicle_ui.inl"\n#include "vehicle_store_ui.inl"')
    return text

def inl(name,text):
    if name.startswith(('mod_store_','store_')):
        for before in ('Legacy / ROM hacks','Legacy Mods / ROM hacks','Legacy Mods / Romhacks','Legacy / Romhacks'):
            text=text.replace(before,'Vehicles')
        text=text.replace('"Characters", "Legacy",','"Characters", "Vehicles",')
    if name=='mod_store_ui.inl':
        text=once(text,'"zip,rtz,dkrmap,xdelta,nrm,dkrcode,bps,ips,7z"','"zip,rtz,dkrmap,dkrkart,dkrhover,dkrplane,xdelta,nrm,dkrcode,bps,ips,7z"')
        text=text.replace('category == "magic" ? 2 : category == "characters" ? 1 : 0',
            'category == "magic" ? 2 : category == "characters" ? 1 : category == "legacy" || category == "vehicles" ? 3 : 0')
        text=once(text,'g_legacy_imports.snapshot().busy || g_code_library.view()->busy || TexturePackImportRunning() ||',
            'StoreVehiclesBusy() || g_legacy_imports.snapshot().busy || g_code_library.view()->busy || TexturePackImportRunning() ||')
    elif name=='mod_store_management_ui.inl':
        text=once(text,'    for (const auto& item : g_code_library.view()->items)',
            '    for (const auto& car : StoreVehicleInventory()) result.push_back(car);\n    for (const auto& item : g_code_library.view()->items)')
        text=once(text,'if (locked || g_legacy_imports.snapshot().busy || g_code_library.view()->busy || TexturePackImportRunning() ||',
            'if (locked || StoreVehiclesBusy() || g_legacy_imports.snapshot().busy || g_code_library.view()->busy || TexturePackImportRunning() ||')
        text=once(text,'!g_code_library.view()->busy && !TexturePackImportRunning() && !DialogJobRunning();',
            '!g_code_library.view()->busy && !StoreVehiclesBusy() && !TexturePackImportRunning() && !DialogJobRunning();')
        text=once(text,'    static const void* last_items = nullptr;',
            '    static const void* last_items = nullptr;\n    static std::shared_ptr<const void> last_vehicles;')
        text=once(text,'        g_code_library.refresh();', '        g_code_library.refresh();\n        StoreRefreshVehicles();')
        text=once(text,'    const auto legacy = g_legacy_imports.snapshot();\n    const auto code =',
            '    const auto legacy = g_legacy_imports.snapshot();\n    const auto vehicles = StoreVehiclesSnapshot();\n    const auto code =')
        text=once(text,'legacy.tracks != last_tracks || legacy.characters != last_characters || last_code != code ||',
            'vehicles != last_vehicles || legacy.tracks != last_tracks || legacy.characters != last_characters || last_code != code ||')
        text=once(text,'        last_tracks = legacy.tracks;', '        last_vehicles = vehicles;\n        last_tracks = legacy.tracks;')
    elif name=='vehicle_ui.inl':
        text=once(text,'if(view_->busy||root_.empty())return false;',
            'if(view_->busy||root_.empty()||(dkr::store::installations().view()->ready&&dkr::store::installations().view()->busy))return false;')
        text=once(text,'if(g_vehicle_library.view()->busy||g_vehicle_library.view()->pending||DialogJobRunning())return;',
            'if(dkr::store::installations().view()->busy||g_vehicle_library.view()->busy||g_vehicle_library.view()->pending||DialogJobRunning())return;')
    return text
