"""Connect the party owner to the existing native window and local bootstrap."""
from pathlib import Path
import hashlib
AUTH=Path(__file__).resolve().parent
def contract():
 identity=hashlib.sha256()
 scripts=[AUTH/name for name in ('pipeline.py','runtime_pipeline.py','owned_pipeline.py','link_pipeline.py','ui_pipeline.py')]
 for p in sorted(list((AUTH/'native').iterdir())+scripts):
  if p.is_file():identity.update(p.name.encode());identity.update(p.read_bytes().replace(b'\r\n',b'\n'))
 return identity.hexdigest()
def once(t,a,b):
 if t.count(a)!=1:raise ValueError('Party runtime anchor changed: '+a[:140])
 return t.replace(a,b,1)
def runtime(text):
 text='#include "online_stability/live_delay.hpp"\n#include "online_stability/host_settings_qualification.hpp"\n#include "party_runtime.hpp"\n#include "party_camera.hpp"\n#include "vehicle_binding.hpp"\n'+text
 text=once(text,'                Session session(*world);',
  '                Session session(*world);\n'
  '                dkr::online_stability::LiveDelay live_delay(dkr::online_stability::control(&lobby),launch->input_delay_frames,launch->rollback_window);\n'
  '                live_delay.local_owner(local);\n                dkr::online_stability::DelayScope delay_scope(live_delay);\n'
  '                dkr::online_stability::HostSettingsQualification host_settings_check(scripted_check);')
 text=once(text,'                    if(!pump.pulse(now,[&]{',
  '''                    host_settings_check.pulse(&lobby,initial.host,session.epoch(),now);
                    const bool dynamic=lobby.view().room.rules.automatic_input_delay;
                    const bool delay_paused=pump.view().wait==PumpWait::ScenePeers||pump.view().wait==PumpWait::ScenePreparation;
                    live_delay.pulse(session.epoch(),1,session.frontier(),delay_paused,pump.view().wait==PumpWait::OwnerInput||pump.view().wait==PumpWait::Confirmation,now,session.missing_input_mask());
                    if(!scripted_check) {
                        const auto physical=unpack(shared.input.load());
                        if(!live_delay.capture({physical.buttons,physical.stick_x,physical.stick_y,physical.accelerator,physical.brake,physical.driving_flags},now))throw std::runtime_error("Dynamic input transition queue exceeded its safe bound.");
                    }
                    for(unsigned fill=live_delay.take_fill();fill&&session.needs_local_input(true);--fill) {
                        const auto held=live_delay.held();
                        if(session.sample_local({held.buttons,held.x,held.y,held.accelerator,held.brake,held.driving_flags},true)!=OwnerInputResult::Accepted)throw std::runtime_error("Dynamic input lead could not retain immutable owner input.");
                    }
                    if(!pump.pulse(now,[&]{''')
 text=once(text,'                        if(!scripted_check)return unpack(shared.input.load());',
  '                        if(!scripted_check){const auto value=live_delay.take();return PackedInput{value.buttons,value.x,value.y,value.accelerator,value.brake,value.driving_flags};}')
 signature='bool run_runtime(ultramodern::renderer::WindowHandle window,'
 text=once(text,signature,(AUTH/'native/runtime_runner.inl').read_text(encoding='utf-8')+'\n'+signature)
 anchor='std::string_view runtime_engine_identity(rom::Revision revision) {return owned_adapter_identity(revision);}'
 text=once(text,anchor,'''std::string_view runtime_engine_identity(rom::Revision revision) {
    auto identify=[](rom::Revision r){auto base=std::string(owned_adapter_identity(r));if(base.empty())return base;
        base+="'''+contract()+'''";const auto h=digest(std::span(reinterpret_cast<const std::uint8_t*>(base.data()),base.size()));
        std::string hex;constexpr char digits[]="0123456789abcdef";for(auto c:h){hex+=digits[c>>4];hex+=digits[c&15];}return hex;};
    static const auto v77=identify(rom::Revision::UsV77),v80=identify(rom::Revision::UsV80);
    return revision==rom::Revision::UsV77?v77:revision==rom::Revision::UsV80?v80:std::string_view{};
}''')
 anchor='    const std::optional<LaunchDescriptor> launch{accepted_launch};const auto initial=lobby.runtime_view();'
 return once(text,anchor,'''    if(dkr::runtime::spectators::party_enabled(&lobby))return run_party_runtime(window,canonical_rom,std::move(bootstrap),lobby,accepted_launch,timeout_seconds,error,scripted_check,std::move(mods),std::move(mod_bootstrap),std::move(code_bootstrap));
'''+anchor)

def timeline(text):
 text='#include "online_stability/live_delay.hpp"\n'+text
 text=once(text,'    const auto result=inputs_.publish(frame,input,command);',
  '    const auto result=inputs_.publish(frame,input,command);\n    if(result==OwnerInputResult::Accepted)dkr::online_stability::input_authored(frame);')
 return once(text,'std::uint64_t(frontier_)+configuration_.input_delay+(live_lead ? 2 : 1)',
  'dkr::online_stability::sample_limit(frontier_,configuration_.input_delay,live_lead)')
def camera(text):
 text='#include "party_camera.hpp"\n#include "camera_scene.hpp"\n#include "game_payload.hpp"\n'+text
 text=once(text,'    const auto next=activation();','    const auto next=activation();\n    const bool director=stamp.online&&dkr::runtime::spectators::spectator_director.load(std::memory_order_relaxed);')
 text=once(text,'if(!session_||retained.size()<0x800000||decoder.size()<DKR_CAMERA_KERNEL_BYTES)return;',
  'if((!session_&&!director)||retained.size()<0x800000||decoder.size()<DKR_CAMERA_KERNEL_BYTES)return;')
 anchor='''        const auto batch=session_->evaluate(retained,stamp,owners);
        if(!batch.diagnostic.empty())report(batch.diagnostic);
        for(auto& range:ranges_)if(batch.poses[range.view])range.pose=*batch.poses[range.view];'''
 text=once(text,anchor,(AUTH/'native/director_pose.inl').read_text(encoding='utf-8'))
 text=once(text,'    bool okay=active_->revision==rom::Revision::UsV77?',
  '    const auto revision=active_?active_->revision:(dkr::runtime::active_payload()?dkr::runtime::active_payload()->revision:rom::Revision::UsV77);\n    bool okay=revision==rom::Revision::UsV77?')
 return text
def capture(text):
 text='#include "party_link.hpp"\n'+text
 anchor='''bool dkr_experimental_bootstrap_activate_save(std::string& error) {
'''
 return once(text,anchor,anchor+'''    if(requested_memory&&dkr::runtime::spectators::party_bootstrap_active()){
        std::vector<char> seed;if(!dkr::runtime::spectators::party_bootstrap_load({},seed)||
            dkr::runtime::netplay::stable_hash(std::string_view(seed.data(),seed.size()))!=requested_save_hash) {
            error="The private party EEPROM does not match the authenticated launch.";return false;
        }
        error.clear();return true;
    }
''')

def registration(text):
 text='#include "party_link.hpp"\n'+text
 return once(text,'    if (!dkr::runtime::saves::runtime_save_context_ready()) {',
 '''    const bool private_party_boot=
#if defined(DKR_EXPERIMENTAL_RACE_TEST)
        dkr_experimental_bootstrap_active()&&dkr::runtime::spectators::party_bootstrap_active();
#else
        false;
#endif
    // The party owns a verified memory-only seed, not a personal save route.
    if (!dkr::runtime::saves::runtime_save_context_ready()&&!private_party_boot) {''')
def game_main(text):
 text='#include "party_link.hpp"\n'+text
 anchor='''            if (!dkr::runtime::saves::prepare_runtime_save_context(config_directory,'''
 start=text.index(anchor)
 end=text.index('        } catch(const std::exception& error) {',start)
 original=text[start:end]
 # Party boot owns a private in-memory seed; spectators do not activate a save
 # file, a persistent Pak route or personal custom records even during cold boot.
 next='''            if(dkr::runtime::spectators::party_enabled(&dkr::runtime::netplay::session())) {
                auto party=dkr::runtime::spectators::party_link(&dkr::runtime::netplay::session());
                std::vector<std::uint8_t> seed;{std::scoped_lock lock(party->mutex);seed=party->seed;}
                if(seed.size()!=512)throw dkr::mods::Error("Party bootstrap is missing its verified save seed.");
                dkr::runtime::spectators::party_bootstrap_seed(seed);
                ultramodern::set_save_storage_callbacks({dkr::runtime::spectators::party_bootstrap_path,
                    dkr::runtime::spectators::party_bootstrap_load,dkr::runtime::spectators::party_bootstrap_write});
                dkr::runtime::pak::begin_session_directory({});
                std::filesystem::path records;
                if(online_save.host) {std::vector<std::uint8_t> active;std::filesystem::path owner;
                    if(!dkr::runtime::saves::read_online_adventure(true,online_save.launch_descriptor->match_id,active,owner,rom_error)||active!=seed)throw dkr::mods::Error("The host online seed changed during party boot: "+rom_error);
                    records=owner.parent_path()/"custom-records-v1.bin";}
                if(!dkr::runtime::saves::records::begin(records,false,rom_error))throw dkr::mods::Error(rom_error);
            } else {
                dkr::runtime::spectators::party_bootstrap_seed({});
'''+original+'''            }
'''
 text=text[:start]+next+text[end:]
 text=once(text,'dkr::runtime::achievements::begin(online_save.active,0,code_set&&!code_set->ordered.empty(),code_set&&std::any_of(code_set->ordered.begin(),code_set->ordered.end(),[](const auto& p){return p->development;}));',
  'dkr::runtime::achievements::begin(online_save.active,0,code_set&&!code_set->ordered.empty(),dkr::runtime::spectators::party_enabled(&dkr::runtime::netplay::session())||(code_set&&std::any_of(code_set->ordered.begin(),code_set->ordered.end(),[](const auto& p){return p->development;})));')
 return text
