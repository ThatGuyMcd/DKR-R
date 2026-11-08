"""Compose beta 10 save repair into a NEW immutable first-party stage.

Inputs are read-only. No submodule, RecompiledFuncs or RecompiledPatches writes.
Verification is static; gameplay acceptance belongs to the owner.
"""
from pathlib import Path
import argparse
import hashlib
import json
import shutil
import sys
sys.dont_write_bytecode = True
AUTH = Path(__file__).resolve().parent
ROOT = AUTH.parents[1]

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def once(text, old, new):
    if text.count(old) != 1:
        raise ValueError('Shared save anchor changed: ' + old[:150])
    return text.replace(old, new)

def function(text, start, end, replacement):
    a = text.index(start)
    b = text.index(end, a)
    return text[:a] + replacement + '\n\n' + text[b:]

def compose(sdk, store, out):
    if out.exists() or not out.resolve().is_relative_to(ROOT / 'build'):
        raise ValueError('Choose a NEW directory inside build/')
    for base, receipt in ((sdk, 'sdk2-receipt.json'), (store, 'mod-store-receipt.json')):
        doc = json.loads((base / receipt).read_text())
        for name, expected in doc['files'].items():
            if sha(base / name) != expected:
                raise ValueError('Accepted baseline changed: ' + name)
    game = out / 'native/src/game'
    game.mkdir(parents=True)
    inputs = {}
    def read(name, from_store=False):
        path = (store if from_store else sdk) / 'native/src/game' / name
        inputs[str(path)] = sha(path)
        return path.read_text()
    def put(name, text):
        path = game / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding='utf-8', newline='\n')
    def read_probe(name):
        path=sdk/'native/tests/replay_probe'/name;inputs[str(path)]=sha(path);return path.read_text()
    def put_probe(name,text):
        path=out/'native/tests/replay_probe'/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_text(text,newline='\n')
    # Keep the prepared content fingerprint for assets only. No save fork.
    text = read('mods/legacy_mod_launch.cpp')
    a = text.index('        if(out->fingerprint.empty())return out;')
    b = text.index('        report("Custom mods ready");return out;', a)
    text = text[:a] + '''        out->save_subfolder.clear();
        out->save_path=config/"saves"/"dkr.us.v77.bin";
        out->pak_directory=config;
        std::string save_error;
        if(!dkr::runtime::saves::shared::prepare(config,save_error))throw Error(save_error);
''' + text[b:]
    # The no-mod route needs the same transaction recovery/migration archive.
    text = once(text, '    if(online)return out;', '''    if(online)return out;
    std::string personal_error;
    if(!dkr::runtime::saves::shared::prepare(config,personal_error))throw Error(personal_error);''')
    put('mods/legacy_mod_launch.cpp', '#include "../shared_saves.hpp"\n' + text)
    text = read('runtime_legacy_mods.cpp')
    text = '#include "custom_records.hpp"\n'+text
    text = once(text, '    bool private_owner=false,frozen_online=false,custom_ai=false;', '    bool private_owner=false,frozen_online=false,custom_ai=false;\n    bool records_restored=false;\n    mods::Bytes records_last;')
    text = once(text, 'bool frozen_courses(){', '#include "record_guest.inl"\nbool frozen_courses(){')
    text = once(text, '    ordinary_world.pending_course_heap=0;', '    ordinary_world.pending_course_heap=0;ordinary_world.records_restored=false;ordinary_world.records_last.clear();')
    text = once(text, '        // Frozen/owned worlds keep the reviewed retail vehicle gate and must', '''        if(!legacy::private_world&&(event==0||event==1||event==15)) {
            auto& owner=legacy::ordinary_world;
            auto bytes=legacy::course_records(rdram,*ctx,!owner.records_restored);
            if(!bytes.empty()) {
                owner.records_restored=true;
                if(!netplay::session().active()&&bytes!=owner.records_last) {
                    saves::records::submit(bytes);owner.records_last=std::move(bytes);
                }
            }
        }
        // Frozen/owned worlds keep the reviewed retail vehicle gate and must''')
    text=once(text,'        if(name=="dkr_legacy_asset_api")', '''        if(name=="dkr_shared_records_capture") {
            arity(0);state_->records_last=course_records(ram,*ctx,false);
            if(state_->records_last.empty())state_->records_last=saves::records::encode({});
            result=reinterpret_cast<std::uintptr_t>(&state_->records_last);
        }
        else if(name=="dkr_legacy_asset_api")''')
    text=once(text,'        else if(name=="dkr_legacy_asset_api")', '''        else if(name=="dkr_shared_records_install") {
            arity(0);state_->records_last=course_records(ram,*ctx,true);
            result=!state_->records_last.empty()||authored_courses().empty();
        }
        else if(name=="dkr_legacy_asset_api")''')
    text += '''\nnamespace dkr::runtime::saves::records {
Bytes snapshot(legacy::ModWorld& world,std::uint8_t* ram) {
    recomp_context call{};std::uint64_t result=0;
    if(world.dispatch("dkr_shared_records_capture",ram,&call,nullptr,0,nullptr,0,result)!=1||!result)
        throw std::runtime_error("Could not capture custom-course records from the owned guest.");
    return *reinterpret_cast<const Bytes*>(std::uintptr_t(result));
}
void install(legacy::ModWorld& world,std::uint8_t* ram) {
    if(seed().size()==20)return;
    recomp_context call{};std::uint64_t result=0;
    if(world.dispatch("dkr_shared_records_install",ram,&call,nullptr,0,nullptr,0,result)!=1||!result)
        throw std::runtime_error("Custom records could not be installed at the initial owned boundary. Originals were retained.");
}
}\n'''
    text = once(text, 'if(prepared->save_subfolder.empty() || prepared->save_path.empty() || prepared->pak_directory.empty())',
        'if(prepared->save_path.empty() || prepared->pak_directory.empty())')
    text = text.replace('Modded runtime admission requires isolated save paths.', 'Runtime admission requires a frozen personal save owner.')
    put('runtime_legacy_mods.cpp', text)
    text = read('game_main.cpp')
    text = once(text, '    if (!ec) roots.push_back(executable.parent_path() / "dkr-runtime-data");',
        '    if (!ec) roots.push_back(executable.parent_path() / "dkr-runtime-data");\n    roots.push_back(dkr::runtime::portable::application_directory() / "dkr-runtime-data");')
    put('game_main.cpp', text)
    text=read('game_main.cpp')
    text=once(text,'              selecting_save_owner = true;', '''              if(development_session&&!online_save.active&&
                 !dkr::runtime::saves::shared::prepare_development(config_directory,developer_folder,rom_error))throw dkr::mods::Error(rom_error);
              selecting_save_owner = true;''')
    text=once(text,'            ultramodern::set_save_storage_callbacks({', '''            {
                const auto path=dkr::runtime::saves::runtime_save_path();
                const auto utf8=path.generic_u8string();
                std::fprintf(stderr,"[save][selected] owner=%s path=%s progress=%s\\n",
                    online_save.active?"online":development_session?"development":"personal",
                    std::string(utf8.begin(),utf8.end()).c_str(),dkr::runtime::saves::shared::describe(path).c_str());
            }
            ultramodern::set_save_storage_callbacks({''')
    text=once(text,'    if (!ec) roots.push_back(executable.parent_path() / "dkr-runtime-data");',
        '    if (!ec) roots.push_back(executable.parent_path() / "dkr-runtime-data");\n    roots.push_back(dkr::runtime::portable::application_directory() / "dkr-runtime-data");')
    text=once(text,'            dkr::runtime::pak::begin_session_directory(dkr::runtime::saves::runtime_pak_directory());', '''            dkr::runtime::pak::begin_session_directory(dkr::runtime::saves::runtime_pak_directory());
            if(!dkr::runtime::saves::records::begin(development_session?std::filesystem::path{}:
                dkr::runtime::saves::runtime_save_path().parent_path()/"custom-records-v1.bin",
                !development_session&&(!online_save.active||online_save.host),rom_error))throw dkr::mods::Error(rom_error);''')
    text=text.replace('        dkr::runtime::saves::retire_runtime_save_context();', '''        {std::string records_error;if(!dkr::runtime::saves::records::finish(records_error)) {
            std::fprintf(stderr,"[save][custom-records] %s\\n",records_error.c_str());
#if DKR_RUNTIME_HAS_RT64
            dkr::runtime::ui::report_mod_error(records_error);
#endif
        }}
        dkr::runtime::saves::retire_runtime_save_context();''')
    put('game_main.cpp','#include "custom_records.hpp"\n#include "shared_saves.hpp"\n'+text)
    text = read('save_manager.hpp')
    text = once(text, 'enum class StoredSaveKind { Adventure, ControllerPak, ExperimentalPaks };',
        'enum class StoredSaveKind { Adventure, ControllerPak, ExperimentalPaks, CustomRecords };')
    text = once(text, '    std::string label, preview;', '    std::string label, preview, content_hash;\n    unsigned copies=1;')
    text = once(text, 'std::vector<StoredAdventure> stored_adventures();',
        'std::vector<StoredAdventure> stored_adventures(std::size_t limit=256, bool* truncated=nullptr);')
    put('save_manager.hpp', text)
    text = read('save_manager.cpp')
    text=once(text,'constexpr std::uint32_t kBundleVersion = 1U;', 'constexpr std::uint32_t kBundleVersion = 2U;\nconstexpr std::uint32_t kBundleRecordsKind=3U;')
    text=once(text,'    64U + kAdventureSaveSize +','    1024U*1024U + kAdventureSaveSize +')
    text=once(text,'bool ReadExperimentalOnlinePaks(const std::filesystem::path&, std::vector<std::uint8_t>&);', '''bool ReadExperimentalOnlinePaks(const std::filesystem::path&, std::vector<std::uint8_t>&);
bool ReadCustomRecords(const std::filesystem::path& path,std::vector<std::uint8_t>& bytes) {
    return ReadFileBounded(path,20+8192*76,bytes)&&dkr::runtime::saves::records::valid(bytes);
}''')
    text=once(text,'        ReadLE32(bytes, 16U) != kBundleVersion)', '        (ReadLE32(bytes, 16U) != 1U && ReadLE32(bytes, 16U) != kBundleVersion))')
    text=once(text,'            1U + static_cast<std::uint32_t>(dkr::runtime::saves::kControllerPakCount))',
        '            2U + static_cast<std::uint32_t>(dkr::runtime::saves::kControllerPakCount))')
    text=once(text,'std::array<bool, 1U + dkr::runtime::saves::kControllerPakCount> seen{};', 'std::array<bool, 2U + dkr::runtime::saves::kControllerPakCount> seen{};')
    text=once(text,'if (entry.channel != 0U || size != kAdventureSaveSize)', 'if (entry.channel != 0U || size != kAdventureSaveSize || !dkr::runtime::saves::codec::validate(entry.bytes))')
    text=once(text,'            seen_index = 1U + entry.channel;\n        } else {', '''            seen_index = 1U + entry.channel;
        } else if(entry.kind==kBundleRecordsKind) {
            if(ReadLE32(bytes,16U)<2||entry.channel||!dkr::runtime::saves::records::valid(entry.bytes))return false;
            seen_index=5;
        } else {''')
    text = once(text, 'std::vector<dkr::runtime::saves::StoredAdventure> dkr::runtime::saves::stored_adventures() {',
        'std::vector<dkr::runtime::saves::StoredAdventure> dkr::runtime::saves::stored_adventures(std::size_t limit, bool* truncated) {\n    limit=std::clamp<std::size_t>(limit,256,50000);\n    if(truncated)*truncated=false;')
    text = text.replace('result.size() >= 256', 'result.size() >= limit').replace('visited++ < 2048 && result.size() < 256', 'visited++ < limit*32 && result.size() < limit')
    text = once(text, '    return result;\n}\n\nbool dkr::runtime::saves::export_stored_adventure',
        '    if(truncated)*truncated=result.size()>=limit||visited>=limit*32;\n    return result;\n}\n\nbool dkr::runtime::saves::export_stored_adventure')
    # All host games have one durable Adventure, not one per content manifest.
    text = once(text, '    if(!profile.empty())folder/="mod-profiles"/std::filesystem::path(profile);',
        '    (void)profile; // Mod manifests identify simulation, never durable progress.')
    text = once(text, 'candidate / "online", candidate / "save-history"',
        'candidate / "online", candidate / "save-history", candidate / "save-migration-v1/images", candidate / "save-recovery", candidate / "save-transactions"')
    text = once(text, '            if (!primary && !recovery) continue;',
        '            const bool preserved=(relative.starts_with("save-migration-v1/images/")||relative.starts_with("save-recovery/"))&&name.ends_with(".bin");\n            const bool transaction=relative.starts_with("save-transactions/")&&name.ends_with(".original");\n            if (!primary && !recovery && !preserved && !transaction) continue;')
    text = once(text, '        item.info.valid = !ec && (kind == StoredSaveKind::Adventure ?',
        '        if(kind==StoredSaveKind::Adventure && item.info.size==32768)kind=item.kind=StoredSaveKind::ControllerPak;\n        item.info.valid = !ec && (kind == StoredSaveKind::Adventure ?')
    text=once(text,'ReadControllerPak(path, bytes) : ReadExperimentalOnlinePaks(path, bytes));',
        'ReadControllerPak(path, bytes) : kind==StoredSaveKind::CustomRecords?ReadCustomRecords(path,bytes):ReadExperimentalOnlinePaks(path, bytes));\n        item.content_hash=storage::content_id(bytes);')
    text=once(text,'        if(kind==StoredSaveKind::Adventure && item.info.size==32768)',
        '        if(item.info.size<=1024*1024)ReadFileBounded(path,1024*1024,bytes);\n        if(dkr::runtime::saves::records::valid(bytes))kind=item.kind=StoredSaveKind::CustomRecords;\n        if(kind==StoredSaveKind::Adventure && item.info.size==32768)')
    text=once(text,'        item.content_hash=storage::content_id(bytes);',
        '        item.content_hash=bytes.empty()?path.generic_string():dkr::mods::sha256(bytes);')
    text=once(text,'            const bool primary = name == "dkr.us.v77.bin"', '            const bool primary = name == "custom-records-v1.bin" || name == "dkr.us.v77.bin"')
    text=once(text,'            const auto kind = relative.find("experimental-controller-paks.bin")',
        '            const auto kind = relative.find("custom-records-v1.bin")!=std::string::npos?StoredSaveKind::CustomRecords:relative.find("experimental-controller-paks.bin")')
    text=once(text,'        } else if (item.info.valid) item.preview = kind == StoredSaveKind::ControllerPak',
        '        } else if(item.info.valid&&kind==StoredSaveKind::CustomRecords)item.preview="Custom-course records. Included in personal save bundles; kept outside the retail EEPROM.";\n        else if (item.info.valid) item.preview = kind == StoredSaveKind::ControllerPak')
    text=once(text,'kind == StoredSaveKind::ControllerPak ? ReadControllerPak : ReadExperimentalOnlinePaks;',
        'kind == StoredSaveKind::ControllerPak ? ReadControllerPak : kind==StoredSaveKind::CustomRecords?ReadCustomRecords:ReadExperimentalOnlinePaks;')
    text=once(text,'        if (!BackupPakUnlocked(channel, backup, error)) {\n            return false;\n        }\n    }\n    return WriteAtomic(ControllerPakPath(channel), bytes, ReadControllerPak, error);',
        '        std::vector<std::uint8_t> previous;\n        if (ReadControllerPak(ControllerPakPath(channel),previous) && !BackupPakUnlocked(channel, backup, error)) {\n            return false;\n        }\n    }\n    return WriteAtomic(ControllerPakPath(channel), bytes, ReadControllerPak, error);')
    text=once(text,'    if (entries.empty()) {\n        error = "No valid Adventure save or Controller Pak is available to export.";', '''    const auto custom=g_config_directory/"saves/custom-records-v1.bin";
    if(std::filesystem::exists(custom)) {
        if(!ReadCustomRecords(custom,image)){error="Custom records need recovery. Export their original image before transferring your save.";return false;}
        entries.push_back({kBundleRecordsKind,0U,image});
    }
    if (entries.empty()) {
        error = "No valid Adventure save or Controller Pak is available to export.";''')
    text = text.replace('No checksum-valid previous online save exists for this exact mod set. Choose a fresh save or copy your single-player save.',
        'No previous online Adventure exists yet. Copy your personal save or start fresh.')
    a=text.index('    if (mode == OnlineSaveSeedMode::Fresh) {',text.index('bool dkr::runtime::saves::prepare_host_online_adventure'))
    b=text.index('    // Creating a lobby is not a commit point:',a)
    text=text[:a]+(AUTH/'online_seed.inl').read_text()+'\n'+text[b:]
    a = text.index('        std::vector<std::uint8_t> unverified;', text.index('bool dkr::runtime::saves::import_adventure'))
    b = text.index('    std::error_code equivalent_error;', a)
    text = text[:a] + '        error="This save needs repair. Use Backups & Recovery to preserve and repair a copy first.";\n        return false;\n    }\n' + text[b:]
    a = text.index('bool dkr::runtime::saves::import_bundle(')
    text = text[:a] + '''bool dkr::runtime::saves::import_bundle(const std::filesystem::path& source, std::string& error) {
    return shared::apply_bundle(source,error);
}
\n#include "shared_save_backend.inl"\n'''
    text += '\n#include "record_store.inl"\n'
    put('save_manager.cpp', '#include "shared_saves.hpp"\n#include "custom_records.hpp"\n#include "mods/legacy_mod_format.hpp"\n#include <future>\n#include <nlohmann/json.hpp>\n' + text)
    text = read('runtime_ui.cpp', True)
    text=once(text,'Legacy mod selections keep separate saves.', 'Normal mods share your personal save. Development tools stay separate.')
    text = once(text, '#include "runtime_ui.hpp"', '#include "runtime_ui.hpp"\n#include "shared_saves.hpp"')
    text=once(text,'SaveManagerViewCache g_save_manager_view_cache;', 'SaveManagerViewCache g_save_manager_view_cache;\nstd::uint64_t g_save_cache_revision=0;')
    text=once(text,'    g_save_manager_view_cache.valid = false;', '    g_save_manager_view_cache.valid = false;\n    ++g_save_cache_revision;')
    text=once(text,'    std::vector<std::filesystem::path> adventure_backups;','    std::vector<std::filesystem::path> adventure_backups;\n    std::optional<dkr::runtime::saves::codec::SaveImage> image;')
    text=function(text,'const SaveManagerViewCache& CachedSaveManagerView() {','std::string g_magic_codes_status;', (AUTH/'cache_ui.inl').read_text())
    text = once(text, 'std::optional<dkr::runtime::saves::codec::SaveImage> g_save_builder_image;',
        'std::optional<dkr::runtime::saves::codec::SaveImage> g_save_builder_image;\n#include "shared_save_jobs.inl"')
    a = text.index('    const auto adventure = dkr::runtime::saves::adventure_info();', text.index('void dkr::runtime::ui::configure'))
    b = text.index('    dkr::runtime::magic_codes::configure', a)
    text = text[:a] + '    // Checksum repair is explicit; never bless unknown payload damage at startup.\n' + text[b:]
    text = once(text, 'int g_online_save_seed_mode = static_cast<int>(\n    dkr::runtime::saves::OnlineSaveSeedMode::CopySinglePlayer);',
        'int g_online_save_seed_mode = static_cast<int>(\n    dkr::runtime::saves::OnlineSaveSeedMode::ContinuePreviousSession);')
    text=once(text,'int g_online_input_profile = 0;', 'bool g_shared_online_policy_loaded=false;\nint g_online_input_profile = 0;')
    text=once(text,'        output << "online_save_seed_mode=" << g_online_save_seed_mode << \'\\n\';',
        '        output << "online_save_policy=2\\n";\n        output << "online_save_seed_mode=" << g_online_save_seed_mode << \'\\n\';')
    text=once(text,'        // Outside the chain below, which is already at MSVC\'s nesting limit.',
        '        if(key=="online_save_policy"){g_shared_online_policy_loaded=value=="2";continue;}\n        // Outside the chain below, which is already at MSVC\'s nesting limit.')
    text=once(text,'    LoadSettings();', '    LoadSettings();\n    if(!g_shared_online_policy_loaded)g_online_save_seed_mode=int(dkr::runtime::saves::OnlineSaveSeedMode::ContinuePreviousSession);')
    text = once(text, '                g_mod_launch.start(g_config_directory,mod_launch_rom,false);',
        '                if(!SharedSaveBusy())g_mod_launch.start(g_config_directory,mod_launch_rom,false);')
    text = once(text, '            result.start_game = true;',
        '            if(SharedSaveBusy())continue;\n            result.start_game = true;')
    text = once(text, '                result.start_game=true;result.rom_path=mod_launch_rom;result.mods=prepared.prepared;',
        '                if(SharedSaveBusy())continue;\n                result.start_game=true;result.rom_path=mod_launch_rom;result.mods=prepared.prepared;')
    text = text.replace('        PumpDialogJob();', '        PumpDialogJob();\n        PumpSharedSaveJob();')
    text=once(text,'    dkr::store::service().gameplay(true);', '''    while(SharedSaveBusy()){PumpSharedSaveJob();SDL_PumpEvents();std::this_thread::sleep_for(std::chrono::milliseconds(2));}
    dkr::store::service().gameplay(true);''')
    # Import file-pickers stage choices; the shared page previews/confirms them.
    text = function(text, 'bool ImportAdventureWithDialog() {', 'bool ExportAdventureWithDialog(',
        (AUTH / 'import_dialog.inl').read_text())
    text = function(text, 'bool ImportSaveBundleWithDialog() {', 'bool ExportSaveBundleWithDialog()',
        (AUTH / 'bundle_dialog.inl').read_text())
    text = once(text, 'bool DialogJobRunning() {', 'bool DialogJobRunning() {\n    if(SharedSaveBusy())return true;')
    # Exports use the same bounded background I/O queue as imports/edits.
    text=once(text,'''        exported = dkr::runtime::saves::export_stored_save(source.empty()
            ? dkr::runtime::saves::adventure_info().path : source, destination, kind, error);''', '''        const auto selected=source.empty()?dkr::runtime::saves::adventure_info().path:source;
        exported=BeginSharedSaveJob([selected,destination,kind](std::string& error){
            return dkr::runtime::saves::export_stored_save(selected,destination,kind,error);
        },"Save exported.");''')
    text=once(text,'        exported = dkr::runtime::saves::export_bundle(destination, error);',
        '        exported = BeginSharedSaveJob([destination](std::string& error){return dkr::runtime::saves::export_bundle(destination,error);},"Save bundle exported.");')
    text=text.replace('? "Save exported."','? "Exporting save..."').replace('? "Offline save bundle exported."','? "Exporting save bundle..."')
    put('runtime_ui.cpp', text)
    text = read('runtime_saves_ui.inl')
    text=once(text,'            codec::normalise_editable_fields(image);','            // Opening the editor must not silently normalise existing progress.')
    text=once(text,'Edit a slot or shared unlocks. Changes stay in your draft until you apply them.',
        'Manual edits use Apply Changes. Completion and unlock buttons save when confirmed.')
    text = once(text, 'enum class Ask { None, Repair, Reset, Restore, MaxOut, UnlockAll, Discard, Apply }',
        'enum class Ask { None, Repair, Reset, Restore, MaxOut, UnlockAll, Discard, Apply, Import, Bundle, Pak, RepairUse, Host, DevelopmentCopy }')
    text = function(text, 'void DrawSavesOverview(', '// ------------------------------------------------------------ transfer',
        (AUTH / 'overview_ui.inl').read_text())
    text = text[:text.index('void DrawSavesTransfer(')] + (AUTH / 'page_ui.inl').read_text()
    # Editor draft is bound to the byte revision, not just its in-memory flags.
    text = once(text, '            state.original = image;', '            state.original = image;\n            g_shared_draft_revision=dkr::runtime::saves::shared::revision();')
    text = once(text, '    auto& settings = g_save_builder_image->settings;', '    auto& settings = g_save_builder_image->settings;')
    put('runtime_saves_ui.inl', text)
    text = read('runtime_online_ui.inl', True)
    text = text.replace('Continue the host-owned online save for this exact mod set. Availability is checked when creating the lobby.',
        'Continue your online Adventure. Changing mods no longer selects a different save.')
    text=text.replace('Copy your progress into a separate online save. Your single-player file is kept.',
        'Replace the host Adventure with a copy of your personal save when this lobby starts. The previous online Adventure is backed up.')
    put('runtime_online_ui.inl', text)
    # Pure course-record observation is carried by the confirmed effect
    # journal, never persisted from speculative RAM or a replay callback.
    text=read_probe('owned_game.hpp')
    text=once(text,'    virtual void set_confirmed_rumble_sink(ConfirmedRumbleSink sink) = 0;',
        '    virtual void set_confirmed_rumble_sink(ConfirmedRumbleSink sink) = 0;\n    virtual std::span<const std::uint8_t> confirmed_records() const {return {};}')
    put_probe('owned_game.hpp',text)
    text=read_probe('probe_driver.cpp')
    text=once(text,'        } else if(!boot.code_bootstrap.empty())throw std::runtime_error("Code bootstrap has no frozen package identity.");', '''        } else if(!boot.code_bootstrap.empty())throw std::runtime_error("Code bootstrap has no frozen package identity.");
#if DKR_PROBE_HAS_MOD_SERVICES
        // Disk data was frozen before native boot. Only the host has a seed;
        // the client's canonical initial checkpoint comes from that host.
        if(mods_)saves::records::install(*mods_,ram_.data());
#endif''')
    text=once(text,'    std::span<const std::uint8_t> confirmed_save() const {return save_.confirmed_image();}',
        '    std::span<const std::uint8_t> confirmed_save() const {return save_.confirmed_image();}\n    std::span<const std::uint8_t> confirmed_records() const {return confirmed_records_;}')
    anchor='''        return true;
    }
    bool commit(std::uint64_t epoch, std::uint32_t frame, std::span<const std::uint8_t> effects,'''
    text=once(text,anchor,'''        if(authored_cpu_) {
            auto records=saves::records::encode({});
#if DKR_PROBE_HAS_MOD_SERVICES
            if(mods_)records=saves::records::snapshot(*mods_,ram_.data());
#endif
            const auto inner=unsigned(output.effects.size());
            prepend_effect_envelope(output.effects,{'D','K','R','C'},std::array{inner,unsigned(records.size())});
            output.effects.insert(output.effects.end(),records.begin(),records.end());
        }
        return true;
    }
    bool commit(std::uint64_t epoch, std::uint32_t frame, std::span<const std::uint8_t> effects,''')
    text=once(text,'        const auto original=effects;', '''        const auto original=effects;
        std::span<const std::uint8_t> course_records;
        if(authored_cpu_) {
            if(effects.size()<12||!std::equal(effects.begin(),effects.begin()+4,"DKRC"))return false;
            const auto inner=unsigned(saves::records::integer(effects,4,4));
            const auto length=unsigned(saves::records::integer(effects,8,4));
            if(inner>131072||length>20000||effects.size()!=12+inner+length)return false;
            course_records=effects.subspan(12+inner,length);
            if(!saves::records::valid(course_records))return false;
            effects=effects.subspan(12,inner);
        }''')
    text=once(text,'        ++confirmed_count_;','        confirmed_records_.assign(course_records.begin(),course_records.end());\n        ++confirmed_count_;')
    text=once(text,'    unsigned confirmed_scene_=0;','    unsigned confirmed_scene_=0;\n    saves::records::Bytes confirmed_records_;')
    text=once(text,'    std::span<const std::uint8_t> confirmed_save() const override {return world_.confirmed_save();}',
        '    std::span<const std::uint8_t> confirmed_save() const override {return world_.confirmed_save();}\n    std::span<const std::uint8_t> confirmed_records() const override {return world_.confirmed_records();}')
    text=text.replace('saves::records::','dkr::runtime::saves::records::')
    put_probe('probe_driver.cpp','#include "custom_records.hpp"\nnamespace dkr::runtime::saves::records {void install(legacy::ModWorld&,std::uint8_t*);}\n'+text)
    text=read('netplay/experimental_confirmed_output.hpp')
    text=once(text,'    std::vector<std::uint8_t> eeprom,paks;','    std::vector<std::uint8_t> eeprom,paks,records;')
    text=once(text,'std::span<const std::uint8_t> paks,std::string& error) {','std::span<const std::uint8_t> paks,std::string& error,std::span<const std::uint8_t> records={}) {')
    text=once(text,'        ConfirmedSaveSnapshot copy{{eeprom.begin(),eeprom.end()},{paks.begin(),paks.end()}};',
        '        if(!records.empty()&&!saves::records::valid(records)){error="Invalid confirmed custom records.";return false;}\n        ConfirmedSaveSnapshot copy{{eeprom.begin(),eeprom.end()},{paks.begin(),paks.end()},{records.begin(),records.end()}};')
    put('netplay/experimental_confirmed_output.hpp','#include "custom_records.hpp"\n'+text)
    text=read('netplay/experimental_runtime.cpp')
    text=once(text,'    std::span<const std::uint8_t> confirmed_save() const override {return game_->confirmed_save();}',
        '    std::span<const std::uint8_t> confirmed_save() const override {return game_->confirmed_save();}\n    std::span<const std::uint8_t> confirmed_records() const override {return game_->confirmed_records();}')
    text=once(text,'        std::scoped_lock lock(shared.mutex);shared.confirmed_save=snapshot.eeprom;return true;', '''        if(!snapshot.records.empty()) {
            std::vector<std::uint8_t> ignored;std::filesystem::path owner;
            if(!saves::read_online_adventure(host,match,ignored,owner,message)||
               !saves::records::persist(owner.parent_path()/"custom-records-v1.bin",snapshot.records,message))return false;
        }
        std::scoped_lock lock(shared.mutex);shared.confirmed_save=snapshot.eeprom;return true;''')
    text=once(text,'                            if(!save_writer.submit(confirmed,paks,message))throw std::runtime_error(message);',
        '                            if(!save_writer.submit(confirmed,paks,message,world->confirmed_records()))throw std::runtime_error(message);')
    text=once(text,'            if(!save_writer.submit(world->confirmed_save(),world->confirmed_paks(),message))shared.fail(message);',
        '            if(!save_writer.submit(world->confirmed_save(),world->confirmed_paks(),message,world->confirmed_records()))shared.fail(message);')
    # Checkpoints can carry a new record without changing the 512-byte EEPROM.
    text=once(text,'                           !std::equal(paks.begin(),paks.end(),last_paks_at.begin(),last_paks_at.end())) {',
        '                           !std::equal(paks.begin(),paks.end(),last_paks_at.begin(),last_paks_at.end())||!world->confirmed_records().empty()) {')
    put('netplay/experimental_runtime.cpp',text)
    for path in AUTH.glob('*.inl'):
        if path.name not in ('overview_ui.inl','page_ui.inl','import_dialog.inl','bundle_dialog.inl','cache_ui.inl','online_seed.inl'):
            put(path.name, path.read_text())
    shutil.copy2(AUTH / 'shared_saves.hpp', game / 'shared_saves.hpp')
    shutil.copy2(AUTH / 'custom_records.hpp', game / 'custom_records.hpp')
    identity=hashlib.sha256()
    for p in sorted(out.rglob('*')):
        if p.is_file():identity.update(p.relative_to(out).as_posix().encode()+b'\0'+bytes.fromhex(sha(p)))
    (out/'engine-identity.txt').write_text(identity.hexdigest()+'\n')
    receipt = {'schema':1,'version':'1.0.7+beta.10','inputs':inputs,
        'authors':{p.relative_to(ROOT).as_posix():sha(p) for p in AUTH.rglob('*') if p.is_file() and '__pycache__' not in p.parts},
        'files':{p.relative_to(out).as_posix():sha(p) for p in out.rglob('*') if p.is_file()},
        'validation':'Static composition only; no game execution'}
    (out/'shared-saves-receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('action', choices=('stage','verify'))
    p.add_argument('--sdk',type=Path)
    p.add_argument('--store',type=Path)
    p.add_argument('--output',type=Path,required=True)
    a=p.parse_args()
    if a.action=='stage':compose(a.sdk.resolve(strict=True),a.store.resolve(strict=True),a.output)
    receipt=json.loads((a.output/'shared-saves-receipt.json').read_text())
    for name,expected in receipt['files'].items():
        if sha(a.output/name)!=expected:raise ValueError('Stage changed: '+name)
    print('Shared save immutable stage inspected; no target execution.')

if __name__=='__main__':main()
