"""Compose SDK 2 only into a fresh, receipted Patch Pipeline payload.

Checked-out native sources, dependencies, generated game code and all earlier
payloads are read-only inputs. This script never launches DKR-R or a mod.
"""
from pathlib import Path
import argparse
import hashlib
import importlib.util
import json
import shutil
import sys

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[2]
AUTH = Path(__file__).resolve().parent


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def once(text, old, new):
    if text.count(old) != 1:
        raise ValueError('Reviewed SDK anchor changed: ' + old[:130])
    return text.replace(old, new)


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def authors():
    files = [AUTH/'pipeline.py', AUTH/'NativeOverlay.cmake',AUTH/'reuse_stage.py',AUTH/'network_pipeline.py',AUTH/'lobby_pipeline.py',AUTH/'catalogue_pipeline.py',AUTH/'control_pipeline.py',AUTH/'presentation_pipeline.py',AUTH/'reload_pipeline.py',AUTH/'settings_pipeline.py']
    files += sorted((AUTH/'native').glob('*')) + sorted((AUTH/'sdk').glob('*'))
    files += sorted((AUTH/'mod').glob('*'))
    return {p.relative_to(ROOT).as_posix(): digest(p) for p in files if p.is_file()}


def transform(name, text):
    if name == 'magic_code_package.hpp':
        text=once(text,'    Json options=Json::array();','    Json options=Json::array();\n    std::map<std::string,std::string> debug_lines;\n    unsigned sdk2_heap=65536,sdk2_instructions=200000,sdk2_native_calls=128;\n    std::string sdk2_profile="standard";')
        return once(text, 'bool deterministic=false;', 'bool deterministic=false;\n    bool development=false;')
    if name == 'magic_code_package.cpp':
        text=once(text,'#include "magic_code_package.hpp"','#include "magic_code_package.hpp"\n#include <cmath>\n#include "sdk2_schema.hpp"')
        text = once(text, 'if(meta.value("schema",0)!=1||meta.value("api",0)!=ApiVersion)bad("unsupported DKR SDK version");',
            '''if(!meta.contains("schema")||!meta["schema"].is_number_unsigned()||meta["schema"]!=1||!meta.contains("api")||!meta["api"].is_number_unsigned()||(meta["api"]!=1&&meta["api"]!=2))bad("unsupported DKR SDK version");
    p->api=meta.at("api").get<unsigned>();
    if(meta.contains("development")&&!meta["development"].is_boolean())bad("invalid development-session declaration");
    p->development=p->api==2&&meta.value("development",false);''')
        text=once(text,'    p->binary=archive.require("mod_binary.bin");', '''    if(p->api==2&&meta.contains("profile")) {
        if(!meta["profile"].is_string())bad("SDK profile must be compact, standard or large");
        p->sdk2_profile=meta["profile"].get<std::string>();
        if(p->sdk2_profile=="compact"){p->sdk2_heap=32768;p->sdk2_instructions=100000;p->sdk2_native_calls=32;}
        else if(p->sdk2_profile=="large"){p->sdk2_heap=262144;p->sdk2_instructions=500000;p->sdk2_native_calls=256;}
        else if(p->sdk2_profile!="standard")bad("unknown portable SDK profile");
    }
    p->binary=archive.require("mod_binary.bin");''')
        # Module names remain versioned capabilities, not a global reinterpretation
        # of every existing API 1 module. API 1 rejects v2 requirements as before.
        text = once(text, 'if(!valid_id(name)||!version.is_number_unsigned()||version.get<std::uint64_t>()!=1)bad("unsupported SDK module version");\n            p->modules.emplace(name,1);',
            '''if(!valid_id(name)||!version.is_number_unsigned()||version.get<std::uint64_t>()<1||version.get<std::uint64_t>()>p->api)bad("unsupported SDK module version");
            p->modules.emplace(name,version.get<unsigned>());''')
        text = once(text, 'Json value={{"api",ApiVersion},{"execution",ExecutionVersion},{"revision",revision},{"packages",Json::array()}};',
            '''const bool sdk2=std::any_of(packages.begin(),packages.end(),[](const auto& p){return p->api==2;});
    Json value={{"api",sdk2?2:ApiVersion},{"execution",sdk2?2:ExecutionVersion},{"revision",revision},{"packages",Json::array()}};''')
        text=once(text,'    p->binary=archive.require("mod_binary.bin");', '''    sdk2_schema(*p);
    if(p->api==2&&p->resources.contains("source-map")) {
        const auto map=parse_document(p->resources.at("source-map"),1024*1024);
        if(map.value("schema",0)!=2||!map.contains("functions")||!map["functions"].is_array()||map["functions"].size()>16384)bad("invalid SDK 2 source map");
        for(const auto& row:map["functions"]) {
            if(!row.is_object()||!row.contains("name")||!row["name"].is_string()||row["name"].get_ref<const std::string&>().size()>512||
               !row.contains("file")||!row["file"].is_string()||row["file"].get_ref<const std::string&>().size()>512||
               !row.contains("line")||!row["line"].is_number_unsigned()||row["line"].get<std::uint64_t>()>10000000)bad("invalid SDK source-map row");
            const auto file=row["file"].get<std::string>();
            if(file.find("..")!=file.npos||file.find(':')!=file.npos||file.find('\\\\')!=file.npos||std::any_of(file.begin(),file.end(),[](unsigned char c){return c<32||c==127;}))bad("unsafe source-map filename");
            if(!file.empty()&&row["line"].get<unsigned>())p->debug_lines.emplace(row["name"].get<std::string>(),file+":"+std::to_string(row["line"].get<unsigned>()));
        }
    }
    p->binary=archive.require("mod_binary.bin");''')
        return text
    if name == 'magic_code_link.cpp':
        text=once(text,'owner.heap_size=64*1024;', 'owner.heap_size=p.api==2?p.sdk2_heap:64*1024;')
        text=once(text,'    std::set<std::string> enabled;', '''    unsigned instruction_requests=0,native_requests=0,simulation_owners=0,tuning_owners=0;
    for(const auto& p:ordered)if(p&&p->api==2) {
        instruction_requests+=p->sdk2_instructions;native_requests+=p->sdk2_native_calls;
        if(p->modules.contains("simulation2")&&++simulation_owners>1)bad("only one simulation control provider can own a session");
        if(p->modules.contains("racer-tuning2")&&++tuning_owners>1)bad("only one racer tuning provider can own a session");
    }
    if(instruction_requests>2000000||native_requests>1024)bad("SDK 2 work profiles exceed the portable session budget");
    std::set<std::string> enabled;''')
        text=once(text,'            if(dep=="*"&&!policy.events.contains(event.event_name))bad("unavailable SDK event: "+event.event_name);', '            if(dep=="*"&&event.event_name=="on_tool"&&p.api!=2)bad("on_tool requires executable API 2");\n            if(dep=="*"&&!policy.events.contains(event.event_name))bad("unavailable SDK event: "+event.event_name);')
        return once(text, 'owner.services=allocate(4096);out->regions.push_back({owner.services,4096,true,false});owner.end=cursor;',
            '''const unsigned service_bytes=p.api==2?65536U:4096U;
        owner.services=allocate(service_bytes);out->regions.push_back({owner.services,service_bytes,true,false});owner.end=cursor;''')
    if name == 'magic_code_world.cpp':
        text=once(text, 'if(name=="dkr_sdk_version"){c.integer[2]=ApiVersion;return;}',
            'if(name=="dkr_sdk_version"){c.integer[2]=owner.package->api;return;}')
        text=once(text,'        throw Error(std::string(e.what())+context);', '''        std::string location;
        const auto& package=*linked_->owners[owner].package;
        if(package.api==2&&!package.debug_lines.empty()) {
            const auto& functions=linked_->owners[owner].functions;const auto found=std::find(functions.begin(),functions.end(),function);
            if(found!=functions.end()) {
                const auto& name=package.symbols->functions[std::size_t(found-functions.begin())].name;
                if(const auto line=package.debug_lines.find(name);line!=package.debug_lines.end())location=" [callback-source="+line->second+"]";
            }
        }
        throw Error(std::string(e.what())+context+location);''')
        text=once(text,'            c.floating[0]=std::bit_cast<std::uint64_t>(value.get<double>());return;', '''            if(owner.package->api==2) {
                const auto& options=owner.package->options;
                const auto found=std::find_if(options.begin(),options.end(),[&](const auto& option){return option.at("id")==key;});
                if(found!=options.end()) {
                    const auto at=owner.services+28672+unsigned(found-options.begin())*8;
                    const auto tag=bus.load(at,4);const auto raw=std::uint32_t(bus.load(at+4,4));
                    if(tag==1||tag==2){const double live=tag==1?double(std::bit_cast<float>(raw)):double(raw);c.floating[0]=std::bit_cast<std::uint64_t>(live);return;}
                }
            }
            c.floating[0]=std::bit_cast<std::uint64_t>(value.get<double>());return;''')
        text=once(text,'#include <cstdio>','#include <cstdio>\n#include <chrono>')
        text=once(text,'    const auto owner=found->second;auto& depth=calls_.at(owner);', '''    const auto owner=found->second;auto& depth=calls_.at(owner);
    const auto& package=*linked_->owners[owner].package;
    const auto before_instructions=budget.instructions;const auto before_native=budget.native_calls;
    const unsigned instruction_cap=package.api==2?package.sdk2_instructions:200000;
    const unsigned native_cap=package.api==2?package.sdk2_native_calls:128;
    const auto reserved_instructions=budget.instructions>instruction_cap?budget.instructions-instruction_cap:0;
    const auto reserved_native=budget.native_calls>native_cap?budget.native_calls-native_cap:0;
    budget.instructions-=reserved_instructions;budget.native_calls-=reserved_native;
    const auto started=std::chrono::steady_clock::now();
    struct Costs {
        Budget& budget;Metric& metric;std::uint64_t instructions,reserved_i;unsigned native,reserved_n;
        std::chrono::steady_clock::time_point started;
        ~Costs(){budget.instructions+=reserved_i;budget.native_calls+=reserved_n;
            ++metric.callbacks;metric.instructions+=instructions-budget.instructions;metric.native_calls+=native-budget.native_calls;
            metric.nanoseconds+=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-started).count();}
    } costs{budget,metrics_[owner],before_instructions,reserved_instructions,before_native,reserved_native,started};''')
        text=once(text,'calls_.push_back(0);','calls_.push_back(0);metrics_.emplace_back();')
        return text
    if name == 'magic_code_world.hpp':
        return once(text,'    bool idle()const', '''    struct Metric {std::uint64_t callbacks=0,instructions=0,native_calls=0,nanoseconds=0;};
    const std::vector<Metric>& local_metrics()const{return metrics_;}
    bool idle()const''').replace('    std::vector<unsigned> calls_;','    std::vector<unsigned> calls_;\n    std::vector<Metric> metrics_;')
    if name == 'magic_code_game_world.hpp':
        text=once(text,'#include <mutex>','#include <mutex>\n#include "sdk2_bridge.hpp"')
        text=once(text,'    const std::string& error()const', '''    unsigned sdk2_pulse();
    int sdk2_dispatch_pulse()noexcept;
    bool sdk2_frame_starts_at(std::uint32_t)const;
    void sdk2_command(const dkr::sdk2::Command&);
    void sdk2_development(bool value){sdk2_development_=value;}
    void sdk2_frame_agreed(bool value){sdk2_frame_agreed_=value;}
    void sdk2_current_course(std::function<std::string()> query){sdk2_current_course_=std::move(query);}
    void sdk2_assets(const std::string& value){if(!value.empty())sdk2_assets_=parse_document(View(reinterpret_cast<const std::uint8_t*>(value.data()),value.size()),2*MiB);}
    bool sdk2_wants_assets()const{for(const auto& o:linked().owners)if(o.package->api==2&&o.package->modules.contains("catalogues2"))return true;return false;}
    const std::string& error()const''')
        return once(text, '    static Registers read(const recomp_context&);', '''    bool sdk2_enabled()const;
    void sdk2_boundary(unsigned);
    void sdk2_inspector();
    void sdk2_lifetime(std::string_view,bool,const Registers&);
    void sdk2_emit(Memory&,std::uint32_t,unsigned,unsigned,unsigned,unsigned,unsigned,unsigned,unsigned);
    bool sdk2_service(std::string_view,std::size_t,Memory&,Registers&,Budget&);
    bool sdk2_operation_available(unsigned)const;
    int sdk2_operation(unsigned,std::size_t,Memory&,const std::array<std::uint32_t,16>&,std::array<std::uint32_t,16>&,Budget&);
    int sdk2_cast(Memory&,std::uint32_t,std::uint32_t,Budget&);
    int sdk2_track_catalogue(Memory&,std::string_view,unsigned,Json&,Budget&);
    int sdk2_control(Memory&,std::size_t,unsigned,unsigned);
    int sdk2_tune(Memory&,std::size_t,unsigned);
    void sdk2_vehicle(std::string_view,bool,const Registers&);
    void sdk2_apply_overrides(std::size_t,unsigned,bool);
    void sdk2_shading(std::string_view,bool,const Registers&);
    bool sdk2_freeze(std::string_view)const;
    void sdk2_native_return(std::string_view,Registers&);
    int sdk2_document(std::size_t,Memory&,std::string_view,Registers&,Budget&);
    bool sdk2_document_command(const dkr::sdk2::Command&,std::size_t,Memory&);
    std::string sdk2_course_identity()const;
    unsigned sdk2_phase_=1;
    bool sdk2_command_=false;
    bool sdk2_development_=false;
    bool sdk2_active_=false;
    bool sdk2_pulsing_=false;
    bool sdk2_frame_agreed_=false;
    unsigned sdk2_inspector_serial_=0;
    Budget sdk2_budget_{};
    std::map<std::string,unsigned> sdk2_modules_;
    std::vector<trophy::Course> sdk2_courses_;
    Json sdk2_inventory_=Json::object(),sdk2_cups_=Json::array();
    Json sdk2_assets_=Json::object();
    std::function<std::string()> sdk2_current_course_;
    static Registers read(const recomp_context&);''')
    if name == 'magic_code_game_world.cpp':
        text = once(text, '#include <cstddef>', '#include <cstddef>\n#include <bit>\n#include <cmath>')
        text = once(text, 'LinkPolicy GameWorld::policy(', '#include "sdk2_world.inl"\n#include "sdk2_operations.inl"\n#include "sdk2_collision.inl"\n#include "sdk2_track_catalogue.inl"\n#include "sdk2_simulation.inl"\n#include "sdk2_documents.inl"\n#include "sdk2_tuning.inl"\n#include "sdk2_overrides.inl"\nLinkPolicy GameWorld::policy(')
        text = once(text, '    auto p=World::core_policy();p.online=online;', '''    auto p=World::core_policy();p.online=online;
    p.modules.insert({{"reflection2",2},{"entities2",2},{"transactions2",2},{"inspection2",2},{"tools-ui2",2},{"operations2",2},{"live-settings2",2},{"catalogues2",2},{"collision2",2},{"events2",2}});
    p.services.insert({"dkr2_capability","dkr2_session","dkr2_property","dkr2_entities","dkr2_read","dkr2_edit","dkr2_symbols","dkr2_memory_read","dkr2_ui_publish","dkr2_action_next","dkr2_operation_available","dkr2_operation","dkr2_setting_set","dkr2_setting_reset","dkr2_catalogue","dkr2_cast","dkr2_events","dkr2_event_next","dkr2_debug"});''')
        text=once(text,'    if(world_.linked().identity!=frozen.identity)', '''    sdk2_active_=std::any_of(frozen.ordered.begin(),frozen.ordered.end(),[](const auto& p){return p->api==2;});
    if(sdk2_active_)sdk2_modules_=policy(table_,online_).modules;
    if(sdk2_active_)for(const auto& p:frozen.ordered)if(p->api==2){sdk2_budget_.instructions+=p->sdk2_instructions;sdk2_budget_.native_calls+=p->sdk2_native_calls;}
    sdk2_development_=!online_&&std::any_of(frozen.ordered.begin(),frozen.ordered.end(),[](const auto& p){return p->development;});
    for(const auto& p:frozen.ordered)if(p->api==2) {
        const auto data=frozen.data.find(p->id);
        if(data!=frozen.data.end()&&data->second.contains("_sdk2_catalogue")) {
            const auto courses=trophy::read_catalogue(data->second.at("_sdk2_catalogue").at("courses"));
            if(!sdk2_courses_.empty()&&sdk2_courses_!=courses)throw Error("SDK 2 packages disagree on their frozen resource catalogue.");
            sdk2_courses_=courses;
            const auto& inventory=data->second.at("_sdk2_catalogue").at("inventory");
            if(!sdk2_inventory_.empty()&&sdk2_inventory_!=inventory)throw Error("SDK 2 packages disagree on their admitted resource inventory.");
            sdk2_inventory_=inventory;
        }
    }
    for(const auto& cup:trophy::menu_entries(frozen))sdk2_cups_.push_back({{"id",cup.id},{"name",cup.name}});
    if(world_.linked().identity!=frozen.identity)''')
        text=once(text,'    std::lock_guard lock(mutex_);world_.attach(ram,initial);ram_=ram;', '''    std::lock_guard lock(mutex_);world_.attach(ram,initial);ram_=ram;
    if(initial&&sdk2_enabled()) {
        std::vector<std::shared_ptr<const Package>> packages;for(const auto& o:linked().owners)packages.push_back(o.package);
        dkr::sdk2::begin(packages,online_);
    }''')
        text = once(text, 'bool active=linked().hooks.contains(entry.address)||named_events_.contains(entry.address)||entry.after_event==DKR_CODE_EVENT_FRAME_END;',
            '''bool active=linked().hooks.contains(entry.address)||named_events_.contains(entry.address)||entry.after_event==DKR_CODE_EVENT_FRAME_END;
        if(sdk2_enabled()&&(entry.before_event||entry.after_event||(entry.name&&(std::string_view(entry.name)=="free_object"||std::string_view(entry.name)=="menu_pause_loop"))))active=true;''')
        text = once(text, '    if(game_memory_service(name,bus,c,budget,', '    if(sdk2_service(name,owner,bus,c,budget))return true;\n    if(game_memory_service(name,bus,c,budget,')
        text = once(text, '    if(event==DKR_CODE_EVENT_FRAME)for(const auto& owner:linked().owners)', '''    sdk2_boundary(event);
    const auto previous_phase=sdk2_phase_;
    sdk2_phase_=event==DKR_CODE_EVENT_FRAME||event==DKR_CODE_EVENT_FRAME_END?2:1;
    struct Phase{unsigned& value;unsigned previous;~Phase(){value=previous;}} phase{sdk2_phase_,previous_phase};
    if(event==DKR_CODE_EVENT_FRAME)for(const auto& owner:linked().owners)''')
        text=once(text,'    world_.event("*",Events[event],args,budget_,outgoing?&*outgoing:nullptr);', '    world_.event("*",Events[event],args,budget_,outgoing?&*outgoing:nullptr);\n    if(event==DKR_CODE_EVENT_FRAME_END)sdk2_inspector();')
        text = once(text, '        const auto lifecycle=leaving?entry->after_event:entry->before_event;', '''        sdk2_lifetime(entry->name?entry->name:"",leaving,read(*context));
        const auto previous_phase=sdk2_phase_;
        const std::string_view function_name=entry->name?entry->name:"";
        if(function_name.find("render")!=function_name.npos||function_name.starts_with("draw_")||function_name.starts_with("hud_"))sdk2_phase_=4;
        struct Phase{unsigned& value;unsigned previous;~Phase(){value=previous;}} phase{sdk2_phase_,previous_phase};
        const auto lifecycle=leaving?entry->after_event:entry->before_event;''')
        text=once(text,'    native_bus_=std::make_unique<Memory>(ram,std::move(regions));', '''    native_bus_=std::make_unique<Memory>(ram,std::move(regions));
    if(initial)for(const auto& o:linked().owners)if(o.package->api==2) {
        native_bus_->store(o.services+sdk2::Header,4,sdk2::Magic);native_bus_->store(o.services+sdk2::Header+4,4,1);
    }''')
        text=text.replace('if(nested_==0)budget_=Budget{};','if(nested_==0)budget_=sdk2_budget_;')
        text=once(text,'std::lock_guard lock(mutex_);Budget budget;world_.initialize(budget);','std::lock_guard lock(mutex_);Budget budget=sdk2_budget_;world_.initialize(budget);')
        text=once(text,'if(!world_.has_hook(address)&&named==named_events_.end()){result=0;return 1;}', 'if(!world_.has_hook(address)&&named==named_events_.end()&&!(sdk2_enabled()&&((leaving&&function_name=="menu_pause_loop")||(!leaving&&function_name=="update_AI_racer")))){result=0;return 1;}')
        text=once(text,'        if(leaving){world_.leave(address,registers,budget_,native_arguments);result=callback.repeat?1:0;}', '        if(leaving){world_.leave(address,registers,budget_,native_arguments);sdk2_native_return(function_name,registers);result=callback.repeat?1:0;}')
        text=once(text,'    p.modules.emplace("game-hooks",1);','    p.modules.emplace("racer-tuning2",2);p.services.insert("dkr2_tune");\n    p.modules.emplace("game-hooks",1);')
        text=once(text,'        if(active)hook_mask_', '        if(sdk2_active_&&entry.name&&sdk2::vehicle_update(entry.name)&&std::any_of(frozen.ordered.begin(),frozen.ordered.end(),[](const auto& p){return p->api==2&&p->modules.contains("racer-tuning2");}))active=true;\n        if(active)hook_mask_')
        text=once(text,'    p.modules.emplace("racer-tuning2",2);p.services.insert("dkr2_tune");', '    p.modules.emplace("racer-tuning2",2);p.services.insert({"dkr2_tune","dkr2_freeze_cpus"});')
        text=once(text,'    p.modules.emplace("game-hooks",1);','    p.modules.emplace("overrides2",2);p.services.insert("dkr2_override");\n    p.modules.emplace("game-hooks",1);')
        text=once(text,'        if(active)hook_mask_', '        if(sdk2_active_&&entry.name&&std::string_view(entry.name)=="light_update_shading"&&std::any_of(frozen.ordered.begin(),frozen.ordered.end(),[](const auto& p){return p->api==2&&p->modules.contains("overrides2");}))active=true;\n        if(active)hook_mask_')
        text=once(text,'entry.name&&sdk2::vehicle_update(entry.name)&&std::any_of', 'entry.name&&(sdk2::vehicle_update(entry.name)||std::string_view(entry.name)=="update_AI_racer")&&std::any_of')
        text=once(text,'else result=callback.skip||world_.enter(address,registers,budget_,native_arguments)?1:0;', 'else result=callback.skip||world_.enter(address,registers,budget_,native_arguments)||sdk2_freeze(function_name)?1:0;')
        text=once(text,'    p.events.insert({"on_frame","on_frame_end"});','    p.events.insert({"on_frame","on_frame_end","on_tool"});\n    p.modules.emplace("simulation2",2);p.modules.emplace("track-tools2",2);p.modules.emplace("documents2",2);p.modules.emplace("presentation2",2);p.services.insert({"dkr2_control","dkr2_route","dkr2_document_publish","dkr2_document_read","dkr2_document_size","dkr2_document_accept"});')
        return text
    raise ValueError('Unreviewed SDK source: ' + name)


TRANSFORMS = ('magic_code_package.hpp','magic_code_package.cpp','magic_code_link.cpp',
              'magic_code_world.hpp','magic_code_world.cpp','magic_code_game_world.hpp','magic_code_game_world.cpp')


def inputs(camera, ui, shadow):
    reloads=load('sdk2_reload_transforms',AUTH/'reload_pipeline.py')
    settings=load('sdk2_settings_transforms',AUTH/'settings_pipeline.py')
    for filename in TRANSFORMS:
        path=camera/'src/game/mods'/filename
        text=transform(filename,path.read_text())
        text=reloads.transform(filename,text,once)
        text=settings.transform(filename,text,once)
        yield Path('native/src/game/mods')/filename, text, path
    for path in sorted((AUTH/'native').glob('*')):
        if path.is_file():yield Path('native/src/game/mods')/path.name,path.read_text(),path
    network=load('sdk2_network_transforms',AUTH/'network_pipeline.py')
    lobby=load('sdk2_lobby_transforms',AUTH/'lobby_pipeline.py')
    catalogue=load('sdk2_catalogue_transforms',AUTH/'catalogue_pipeline.py')
    controls=load('sdk2_control_transforms',AUTH/'control_pipeline.py')
    presentation=load('sdk2_presentation_transforms',AUTH/'presentation_pipeline.py')
    for filename in sorted(set(network.FILES+lobby.FILES+catalogue.FILES+controls.FILES+presentation.FILES)):
        path=camera/filename
        if filename.startswith('src/game/') and (ui/filename).is_file():path=ui/filename
        if (shadow/'native'/filename).is_file():path=shadow/'native'/filename
        text=path.read_text()
        if filename in network.FILES:text=network.transform(filename,text,once)
        if filename in lobby.FILES:text=lobby.transform(filename,text,once)
        if filename in catalogue.FILES:text=catalogue.transform(filename,text,once)
        if filename in controls.FILES:text=controls.transform(filename,text,once)
        if filename in presentation.FILES:text=presentation.transform(filename,text,once)
        if filename=='src/game/game_main.cpp':
            text=once(text,'dkr::runtime::camera::begin(config_directory,DKR_RELEASE_VERSION,registered_revision,online_save.active);', 'dkr::runtime::camera::begin(config_directory,DKR_RELEASE_VERSION,registered_revision,online_save.active,code_set&&std::any_of(code_set->ordered.begin(),code_set->ordered.end(),[](const auto& p){return p->api==2&&p->modules.contains("presentation2");}));')
        if filename=='src/game/netplay/experimental_runtime.cpp':
            text=once(text,'#include "experimental_runtime.hpp"','#include "experimental_runtime.hpp"\n#include "../mods/sdk2_bridge.hpp"')
            text=once(text,'        OwnerActivity::Scope stage(shared.save_activity,OwnerStage::SaveWrite);', '''        OwnerActivity::Scope stage(shared.save_activity,OwnerStage::SaveWrite);
        if(launch->development){message.clear();return true;}''')
            text=once(text,'mods?mods->code():nullptr,std::move(code_bootstrap),launch->match_id});','mods?mods->code():nullptr,std::move(code_bootstrap),launch->match_id,launch->development});')
            text=once(text,'            if(!world)throw std::runtime_error(message);','            if(!world)throw std::runtime_error(message);\n            dkr::sdk2::authority(initial.host,launch->development);')
            text=once(text,'Pump pump(session,network);', '''Pump pump(session,network);
                pump.command_source([local,development=launch->development]{return local==0&&development?dkr::sdk2::take(true):dkr::sdk2::Command{};});''')
        if filename=='tests/replay_probe/probe_driver.cpp':
            text=once(text,'            code_->attach(ram_,true);code_->commit(code_->stage(boot.code_bootstrap));','            code_->sdk2_development(boot.development);\n            code_->attach(ram_,true);code_->commit(code_->stage(boot.code_bootstrap));')
        if filename=='src/game/runtime_ui.cpp':
            text=once(text,'#include "camera_runtime.hpp"','#include "camera_runtime.hpp"\n#include "mods/sdk2_bridge.hpp"')
            text=once(text,'#include "runtime_code_mods_ui.inl"','#include "runtime_code_mods_ui.inl"\n#include "mods/sdk2_tools_ui.inl"')
            text=once(text,'void DrawOverlayContent(float content_width) {', '''void DrawOverlayContent(float content_width) {
    if(dkr::sdk2::available()) {
        if(ImGui::Button("CUSTOM MAGIC CODE TOOLS",{std::min(content_width,320.F),42})) {
            dkr::sdk2::tools_open=true;g_overlay_visible=false;
        }
    }''')
            text=once(text,'!show_online_waiting && !show_mipmap_loading && !show_camera_tuning && !keep_owned_ui)', '!show_online_waiting && !show_mipmap_loading && !show_camera_tuning && !keep_owned_ui && !dkr::sdk2::tools_open.load() && !dkr::sdk2::debug_available())')
            text=once(text,'    if (show_camera_tuning && !show_overlay) DrawCameraTuningPanel();','    if (show_camera_tuning && !show_overlay) DrawCameraTuningPanel();\n    DrawSdk2Debug();\n    if(dkr::sdk2::tools_open.load())DrawSdk2Tools();')
            text=once(text,'    if (HandleInputCaptureEvent(event)) {', '''    if(g_sdk2_key_capture&&event->type==SDL_KEYDOWN&&!event->key.repeat) {
        if(event->key.keysym.scancode!=SDL_SCANCODE_ESCAPE)g_sdk2_tools_key=event->key.keysym.scancode;
        g_sdk2_key_capture=false;SaveSettings();return true;
    }
    if(dkr::sdk2::available()&&g_sdk2_tools_chord&&(event->type==SDL_CONTROLLERBUTTONDOWN||event->type==SDL_CONTROLLERBUTTONUP)) {
        const auto button=event->cbutton.button;const unsigned flag=button==SDL_CONTROLLER_BUTTON_BACK?1U:button==SDL_CONTROLLER_BUTTON_Y?2U:0U;
        if(flag) {
            auto& held=g_sdk2_chord_held[event->cbutton.which];const auto previous=held;
            if(event->type==SDL_CONTROLLERBUTTONDOWN)held|=flag;else held&=~flag;
            if(held==3&&previous!=3){dkr::sdk2::tools_open=!dkr::sdk2::tools_open.load();g_overlay_visible=false;return true;}
        }
    }
    if(dkr::sdk2::available()&&event->type==SDL_KEYDOWN&&!event->key.repeat&&event->key.keysym.scancode==g_sdk2_tools_key) {
        dkr::sdk2::tools_open=!dkr::sdk2::tools_open.load();return true;
    }
    if(dkr::sdk2::tools_open.load()&&g_text_entry_target==TextEntryTarget::None&&event->type==SDL_KEYDOWN&&!event->key.repeat&&event->key.keysym.scancode==SDL_SCANCODE_ESCAPE) {
        dkr::sdk2::tools_open=false;return true;
    }
    if (HandleInputCaptureEvent(event)) {''')
            text=once(text,'bool dkr::runtime::ui::overlay_visible() {\n    return g_overlay_visible.load(std::memory_order_acquire);', 'bool dkr::runtime::ui::overlay_visible() {\n    return g_overlay_visible.load(std::memory_order_acquire)||dkr::sdk2::tools_open.load();')
        yield Path('native')/filename,text,path


def stage(camera, ui, shadow, output):
    camera,ui,shadow=[p.resolve(strict=True) for p in (camera,ui,shadow)]
    verifier=load('sdk2_shadow_base',ROOT/'patches/camera-shadow/pipeline.py')
    verifier.inspect(shadow)
    if output.exists():raise ValueError('SDK output must be a NEW directory')
    output=output.resolve()
    if not output.is_relative_to(ROOT/'build'):raise ValueError('SDK payload must stay in build/')
    composed=list(inputs(camera,ui,shadow))
    # Complete native tree keeps quoted-header layouts consistent across all
    # static libraries and CPU ownership domains. Sources are only copied.
    output.mkdir(parents=True)
    shutil.copytree(camera/'src',output/'native/src')
    shutil.copytree(camera/'tests',output/'native/tests')
    if (camera/'include').exists():shutil.copytree(camera/'include',output/'native/include')
    for path in (ui/'src/game').iterdir():
        if path.is_file():shutil.copy2(path,output/'native/src/game'/path.name)
    for path in (shadow/'native/src/game').iterdir():
        if path.is_file():shutil.copy2(path,output/'native/src/game'/path.name)
    # Preserve checked reference includes relative to native/src/game/mods.
    for revision in ('77','80'):
        shutil.copytree(ROOT/f'modding/reference-us-v{revision}',output/f'modding/reference-us-v{revision}')
    for relative,text,_ in composed:
        target=output/relative;target.parent.mkdir(parents=True,exist_ok=True);target.write_text(text,newline='\n')
    # Fresh SDK kit retains all API 1 examples and reference layouts unchanged.
    shutil.copytree(ROOT/'modding',output/'sdk',ignore=shutil.ignore_patterns('__pycache__','*.pyc'))
    for path in (AUTH/'sdk').iterdir():
        if path.suffix in ('.h','.hpp'):shutil.copy2(path,output/'sdk/include/dkr'/path.name)
    for path in (AUTH/'sdk').glob('*.py'):shutil.copy2(path,output/'sdk'/path.name)
    for filename in ('magic_codes.h','game.h'):
        path=output/'sdk/include/dkr'/filename;header=path.read_text()
        # C ABI linkage for optional freestanding C++, without changing any
        # original API 1 declarations, records or C compiler interpretation.
        lines=header.splitlines();guard=next(i for i,line in enumerate(lines) if line.startswith('#define DKR_') and i<5)
        lines[guard+1:guard+1]=['#ifdef __cplusplus','extern "C" {','#endif']
        closing=max(i for i,line in enumerate(lines) if line.startswith('#endif'))
        lines[closing:closing]=['#ifdef __cplusplus','}','#endif']
        header='\n'.join(lines)+'\n'
        if filename=='magic_codes.h':header=once(header,'    DkrGameCall call={0};DkrU32 stack[16]', '#ifdef __cplusplus\n    DkrGameCall call={};\n#else\n    DkrGameCall call={0};\n#endif\n    DkrU32 stack[16]')
        path.write_text(header)
    presentation=camera/'include/dkr/presentation.h'
    if presentation.is_file():shutil.copy2(presentation,output/'sdk/include/dkr/presentation.h')
    shutil.copytree(AUTH/'mod',output/'sdk/examples/dev-menu')
    ui_schema=load('sdk2_dev_menu_schema',AUTH/'mod/tools.py').schema()
    (output/'sdk/examples/dev-menu/tools.json').write_text(json.dumps(ui_schema,indent=2)+'\n')
    # A separate API 2 edition demonstrates explicitly opted-in cross-mod live
    # controls. The existing API 1 difficulty package/source is unchanged.
    live=output/'sdk/examples/live-ai-difficulty'
    shutil.copytree(output/'sdk/examples/ai-boss-difficulty',live)
    meta=json.loads((live/'dkr.json').read_text());meta['api']=2
    meta['requires']['live-settings2']=2;meta['live_options']=['race_ai','tricky','bluey','smokey','bubbler','wizpig','rocket']
    (live/'dkr.json').write_text(json.dumps(meta,indent=2)+'\n')
    manifest=(live/'mod.toml').read_text().replace('id = "dkr_ai_boss_difficulty"','id = "dkr_live_ai_boss_difficulty"').replace('version = "1.0.0"','version = "1.1.0"').replace('display_name = "AI & Boss Difficulty"','display_name = "Live AI & Boss Difficulty (SDK 2)"').replace('Changes apply next game.','Admitted settings apply next game; the Dev Menu can adjust explicitly opted-in values live in development sessions.')
    (live/'mod.toml').write_text(manifest)
    source=(live/'main.c').read_text().replace('#include <dkr/magic_codes.h>','#include <dkr/development.h>')
    source+='\nDKR_CALLBACK(on_live_setting) void update_live_settings(unsigned option,unsigned bits,unsigned tag) {\n    (void)option;(void)bits;(void)tag;boot();\n}\n'
    (live/'main.c').write_text(source)
    tool=(output/'sdk/build.py').read_text()
    tool=once(tool,"meta.get('api') != 1", "meta.get('api') not in (1, 2)")
    tool=once(tool,"    for index, source in enumerate([SDK/'runtime.c',*sorted(sources)]):", "    # Keep complete HI16/LO16 address expressions for relocation at arbitrary\n    # mod bases; machine CSE can otherwise share a LUI across different carries.\n    flags += ['-fdata-sections', '-mllvm', '-disable-machine-cse']\n    for index, source in enumerate([SDK/'runtime.c',*sorted(sources)]):")
    tool=once(tool,"    p.add_argument('--lld', default='ld.lld')", "    p.add_argument('--lld', default='ld.lld')\n    p.add_argument('--source-map', action='store_true', help='Include callback source locations in this development package')")
    tool=once(tool,"source.suffix != '.c'", "source.suffix not in ('.c', '.cpp', '.cc')")
    tool=once(tool,'Only bounded portable C sources are supported','Only bounded freestanding C or C++ sources are supported')
    tool=once(tool,"subprocess.run([clang, *flags, '-c', str(source), '-o', str(obj)], check=True)",
        "extra = ['-fno-exceptions', '-fno-rtti', '-fno-threadsafe-statics', '-std=c++17'] if source.suffix in ('.cpp','.cc') else []\n        subprocess.run([clang, *flags, *extra, '-c', str(source), '-o', str(obj)], check=True)")
    tool=once(tool,"    for index, source in enumerate([SDK/'runtime.c',*sorted(sources)]):", "    if a.source_map:\n        flags += ['-gline-tables-only', '-fdebug-compilation-dir=/mod', '-ffile-prefix-map='+str(project)+'=/mod', '-ffile-prefix-map='+str(SDK)+'=/sdk']\n    for index, source in enumerate([SDK/'runtime.c',*sorted(sources)]):")
    tool=once(tool,"    fixed = {'mod.json': output / 'mod.json'", "    if a.source_map:\n        import sys\n        if 'source-map' in meta.get('resources', {}) or 'source-map.json' in resources:\n            raise ValueError('The project already declares a source map; omit --source-map to retain it')\n        source_map = output/'source-map.json'\n        subprocess.run([sys.executable, str(SDK/'tools.py'), 'source-map', str(elf), '--output', str(source_map)], check=True)\n        meta.setdefault('resources', {})['source-map']='source-map.json'\n        resources['source-map.json']=source_map\n        meta_path=output/'dkr-staged.json'\n        meta_path.write_text(json.dumps(meta, indent=2)+'\\n', encoding='utf-8')\n    fixed = {'mod.json': output / 'mod.json'")
    tool=once(tool,"    package = output / (manifest['id'] + '.nrm')", "    import sys\n    subprocess.run([sys.executable, '-B', str(SDK/'relocations.py'), '--elf', str(elf),\n                    '--symbols', str(output/'mod_syms.bin'), '--binary', str(output/'mod_binary.bin'),\n                    '--report', str(output/'relocation-report.json')], check=True)\n    package = output / (manifest['id'] + '.nrm')")
    tool=once(tool,"subprocess.run([sys.executable, str(SDK/'tools.py'), 'source-map'", "subprocess.run([sys.executable, '-B', str(SDK/'tools.py'), 'source-map'")
    (output/'sdk/build.py').write_text(tool,newline='\n')
    files={p.relative_to(output).as_posix():digest(p) for p in sorted(output.rglob('*')) if p.is_file()}
    # Tooling, examples and documentation do not define the owned simulation.
    # Their package digests are admitted separately when actually enabled.
    # In particular, correcting a guide must not split Windows/Linux lobbies.
    engine={name:value for name,value in files.items() if name.startswith(('native/src/','native/include/','native/tests/replay_probe/','modding/'))}
    engine['composition']=digest(AUTH/'NativeOverlay.cmake')
    identity=hashlib.sha256(json.dumps(engine,sort_keys=True,separators=(',',':')).encode()).hexdigest()
    (output/'sdk2-engine-identity.txt').write_text(identity+'\n')
    files['sdk2-engine-identity.txt']=digest(output/'sdk2-engine-identity.txt')
    receipt={'schema':1,'camera':camera.relative_to(ROOT).as_posix(),'ui':ui.relative_to(ROOT).as_posix(),
             'shadow':shadow.relative_to(ROOT).as_posix(),'authored':authors(),'files':files}
    (output/'sdk2-receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
    inspect(output)


def inspect(output,frozen=False):
    receipt=json.loads((output/'sdk2-receipt.json').read_text())
    if receipt['schema']!=1 or (not frozen and receipt['authored']!=authors()):raise ValueError('SDK authorship changed; prepare a new payload')
    files={p.relative_to(output).as_posix():digest(p) for p in sorted(output.rglob('*')) if p.is_file() and p.name!='sdk2-receipt.json'}
    if files!=receipt['files']:raise ValueError('SDK payload was modified after composition')
    if any(p.is_symlink() for p in output.rglob('*')):raise ValueError('SDK payload cannot contain symlinks')
    if not frozen:
        for relative,expected,_ in inputs(*(ROOT/receipt[n] for n in ('camera','ui','shadow'))):
            if (output/relative).read_text()!=expected:raise ValueError('SDK source differs from reviewed transformation: '+str(relative))
    print('SDK 2 source composition inspected; API 1 path and protected inputs retained. No runtime execution.')


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('mode',choices=('stage','verify'))
    for name in ('camera','ui','shadow'):p.add_argument('--'+name,type=Path)
    p.add_argument('--frozen',action='store_true',help='Verify the complete prepared image without comparing newer authoring inputs; final delivery must use strict verify')
    p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    if a.mode=='stage':stage(a.camera,a.ui,a.shadow,a.output)
    else:inspect(a.output.resolve(strict=True),a.frozen)
