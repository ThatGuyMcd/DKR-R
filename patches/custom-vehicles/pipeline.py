"""Stage authored vehicle adapters without changing source inputs or submodules."""
from pathlib import Path
import argparse
import hashlib
import json
import tomllib

AUTH = Path(__file__).resolve().parent
ROOT = AUTH.parents[1]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def put(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    if not path.exists() or path.read_bytes() != data:
        path.write_bytes(data)


def once(text, old, new):
    if text.count(old) != 1:
        raise ValueError('Vehicle integration anchor changed: ' + old[:120])
    return text.replace(old, new, 1)


def symbols(sdk):
    fields = ('gMenuStage', 'gPlayerSelectVehicle', 'gPlayerSelectConfirm',
              'gNumberOfReadyPlayers', 'gNumberOfActivePlayers', 'gMenuDelay',
              'gMenuButtons', 'gMenuStickX', 'gMenuStickY', 'sMenuCurrDisplayList',
              'dDialogueBoxBegin', 'dDialogueBoxDrawModes', 'gIsInTracksMode',
              'gCurrentMenuId', 'gObjectCurrDisplayList', 'gObjectCurrMatrix',
              'gSceneCurrentPlayerID', 'gMenuImages', 'gTrackSelectWoodFrameHeightScale',
              'gTrackSelectPlayerImage', 'gMenuSelectionArrows', 'gRaceSelectionImages', 'gCurrentRNGSeed',
              'gCurrentPlayerIndex', 'gRacersByPort', 'gCurrentLightIntensity',
              'gInverseViewMatrixF', 'gViewportLayout')
    text = '#pragma once\n// Generated from the admitted, revision-specific SDK symbol tables.\nnamespace dkr::runtime::vehicles {\n'
    for rev in (77, 80):
        data = tomllib.loads((sdk / f'reference-us-v{rev}/data.toml').read_text(encoding='utf-8'))
        found = {}
        for section in data['section']:
            for item in section.get('symbols', []):
                if item['name'] in fields:
                    if item['name'] in found: raise ValueError('Duplicate vehicle symbol: ' + item['name'])
                    found[item['name']] = item['vram']
        if set(found) != set(fields): raise ValueError('Incomplete custom vehicle symbol table')
        text += 'inline constexpr Symbols VehicleSymbols' + str(rev) + '{' + ','.join(f'0x{found[f]:08x}U' for f in fields) + '};\n'
    return text + '}\n'


def launch_source(text):
    text = once(text, '#include "legacy_mod_launch.hpp"', '#include "legacy_mod_launch.hpp"\n#include "vehicle_binding.hpp"')
    text = once(text, '    const auto root=config/"mods"/"legacy";\n    if(!TrackCatalog::has_enabled(root))return finish();', '''    const auto root=config/"mods"/"legacy";
    report("Preparing custom cars");
    auto cars=vehicles::Library(config/"mods"/"vehicles").freeze(stop);
    if(!TrackCatalog::has_enabled(root)&&cars.packages.empty())return finish();''')
    text = once(text, '    auto session=std::make_shared<RuntimeSession>(stock,names,std::vector<Bytes>{},\n        [artwork](std::shared_ptr<const AssetBank> bank){return AssetBank::append_artwork(std::move(bank),*artwork);});', '''    auto vehicle_resources=vehicles::prepare_resources(stock,
        AssetBank::append_artwork(names?names->apply(stock):stock,*artwork),names,std::move(cars));
    auto session=std::make_shared<RuntimeSession>(stock,names,std::vector<Bytes>{},
        [artwork,vehicle_resources](std::shared_ptr<const AssetBank> bank){
            return vehicle_resources->apply(AssetBank::append_artwork(std::move(bank),*artwork));
        });
    vehicles::bind_resources(session,vehicle_resources);''')
    text = once(text, '    std::vector<std::string> identities;', '    std::vector<std::string> identities;\n    if(!vehicle_resources->library.packages.empty())identities.push_back("vehicles:"+vehicle_resources->identity);')
    return text


def runtime_source(text):
    from music_pipeline import runtime_binding
    text = runtime_binding(text)
    text = once(text, '#include "runtime_legacy_mods.hpp"', '#include "runtime_legacy_mods.hpp"\n#include "vehicle_binding.hpp"\n#include "vehicle_world.hpp"\n#include "vehicle_lighting/road_wheels.h"\n#include "vehicle_lighting/road_wheels.hpp"\n#include <bit>')
    text = once(text, 'struct WorldState {', 'struct WorldState {\n    std::shared_ptr<vehicles::World> vehicles;')
    ai_entry='extern "C" void dkr_legacy_character_ai_event(std::uint8_t* rdram,recomp_context* ctx,unsigned event,dkr::mods::OriginalPiHandler random) {\n    using namespace dkr::runtime;'
    text = once(text,ai_entry,ai_entry+'''
    // Cars have their own primary-racer lifecycle. Do not gate this on a
    // character selector, the custom-AI preference, or an offline-only path.
    try {
        if(const auto cars=legacy::current_world().vehicles)
            cars->race({rdram,legacy::guest_memory_size()},*ctx,event);
    }catch(const ultramodern::thread_terminated&){throw;}
     catch(const std::exception& error){legacy::fail(error.what());}
''')
    text = once(text, '    ordinary_music_enabled=false;ordinary_world.music.store(nullptr);', '''    ordinary_music_enabled=false;ordinary_world.music.store(nullptr);
    const auto assets=session.load();const auto cars=mods::vehicles::resources_for(assets.get());
    ordinary_world.vehicles=cars&&!cars->library.packages.empty()?std::make_shared<vehicles::World>(cars,assets->boot_bank()->revision()=="us.v80"):nullptr;
    mods::vehicles::publish_render_resources(cars);
    std::vector<unsigned> road_headers;
    if(assets&&assets->characters())for(const auto& character:assets->characters()->characters)
        road_headers.push_back(character.headers[0]);
    if(!road_headers.empty())std::fprintf(stderr,"[graphics][road-wheels][catalogue] custom-headers=%zu; normal renderer\\n",road_headers.size());
    vehicles::lighting::road_wheels::publish_headers(std::move(road_headers));''')
    text = once(text, '    auto state=legacy::characters.load();if(!state)return;\n    try {', '''    if(event>=200) {
        try {
            const auto cars=legacy::current_world().vehicles;
            if(event==200)ctx->r2=cars?cars->render_begin({rdram,legacy::guest_memory_size()},roster_address):0;
            else if(event==201){if(cars)cars->render_end({rdram,legacy::guest_memory_size()},roster_address,unsigned(ctx->r8));}
            else if(event==202||event==203){if(cars)cars->wheel_scope({rdram,legacy::guest_memory_size()},roster_address,event==202);}
            else if(event==204){if(cars)cars->advance_wheels({rdram,legacy::guest_memory_size()},roster_address,std::bit_cast<float>(std::uint32_t(ctx->r6)));}
            else throw dkr::mods::Error("Unknown custom vehicle render event.");
            if(event<=203) {
                const auto assets=legacy::session.load();
                if(assets) {
                    const auto& s=vehicles::symbols(assets->boot_bank()->revision()=="us.v80");
                    dkr_road_wheel_call call{event,roster_address,unsigned(ctx->r4),0,0,
                        s.light_intensity,s.inverse_view,s.viewport_layout,event==200?unsigned(ctx->r2):0};
                    if(event==202) {
                        const vehicles::lighting::road_wheels::Memory g({rdram,legacy::guest_memory_size()});
                        call.sprite=g.get(unsigned(ctx->r29)+16);call.flags=g.get(unsigned(ctx->r29)+20);
                    }
                    dkr_native_road_wheel_scope(rdram,legacy::guest_memory_size(),&call);
                }
            }
            return;
        }catch(const ultramodern::thread_terminated&){throw;}
         catch(const std::exception& error){legacy::fail(error.what());}
    }
    auto state=legacy::characters.load();if(!state)return;
    try {''')
    anchor = 'extern "C" int dkr_legacy_track_menu(std::uint8_t* rdram,recomp_context* ctx,unsigned event,const std::uint32_t* fields,unsigned observed) {'
    text = once(text, anchor, '''extern "C" void dkr_vehicle_menu_draw(std::uint8_t* rdram,recomp_context* ctx,
        dkr::runtime::RecompiledEntrypoint frame,dkr::runtime::RecompiledEntrypoint text_width) {
    using namespace dkr::runtime;
    try {
        const auto cars=legacy::current_world().vehicles;const auto calls=legacy::guest_payload();
        if(cars&&calls)cars->draw({rdram,legacy::guest_memory_size()},*ctx,*calls,frame,text_width);
    }catch(const ultramodern::thread_terminated&){throw;}
     catch(const std::exception& error){legacy::fail(error.what());}
}

''' + anchor + '''
    if(event>=100) {
        using namespace dkr::runtime;
        try {
            const auto cars=legacy::current_world().vehicles;if(!cars)return 0;
            const auto calls=legacy::guest_payload();if(!calls)throw dkr::mods::Error("Custom vehicles have no admitted native menu callbacks.");
            const auto result=cars->menu({rdram,legacy::guest_memory_size()},*ctx,event,*calls,[&](unsigned player) {
                if(const auto state=legacy::characters.load()) {
                    std::lock_guard lock(state->mutex);
                    if(const auto entry=state->roster.entry(player))return entry->headers[0];
                }
                dkr::mods::CharacterMenuFields addresses;addresses.fill(0x80000000U);
                dkr::mods::CharacterMenuMemory g({rdram,legacy::guest_memory_size()},addresses);
                const auto table=legacy::session.load()->boot_bank()->revision()=="us.v80"?0x80126990U:0x801263f0U;
                constexpr unsigned headers[]{2,3,4,5,6,7,8,9,1,0};const auto base=g.read(table+player,1);
                if(base>=10)throw dkr::mods::Error("Invalid selected native driver.");return headers[base];
            });
            return result?1:0;
        }catch(const ultramodern::thread_terminated&){throw;}
         catch(const std::exception& error){legacy::fail(error.what());}
    }
''')
    text = once(text, '    state_->session.store(assets);state_->private_owner=true;state_->calls=calls;', '''    state_->session.store(assets);state_->private_owner=true;state_->calls=calls;
    std::vector<unsigned> road_headers;
    if(assets&&assets->characters())for(const auto& character:assets->characters()->characters)
        road_headers.push_back(character.headers[0]);
    if(!road_headers.empty())std::fprintf(stderr,"[graphics][road-wheels][catalogue] custom-headers=%zu; owned renderer\\n",road_headers.size());
    vehicles::lighting::road_wheels::publish_headers(std::move(road_headers));
    if(const auto cars=mods::vehicles::resources_for(assets.get());cars&&!cars->library.packages.empty()) {
        state_->vehicles=std::make_shared<vehicles::World>(cars,assets->boot_bank()->revision()=="us.v80");
        mods::vehicles::publish_render_resources(cars);
    }''')
    # Extend the native sidecar transaction, not the guest save/ROM format.
    text = once(text, '    std::shared_ptr<MusicBinding> music;\n};', '    std::shared_ptr<MusicBinding> music;\n    std::shared_ptr<vehicles::World> vehicles;\n};')
    text = once(text, 'mods::CheckpointWriter out;out.u32(4);out.block(state.session.load()->checkpoint());', 'mods::CheckpointWriter out;out.u32(5);out.block(state.session.load()->checkpoint());\n    out.flag(bool(state.vehicles));if(state.vehicles)out.block(state.vehicles->checkpoint());')
    text = once(text, 'mods::CheckpointReader in(bytes);if(in.u32()!=4)', 'mods::CheckpointReader in(bytes);if(in.u32()!=5)')
    text = once(text, 'out->session=assets->stage_checkpoint(in.block());', 'out->session=assets->stage_checkpoint(in.block());\n    if(in.flag()!=bool(state_->vehicles))throw mods::Error("Owned vehicle catalogue differs.");\n    if(state_->vehicles)out->vehicles=state_->vehicles->stage_checkpoint(in.block());')
    text = once(text, '    state_->pending_course_heap=staged->pending_course_heap;', '    state_->pending_course_heap=staged->pending_course_heap;\n    state_->vehicles=std::move(staged->vehicles);')
    return text


def bridge_source(text):
    text = once(text, '#include "f3ddkr_rt64.hpp"', '#include "f3ddkr_rt64.hpp"\n#include "vehicle_rt64.hpp"')
    text = once(text, '    camera::RenderPass* camera=nullptr;', '    camera::RenderPass* camera=nullptr;\n    vehicles::RenderPass* cars=nullptr;')
    text = once(text, '    active_ = this;\n    StateData fresh{};', '''    const auto car_resources=mods::vehicles::render_resources();
    const bool cars_requested=car_resources&&!car_resources->library.packages.empty();
    if(cars_requested)kRDRAMSize=0x1000000U;
    vehicles::RenderPass car_pass(*application.state,car_resources,[](RT64::RSP& rsp,std::uint32_t id,bool vertices){
        SelectInterpolationGroup(rsp,id,vertices,false,false,G_EX_ASPECT_AUTO,false,0U,true);
    });
    active_ = this;
    StateData fresh{};
    fresh.cars=&car_pass;''')
    text = once(text, '    const Handler handler = active_->original_handlers_[opcode];', '''    if((command->w0&0xffff0000U)==0xb0ca0000U) {
        if(!active_->data_->cars||!active_->data_->cars->marker(command->w0&0xffffU,command->w1))RejectTask(display_list);
        return;
    }
    const Handler handler = active_->original_handlers_[opcode];''')
    text = once(text, '#endif\n}\n\nvoid dkr::runtime::F3DDKRRT64Bridge::FillRect(', '#endif\n    if(data.cars)data.cars->matrix(index);\n}\n\nvoid dkr::runtime::F3DDKRRT64Bridge::FillRect(')
    at = text.index('void dkr::runtime::F3DDKRRT64Bridge::Triangle(')
    text = text[:at] + once(text[at:], '        rsp.drawIndexedTri(vertices[0], vertices[1], vertices[2]);', '        if(!data.cars||!data.cars->suppress(address))rsp.drawIndexedTri(vertices[0], vertices[1], vertices[2]);')
    return text


def renderer_source(text):
    text = '#include "vehicle_binding.hpp"\n' + text
    text = once(text, '    const bool camera_requested=bool(camera::activation());\n    if(camera_requested) {', '''    const bool camera_requested=bool(camera::activation());
    const auto car_resources=mods::vehicles::render_resources();
    const bool cars_requested=car_resources&&!car_resources->library.packages.empty();
    if(camera_requested||cars_requested) {''')
    return text


def rt64_state_source(text):
    text = once(text,'#include "local_scenery_texture_scope.hpp"','#include "vehicle_texture_scope.hpp"\n#include "vehicle_wheel_interpolation.hpp"')
    return once(text, '        workload.workloadId = ++workloadId;',
        '        dkr::runtime::vehicles::wheel_interpolation::seal(workload,workloadId+1);\n        workload.workloadId = ++workloadId;')


def rt64_transform_source(text):
    text = once(text, '#include "rt64_transform_processor.h"',
        '#include "rt64_transform_processor.h"\n#include "vehicle_wheel_interpolation.hpp"')
    text = once(text, '                const DrawData &prevDrawData = p.workloadQueue->workloads[workloadMap.prevWorkloadIndex].drawData;', '''                const DrawData &prevDrawData = p.workloadQueue->workloads[workloadMap.prevWorkloadIndex].drawData;
                const auto currentWheels=dkr::runtime::vehicles::wheel_interpolation::capture(workload);
                const auto previousWheels=dkr::runtime::vehicles::wheel_interpolation::capture(p.workloadQueue->workloads[workloadMap.prevWorkloadIndex]);''')
    return once(text, '                        curMatrix = transformMap.rigidBody.lerp(p.curFrameWeight, prevTransform, curTransform, true);', '''                        curMatrix = transformMap.rigidBody.lerp(p.curFrameWeight, prevTransform, curTransform, true);
                        if(currentWheels&&dkr::runtime::vehicles::wheel_interpolation::find(*currentWheels,t)) {
                            if(!dkr::runtime::vehicles::wheel_interpolation::interpolate(previousWheels,currentWheels,
                                transformMap.prevTransformIndex,t,p.prevFrameWeight,p.curFrameWeight,
                                prevTransform,curTransform,prevMatrix,curMatrix)) {
                                prevMatrix=curTransform;curMatrix=curTransform;
                            }
                        }''')


def asset_bus_source(text):
    text = '#include <cstdio>\n#include <vector>\n' + text
    start = text.index('std::shared_ptr<const AssetBus::Mount> AssetBus::mount(')
    end = text.index('std::optional<AssetBus::Read> AssetBus::resolve(', start)
    return text[:start] + (AUTH / 'native/asset_bus_mount.inl').read_text() + '\n' + text[end:]


def stage(args):
    from online_pipeline import manifest_source, profile_source, online_runtime_source
    from music_pipeline import driver
    out = args.output.resolve()
    for protected in ('extern', 'runtime-recomp/RecompiledFuncs', 'runtime-recomp/RecompiledPatches', 'patches'):
        if out.is_relative_to(ROOT / protected):
            raise ValueError('The vehicle stage must be a separate build directory')
    authored = sorted(p for p in (AUTH / 'native').rglob('*') if p.is_file())
    receipt = {'schema': 1, 'adapter': 1, 'scope': 'custom-car-playtest', 'inputs': {},
               'authored': {p.relative_to(AUTH).as_posix(): digest(p) for p in authored}}
    for path in authored:
        put(out / path.relative_to(AUTH), path.read_bytes())
    if args.sdk:
        for rev in (77, 80):
            path = args.sdk / f'reference-us-v{rev}/data.toml'
            receipt['inputs'][str(path.resolve())] = digest(path)
        put(out / 'native/vehicle_symbols.hpp', symbols(args.sdk).encode())
        receipt['outputs'] = {'native/vehicle_symbols.hpp': digest(out / 'native/vehicle_symbols.hpp')}
    transforms = (('launch',launch_source),('runtime',runtime_source),('bridge',bridge_source),('renderer',renderer_source),
                  ('manifest',manifest_source),('profile',profile_source),('online_runtime',online_runtime_source),('rt64_state',rt64_state_source),('rt64_transform',rt64_transform_source),('driver',driver),('asset_bus',asset_bus_source))
    for name, transform in transforms:
        path = getattr(args, name)
        if path:
            path = path.resolve(strict=True)
            if out.is_relative_to(path.parent) or path.is_relative_to(out): raise ValueError('Native input overlaps the vehicle stage')
            receipt['inputs'][str(path)] = digest(path)
            put(out / ('native/' + path.name), transform(path.read_text(encoding='utf-8')).encode())
    if args.ui:
        ui = args.ui.resolve(strict=True)
        library = args.library.resolve(strict=True)
        store = ui.parent / 'mod_store_ui.inl'
        online_ui = library.parent / 'runtime_online_mods_ui.inl'
        texture_scope = ROOT / 'runtime-recomp/src/game/local_scenery_texture_scope.hpp'
        for p in (ui, library, store, online_ui, texture_scope):
            if out.is_relative_to(p.parent) or p.is_relative_to(out):
                raise ValueError('Read-only source input overlaps the vehicle stage')
            receipt['inputs'][str(p)] = digest(p)
        value = ui.read_text(encoding='utf-8')
        value = once(value, '#include "mods/legacy_mod_launch.hpp"', '#include "mods/legacy_mod_launch.hpp"\n#include "vehicle_library.hpp"\n#include "vehicle_protocol.hpp"')
        value = once(value, 'void ImportMagicCodeWithDialog() {', 'void ImportVehicleWithDialog();\nvoid ReleaseVehicleTextures();\nvoid ImportMagicCodeWithDialog() {')
        value = once(value, '#include "mod_store_ui.inl"', '#include "mod_store_ui.inl"\n#include "vehicle_ui.inl"')
        value = once(value, 'g_code_library.view()->items.size(),width);', 'g_code_library.view()->items.size(),g_vehicle_library.view()->items.size(),width);')
        value = once(value, '            g_code_library.refresh();', '            g_code_library.refresh();\n            g_vehicle_library.job();')
        value = once(value, '    if(g_mods_page.category==2){DrawCustomMagicCodeLibrary(width,locked);return;}', '    if(g_mods_page.category==3){DrawVehicleLibrary(width,locked);return;}\n    if(g_mods_page.category==2){DrawCustomMagicCodeLibrary(width,locked);return;}')
        value = once(value, '                          go_to_play, [] { ImportMagicCodeWithDialog(); }});', '                          go_to_play, [] { ImportMagicCodeWithDialog(); }, [] { ImportVehicleWithDialog(); }});')
        value = once(value, '    DrawCustomMagicCodeDetails(mods_locked || picking);', '    DrawCustomMagicCodeDetails(mods_locked || picking);\n    DrawVehicleDetails(mods_locked || picking);')
        value = once(value, '    g_code_library.configure(config_directory / "mods" / "custom-magic-codes",DKR_RELEASE_VERSION);', '    g_code_library.configure(config_directory / "mods" / "custom-magic-codes",DKR_RELEASE_VERSION);\n    g_vehicle_library.configure(config_directory / "mods" / "vehicles");')
        value = once(value, '    const bool locked = mods_locked || mods.busy;\n    const std::size_t track_count', '    const bool locked = mods_locked || mods.busy || g_vehicle_library.view()->busy;\n    const std::size_t track_count')
        value = once(value, '    const bool mods_locked = game_running || lobby || g_mod_launch.snapshot().modal || OnlineModSelectionLocked();', '    const bool mods_locked = game_running || lobby || g_mod_launch.snapshot().modal || OnlineModSelectionLocked() || g_vehicle_library.view()->busy;')
        value = once(value, '"Your tracks, racers and race modifiers."', '"Your tracks, racers, vehicles and race modifiers."')
        put(out / 'native/runtime_ui.cpp', value.encode())
        value = library.read_text(encoding='utf-8')
        value = once(value,'struct LibraryCardView {','struct LibraryCardView {\n    float preview_height=0;\n    std::function<void(float,float)> preview;')
        value = once(value,'    if (view.prepare) height += 42.0F;', '    if (view.prepare) height += 42.0F;\n    if(view.preview)height+=view.preview_height+12.F;')
        value = once(value,'    bool details = false;\n    if (view.prepare)', '''    if(view.preview) {
        ImGui::SetCursorScreenPos({left,y});view.preview(inner,view.preview_height);y+=view.preview_height+12.F;
    }
    bool details = false;
    if (view.prepare)''')
        value = once(value, '    std::function<void()> choose_code;', '    std::function<void()> choose_code;\n    std::function<void()> choose_vehicle;')
        anchor = '    PaddockGap(18.0F);\n    if (PaddockButton("Cancel##import-chooser", PaddockButtonKind::Link))'
        option = '''    PaddockGap(18.0F);
    DrawModImportOption("vehicle",PaddockFormat::Dkr,"Custom vehicles",
        "Import a .dkrkart car or a vehicle ZIP. Choose your car from Track Select.",width,[&] {
        ImGui::BeginDisabled(actions.locked);
        if(PaddockButton("Choose car package")) {
            ImGui::CloseCurrentPopup();if(actions.choose_vehicle)actions.choose_vehicle();
        }
        ImGui::EndDisabled();
    });
'''
        value = once(value, anchor, option + anchor)
        value = once(value, 'std::size_t codes,float available)', 'std::size_t codes,std::size_t vehicles,float available)')
        value = once(value, 'const std::array<const char*, 3> labels{{"Tracks", "Characters", "Custom Magic Codes"}};', 'const std::array<const char*, 4> labels{{"Tracks", "Characters", "Custom Magic Codes", "Vehicles"}};')
        value = once(value, 'const std::array<std::string, 3> counts{{std::to_string(tracks), std::to_string(characters),std::to_string(codes)}};', 'const std::array<std::string, 4> counts{{std::to_string(tracks), std::to_string(characters),std::to_string(codes),std::to_string(vehicles)}};')
        # Only this function's fixed-size category arrays and loops expand.
        start = value.index('int DrawLibraryCategories(')
        prefix, categories = value[:start], value[start:]
        categories = once(categories, 'std::array<float, 3> widths{};', 'std::array<float, 4> widths{};')
        categories = once(categories, 'std::array<ImVec2,3> positions{};', 'std::array<ImVec2,4> positions{};')
        categories = once(categories, 'for(unsigned i=0;i<3;++i)', 'for(unsigned i=0;i<4;++i)')
        categories = once(categories, 'for (int index = 0; index < 3; ++index)', 'for (int index = 0; index < 4; ++index)')
        value = prefix + categories
        value = once(value, '            if (view.code) {\n                if(code_toggle)code_toggle(enabled);', '            if (code_toggle) {\n                code_toggle(enabled);')
        put(out / 'native/runtime_mod_library_ui.inl', value.encode())
        value = store.read_text(encoding='utf-8')
        value = once(value, 'void ReleaseModStoreTextures() {', 'void ReleaseModStoreTextures() {\n    ReleaseVehicleTextures();')
        put(out / 'native/mod_store_ui.inl', value.encode())
        value = online_ui.read_text(encoding='utf-8')
        value = once(value,'if(!profile->manifest.code.empty()&&host_mode &&','if((!profile->manifest.code.empty()||dkr::mods::online::car_count(profile->manifest))&&host_mode &&')
        value = once(value,'Custom Magic Codes need Auto or Experimental Rollback. Other online modes still support tracks and characters.','Custom cars and Magic Codes need Auto or Experimental Rollback.')
        value = once(value,'mod.kind==Kind::Character?"Character":mod.kind==Kind::TrackLab?"Track Lab":"Track"','mod.kind==dkr::mods::online::CarContent?"Car":mod.kind==Kind::Character?"Character":mod.kind==Kind::TrackLab?"Track Lab":"Track"')
        put(out / 'native/runtime_online_mods_ui.inl',value.encode())
        value = texture_scope.read_text(encoding='utf-8')
        value = once(value,'address>=0x00D00000U&&address<0x00E00000U','address>=0x00D00000U&&address<0x00F00000U')
        value = once(value,'if(!enabled_)return;',
            'if(!enabled_&&!active_&&!(address>=0x00E00000U&&address<0x00F00000U))return;')
        put(out / 'native/vehicle_texture_scope.hpp',value.encode())
    receipt.setdefault('outputs', {}).update({p.relative_to(AUTH).as_posix(): digest(out / p.relative_to(AUTH)) for p in authored})
    for name, transform in transforms:
        path = getattr(args, name)
        if path: receipt['outputs']['native/' + path.name] = digest(out / ('native/' + path.name))
    for name in ('native/runtime_ui.cpp', 'native/runtime_mod_library_ui.inl', 'native/mod_store_ui.inl', 'native/runtime_online_mods_ui.inl','native/vehicle_texture_scope.hpp'):
        if args.ui: receipt['outputs'][name] = digest(out / name)
    for p, expected in receipt['inputs'].items():
        if digest(Path(p)) != expected:
            raise ValueError('Read-only input changed while composing vehicles')
    put(out / 'vehicle-receipt.json', (json.dumps(receipt, indent=2) + '\n').encode())
    print('Custom car adapters staged; no game, mod or tests executed.')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--ui', type=Path)
    parser.add_argument('--library', type=Path)
    parser.add_argument('--sdk', type=Path)
    parser.add_argument('--launch', type=Path)
    parser.add_argument('--runtime', type=Path)
    parser.add_argument('--bridge', type=Path)
    parser.add_argument('--renderer', type=Path)
    parser.add_argument('--manifest', type=Path)
    parser.add_argument('--profile', type=Path)
    parser.add_argument('--online-runtime', type=Path)
    parser.add_argument('--rt64-state', type=Path)
    parser.add_argument('--rt64-transform', type=Path)
    parser.add_argument('--driver', type=Path)
    parser.add_argument('--asset-bus', type=Path)
    stage(parser.parse_args())
