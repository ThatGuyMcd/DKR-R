#include "experimental_runtime.hpp"
#include "experimental_lobby_admission.hpp"
#include "experimental_checkpoint_hash.hpp"
#include "owned_game.hpp"
#include "runtime_platform.hpp"
#include "runtime_ui.hpp"
#include "rt64_renderer.hpp"
#include "runtime_enhancements.hpp"
#include "save_manager.hpp"
#include "ultramodern/config.hpp"
#include "monocypher.h"
#include <SDL.h>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <fstream>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>
#include <tuple>

namespace dkr::runtime::netplay::experimental {
namespace {
std::mutex view_mutex;
RuntimeView view;
std::atomic<bool> requested_stop{false};
void report(RuntimeView v) {std::scoped_lock lock(view_mutex);view=std::move(v);}
secure::Key digest(std::span<const std::uint8_t> bytes) {secure::Key r{};crypto_blake2b(r.data(),r.size(),bytes.data(),bytes.size());return r;}
void put(std::vector<std::uint8_t>& bytes,std::uint64_t v,unsigned n) {for(unsigned i=0;i<n;++i)bytes.push_back(std::uint8_t(v>>(8*i)));}
std::vector<std::uint8_t> read_rom(const std::filesystem::path& path) {
    std::ifstream file(path,std::ios::binary|std::ios::ate);
    if(!file||file.tellg()!=rom::kRetailRomSize)throw std::runtime_error("Cannot open the canonical ROM for owned play.");
    std::vector<std::uint8_t> b(rom::kRetailRomSize);file.seekg(0);
    if(!file.read(reinterpret_cast<char*>(b.data()),b.size()))throw std::runtime_error("Cannot read the canonical ROM for owned play.");return b;
}
class LobbyRoute final:public SessionTransport {
    DirectSession& lobby_;
    std::array<std::uint8_t,4> slots_{};
    unsigned count_=0;
public:
    LobbyRoute(DirectSession& lobby,const LaunchDescriptor& d):lobby_(lobby) {
        slots_.fill(255);for(std::uint8_t slot=0;slot<4;++slot)if(d.occupied(slot))slots_[count_++]=slot;
    }
    std::uint8_t owner(std::uint8_t slot) const {for(unsigned p=0;p<count_;++p)if(slots_[p]==slot)return std::uint8_t(p);return 255;}
    bool configuration(const secure::Key& incarnation,NetworkConfiguration& c,std::string& error) {
        NetworkConfiguration stable;
        if(!lobby_.owned_network_configuration(incarnation,stable,error))return false;
        c=stable;c.peers={};
        for(unsigned p=0;p<count_;++p)if(stable.peers[slots_[p]].address) {
            c.peers[p]=stable.peers[slots_[p]];c.peers[p].address=LobbyAdmission::address(std::uint8_t(p));
        }
        return true;
    }
    bool open(std::uint16_t,std::string&) override {return false;}
    void close() override {}
    bool is_open() const override {return lobby_.owned_game_active();}
    std::uint16_t local_port() const override {return 0;}
    std::size_t maximum_plaintext_datagram_bytes() const override {return 12352;}
    DatagramSendStatus send_status(const PeerAddress& a,std::span<const std::uint8_t> b,TransportTrafficClass t,std::string& e) override {
        if(a.size!=4||a.storage[0]!='D'||a.storage[1]!='K'||a.storage[2]!='X'||a.storage[3]>=count_) {
            e="Invalid owned lobby destination.";return DatagramSendStatus::Error;
        }
        return lobby_.send_owned_packet(slots_[a.storage[3]],b,t,e);
    }
    bool receive(PeerAddress& a,std::vector<std::uint8_t>& b,std::string& e) override {
        DirectSession::OwnedPacket p;e.clear();
        if(!lobby_.owned_game_active()){e=lobby_.runtime_view().status;return false;}
        if(!lobby_.take_owned_packet(p))return false;
        const auto index=owner(p.source);if(index==255){e="Owned packet is outside the immutable lobby roster.";return false;}
        a=LobbyAdmission::address(index);b=std::move(p.bytes);return true;
    }
};
struct Shared {
    PresentationMailbox mailbox;
    std::atomic<bool> stop{false},renderer_ready{false},simulation_done{false},renderer_done{false};
    std::atomic<std::uint32_t> input{0};
    std::atomic<int> confirmed_motor{-1};
    std::mutex mutex;std::string failure;
    std::vector<std::uint8_t> confirmed_save;
    void fail(std::string error) {
        if(error.empty())error="The experimental runtime failed without stage diagnostics.";
        std::scoped_lock lock(mutex);
        if(failure.empty()) {
            std::fprintf(stderr,"[rollback][failure] %s\n",error.c_str());
            failure=std::move(error);
        }
        stop=true;
    }
};
// Joining must happen even if a later worker fails to start or event pumping throws.
struct WorkerCompletion {
    std::atomic<bool>& done;
    ~WorkerCompletion() {done.store(true,std::memory_order_release);}
};
struct RuntimeWorkers {
    Shared& shared;
    std::thread render,simulation;
    void retire() {
        if(!simulation.joinable()&&!render.joinable())return;
        shared.stop=true;
        const auto started=std::chrono::steady_clock::now();auto next_report=started;
        std::fprintf(stderr,"[rollback][retire] stopping owned workers; servicing native window messages\n");
        // GPU/swap-chain destruction can synchronously send native window
        // messages to DkrMain. A blind join stops that window's message pump
        // and can deadlock teardown. Do not invoke game/overlay callbacks here:
        // their owners are being retired. Completion is published only AFTER
        // each worker's owned objects (including the GPU) have been destroyed.
        while((simulation.joinable()&&!shared.simulation_done.load(std::memory_order_acquire))||
              (render.joinable()&&!shared.renderer_done.load(std::memory_order_acquire))) {
            SDL_PumpEvents();
            const auto now=std::chrono::steady_clock::now();
            if(now>=next_report) {
                std::fprintf(stderr,"[rollback][retire] elapsed-ms=%lld simulation-done=%d renderer-done=%d\n",
                    static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(now-started).count()),
                    int(shared.simulation_done.load()),int(shared.renderer_done.load()));
                next_report=now+std::chrono::seconds(2);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        if(simulation.joinable())simulation.join();
        if(render.joinable())render.join();
        std::fprintf(stderr,"[rollback][retire] both owned workers fully destroyed\n");
    }
    ~RuntimeWorkers() {
        retire();
        // While the immutable lobby routing is still installed, stop only
        // this participant's physical controller. Never replay a motor edge.
        for(int port=0;port<4;++port)platform::set_rumble(port,false);
        platform::set_online_input_routing(false);
        report({});
    }
};
std::uint32_t pack(PackedInput p) {return p.buttons|(std::uint32_t(std::uint8_t(p.stick_x))<<16)|(std::uint32_t(std::uint8_t(p.stick_y))<<24);}
PackedInput unpack(std::uint32_t p) {return {std::uint16_t(p),std::int8_t(p>>16),std::int8_t(p>>24)};}
}
bool runtime_available(rom::Revision revision) {return revision==rom::Revision::UsV77;}
RuntimeView runtime_view() {std::scoped_lock lock(view_mutex);return view;}
void request_runtime_stop() {requested_stop=true;}
bool run_runtime(ultramodern::renderer::WindowHandle window,const std::filesystem::path& canonical_rom,
    std::vector<std::uint8_t> bootstrap,DirectSession& lobby,const LaunchDescriptor& accepted_launch,
    unsigned timeout_seconds,std::string& error,bool scripted_check) {
    const std::optional<LaunchDescriptor> launch{accepted_launch};const auto initial=lobby.runtime_view();
    // A departure may retire the accepted descriptor during cold boot, or
    // between this caller and the worker startup. Validate against the caller's
    // immutable launch, then acknowledge retirement without constructing a GPU.
    if(initial.owned_match_ending||!initial.active) {
        if(!initial.active) lobby.disconnect(initial.status);
        else {
            std::vector<std::uint8_t> seed;std::filesystem::path path;
            if(!saves::read_online_adventure(initial.host,launch->match_id,seed,path,error)||
               !lobby.complete_owned_match_end(launch_descriptor_hash(*launch),seed,error))return false;
        }
        if(ui::lifecycle_request()!=ui::LifecycleRequest::Exit&&ui::lifecycle_request()!=ui::LifecycleRequest::Restart)
            ui::report_online_game_end(initial.status.empty()?"The online match ended.":initial.status,lobby.active());
        return true;
    }
    if(initial.launch_descriptor!=launch||launch->synchronization!=SynchronizationMode::ExperimentalRollback||bootstrap.empty()||
       lobby.view().method!=ConnectionMethod::QuickJoin) {error="The normal experimental Quick Join launch is incomplete.";return false;}
    // All peers use the same compact N64 owner roster even if a departed lobby
    // slot leaves a hole. The authenticated network routes retain their slots.
    LobbyRoute route(lobby,*launch);const auto local=route.owner(initial.local_slot);
    if(local>=launch->player_count){error="The local racer is absent from the accepted roster.";return false;}
    std::vector<std::uint8_t> seed;std::filesystem::path save_path;
    if(!saves::read_online_adventure(initial.host,launch->match_id,seed,save_path,error)||
       stable_hash(std::string_view(reinterpret_cast<const char*>(seed.data()),seed.size()))!=initial.online_save_hash) {
        if(error.empty())error="The owned online save changed after lobby verification.";return false;
    }
    Shared shared;requested_stop=false;
    shared.confirmed_save=seed; // valid fallback when departure interrupts admission
    RuntimeWorkers workers{shared};
    report({true,PumpWait::ScenePreparation,0,0,0,0,0,"Preparing experimental rollback in the existing game window"});
    platform::set_online_input_routing(true,launch->occupied_mask,initial.local_slot);
    workers.render=std::thread([&] {
        WorkerCompletion completed{shared.renderer_done};
        try {
            std::vector<std::uint8_t> parked(PresentationMailbox::kImageBytes);
            RT64Renderer renderer(parked.data(),window,false,true);
            if(!renderer.valid())throw std::runtime_error("The normal renderer could not start for owned play.");
            auto config=ultramodern::renderer::get_graphics_config();shared.renderer_ready=true;
            while(!shared.stop) {
                renderer.reap_owned();
                auto next=ultramodern::renderer::get_graphics_config();
                if(config!=next){renderer.update_config(config,next);config=next;}
                bool presented=false;
                if(renderer.owned_capacity_available()) {
                    if(auto lease=shared.mailbox.take()) {
                        renderer.present_owned(lease,shared.mailbox);presented=true;
                    }
                }
                if(!presented){renderer.repeat_owned();std::this_thread::sleep_for(std::chrono::milliseconds(1));}
            }
        } catch(const std::exception& e){shared.fail(e.what());}catch(...){shared.fail("The owned presentation worker failed.");}
    });
    workers.simulation=std::thread([&] {
        WorkerCompletion completed{shared.simulation_done};
        std::unique_ptr<OwnedGame> world;
        bool simulation_started=false;
        try {
            std::string message;auto rom_bytes=read_rom(canonical_rom);
            constexpr std::uint64_t epoch=91;
            if(!shared.mailbox.begin_epoch(epoch))throw std::runtime_error("Owned initial presentation epoch refused.");
            world=make_owned_game(bootstrap,rom_bytes,epoch,shared.mailbox,
                [pcm=std::vector<std::int16_t>{}](ConfirmedAudio audio,std::string& e) mutable {
                    if(audio.rate<8000||audio.rate>48000||audio.pcm.size()%4||audio.pcm.size()>8192) {e="Invalid confirmed owned audio block.";return false;}
                    // Confirmed output is sequential on the simulation owner.
                    // Retain the conversion buffer, not one heap allocation per
                    // block. queue_audio copies into its own device storage.
                    pcm.resize(audio.pcm.size()/2);
                    // platform::queue_audio accepts guest-native R,L pairs.
                    // The owned sink carries conventional little-endian L,R.
                    for(std::size_t i=0;i<pcm.size();++i){const auto v=std::uint16_t(audio.pcm[2*i])|(std::uint16_t(audio.pcm[2*i+1])<<8);pcm[i^1]=std::int16_t(v);}
                    platform::set_audio_frequency(audio.rate);platform::queue_audio(pcm.data(),pcm.size());return true;
                },message,false,std::uint32_t(lobby.view().room.manifest.magic_codes_hash),seed,
                OwnedBootOptions{true,launch->player_count,launch->host_control,
                    enhancements::multiplayer_race_music_enabled()});
            if(!world)throw std::runtime_error(message);
            if(initial.host) {
                std::vector<std::uint8_t> pak_seed;
                if(!saves::read_experimental_online_paks(true,launch->match_id,pak_seed,message)||
                   !world->install_initial_paks(epoch,pak_seed,message))throw std::runtime_error(message);
            }
            world->set_confirmed_rumble_sink([&](unsigned port,bool enabled) {
                if(port==local)shared.confirmed_motor.store(enabled?1:0,std::memory_order_release);
            });
            if(world->scene_view().owners!=launch->player_count)throw std::runtime_error("Owned construction roster differs from the accepted lobby.");
            const auto contract=world->contract();std::vector<std::uint8_t> identity;
            const std::string_view build=DKR_OWNED_BUILD_ID;
            identity.insert(identity.end(),build.begin(),build.end());
            const auto rom_hash=digest(rom_bytes);identity.insert(identity.end(),rom_hash.begin(),rom_hash.end());
            put(identity,contract.schema,8);put(identity,contract.state_bytes,8);put(identity,launch_descriptor_hash(*launch),8);
            put(identity,initial.online_save_hash,8);put(identity,epoch,8);
            std::vector<std::uint8_t> baseline;
            std::fprintf(stderr,"[rollback][admission] owner=%u host=%d build=%s checkpoint=%zu capture-begin\n",
                unsigned(local),initial.host,DKR_OWNED_BUILD_ID,contract.state_bytes);
            if(initial.host) {
                baseline.resize(contract.state_bytes);
                if(!world->capture(baseline,message))throw std::runtime_error(
                    message.empty()?"The initial owned checkpoint could not be captured.":message);
            }
            LobbyAdmission admission(route,initial.host,launch->player_count,local,digest(identity));
            if(!admission.begin(std::move(baseline),[&](auto b,std::string& e){return world->admit_initial(b,e);},message))throw std::runtime_error(message);
            std::fprintf(stderr,"[rollback][admission] owner=%u checkpoint-captured decoded=%u wire=%u\n",
                unsigned(local),admission.decoded_bytes(),admission.total_bytes());
            bool loaded=false;
            auto admission_report_at=Network::Clock::now();
            auto prepared_at=Network::Clock::time_point{};
            while(!shared.stop&&!lobby.running()) {
                if(!lobby.owned_game_active()){shared.stop=true;break;}
                if(!admission.service_admission())throw std::runtime_error(admission.error());
                const auto now=Network::Clock::now();
                const bool prepared=admission.prepared();
                if(admission.all_prepared()&&!prepared_at.time_since_epoch().count())prepared_at=now;
                if(!loaded&&admission.prepared()&&shared.renderer_ready) {
                    const auto active_save=world->confirmed_save();
                    const auto active_hash=active_save.size()==512?stable_hash(std::string_view(
                        reinterpret_cast<const char*>(active_save.data()),active_save.size())):0;
                    if(active_hash!=initial.online_save_hash)throw std::runtime_error("The owned EEPROM seed does not match the verified launch save.");
                    std::fprintf(stderr,"[rollback][save] runtime-proof launch=%llu generation=%u expected=%llu actual=%llu bytes=%zu\n",
                        static_cast<unsigned long long>(launch_descriptor_hash(*launch)),initial.online_save_generation,
                        static_cast<unsigned long long>(initial.online_save_hash),static_cast<unsigned long long>(active_hash),active_save.size());
                    lobby.mark_game_loaded(admission.baseline_identity(),initial.online_save_generation,initial.online_save_hash,launch_descriptor_hash(*launch));loaded=true;
                    std::fprintf(stderr,"[rollback][admission] owner=%u checkpoint-verified; submitted normal Loaded proof\n",unsigned(local));
                }
                const auto status=prepared&&!shared.renderer_ready
                    ? std::string("INITIAL GAME STATE VERIFIED; WAITING FOR RENDERER") : admission.status();
                if(now>=admission_report_at) {
                    const auto lobby_view=lobby.runtime_view();
                    std::fprintf(stderr,"[rollback][admission] owner=%u received=%u wire=%u decoded=%u prepared=%d renderer=%d loaded=%d lobby-state=%d status=%s\n",
                        unsigned(local),admission.received_bytes(),admission.total_bytes(),admission.decoded_bytes(),prepared,
                        int(shared.renderer_ready.load()),loaded,int(lobby_view.state),status.c_str());
                    admission_report_at=now+std::chrono::seconds(2);
                    std::fprintf(stderr,"[rollback][transfer]%s\n",admission.diagnostics().c_str());
                }
                // The transfer has its own no-progress deadline. After it is
                // verified, waiting for Loaded/Start must also be bounded;
                // never release a peer by bypassing those integrity gates.
                if(prepared_at.time_since_epoch().count()&&now-prepared_at>=std::chrono::seconds(120))
                    throw std::runtime_error("Initial game state was verified, but the synchronized start did not complete within 120 seconds. "+lobby.runtime_view().status);
                report({true,PumpWait::ScenePeers,epoch,0,0,0,0,status,true});
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            if(!shared.stop) {
                admission.complete_start();
                std::fprintf(stderr,"[rollback][admission] owner=%u normal Start released; starting owned simulation\n",unsigned(local));
                Session session(*world);
                const auto budget=std::max<std::size_t>(128U*1024U*1024U,contract.state_bytes*(std::size_t(launch->rollback_window)+2));
                if(!session.start({{epoch,launch->player_count,launch->rollback_window,budget},local,launch->input_delay_frames},message))throw std::runtime_error(message);
                simulation_started=true;
                NetworkConfiguration config;if(!route.configuration(admission.incarnation(),config,message))throw std::runtime_error(message);
                Network network(session,admission);if(!network.start(config,message))throw std::runtime_error(message);Pump pump(session,network);
                std::uint64_t published_epoch=0;std::uint32_t published_frame=UINT32_MAX,published_replays=UINT32_MAX;
                auto last_save_at=seed;
                const auto initial_paks=world->confirmed_paks();
                std::vector<std::uint8_t> last_paks_at(initial_paks.begin(),initial_paks.end());
                auto next_save=Network::Clock::now()+std::chrono::seconds(5);
                auto observed=world->scene_view();auto observed_epoch=session.epoch();auto menu_since=session.frontier();
                auto progress_at=Network::Clock::now();auto progress_epoch=session.epoch();std::uint32_t confirmed_progress=0;
                auto report_at=Network::Clock::now();
                auto transition_started=Network::Clock::time_point{};
                auto transition_epoch=session.epoch();
                std::optional<std::tuple<PumpWait,std::uint64_t,std::uint32_t,
                    std::uint32_t,std::uint32_t,std::uint32_t>> reported_view;
                while(!shared.stop) {
                    if(!lobby.owned_game_active()){shared.stop=true;break;}
                    const auto now=Network::Clock::now();
                    const auto scene=world->scene_view();
                    if(scene.menu!=observed.menu||(scene.menu&&scene.menu_id!=observed.menu_id)||observed_epoch!=session.epoch()) {
                        observed=scene;observed_epoch=session.epoch();menu_since=session.frontier();
                        std::fprintf(stderr,"[rollback][scene] epoch=%llu menu=%d id=%d owners=%u level=%d type=%d viewports=%u\n",
                            static_cast<unsigned long long>(observed_epoch),scene.menu,scene.menu_id,unsigned(scene.owners),scene.level,scene.race_type,unsigned(scene.viewports));
                    }
                    if(!pump.pulse(now,[&]{
                        if(!scripted_check)return unpack(shared.input.load());
                        if(scene.menu) {
                            const auto age=session.frontier()-menu_since;
                            if(scene.menu_id==0&&age>=150&&age%30==0)return PackedInput{0x1000,0,0};
                            if((scene.menu_id==3||scene.menu_id==15)&&age>=50&&age%30==20)return PackedInput{0x8000,0,0};
                            return PackedInput{};
                        }
                        return PackedInput{0x8000,std::int8_t((session.frontier()/15)%2?30:-30),0};
                    })) {
                        const auto lifecycle=lobby.runtime_view();
                        if(!lifecycle.active||lifecycle.owned_match_ending){shared.stop=true;break;}
                        throw std::runtime_error(pump.error());
                    }
                    // Once-per-scene diagnostics only. These wall-clock values
                    // never enter checkpoints, input assignment or peer hashes.
                    if(session.epoch()!=transition_epoch) {
                        if(transition_started.time_since_epoch().count())
                            std::fprintf(stderr,"[rollback][scene-release] owner=%u old-epoch=%llu new-epoch=%llu total-us=%lld\n",
                                unsigned(local),static_cast<unsigned long long>(transition_epoch),
                                static_cast<unsigned long long>(session.epoch()),
                                static_cast<long long>(std::chrono::duration_cast<std::chrono::microseconds>(
                                    Network::Clock::now()-transition_started).count()));
                        transition_started={};transition_epoch=session.epoch();
                    } else if(!transition_started.time_since_epoch().count() &&
                              (pump.view().wait==PumpWait::ScenePeers || pump.view().wait==PumpWait::ScenePreparation)) {
                        transition_started=now;
                    }
                    const auto& stats=session.statistics();
                    if(now>=report_at) {
                        const auto& packets=network.statistics();
                        std::fprintf(stderr,"[rollback][progress] epoch=%llu frame=%u confirmed=%u corrections=%u replayed=%u received=%llu rejected=%llu wait=%d\n",
                            static_cast<unsigned long long>(session.epoch()),stats.next_frame,stats.confirmed_frames,stats.rollbacks,stats.replayed_frames,
                            static_cast<unsigned long long>(packets.received),static_cast<unsigned long long>(packets.rejected),int(pump.view().wait));
                        const auto current_scene=world->scene_view();
                        std::fprintf(stderr,"[rollback][scene-budget] level=%d type=%d menu=%u owners=%u viewports=%u finished=%u tt-camera=%u ticks-this-pulse=%u\n",
                            current_scene.level,current_scene.race_type,unsigned(current_scene.menu),unsigned(current_scene.owners),
                            unsigned(current_scene.viewports),unsigned(current_scene.finished_racers),unsigned(current_scene.tt_camera),pump.view().ticks_this_pulse);
                        report_at=now+std::chrono::seconds(5);
                    }
                    if(progress_epoch!=session.epoch()||confirmed_progress!=stats.confirmed_frames) {
                        progress_epoch=session.epoch();confirmed_progress=stats.confirmed_frames;progress_at=now;
                    } else if(now-progress_at>=std::chrono::seconds(90)) {
                        throw std::runtime_error("Experimental peers made no confirmed progress for 90 seconds. The session ended safely; separate saves are retained.");
                    }
                    if(session.presentation_ready()&&(published_epoch!=session.epoch()||published_frame!=stats.next_frame||published_replays!=stats.replayed_frames)) {
                        if(!world->publish_current(session.epoch(),stats.next_frame-1,message))throw std::runtime_error(message);
                        published_epoch=session.epoch();published_frame=stats.next_frame;published_replays=stats.replayed_frames;
                    }
                    // The network pump runs about every millisecond, but this
                    // UI state changes at authored ticks/wait transitions.
                    // Avoid repeatedly allocating its status string and taking
                    // the UI mutex while all six displayed fields are identical.
                    const auto next_view=std::tuple{pump.view().wait,session.epoch(),
                        stats.next_frame,stats.confirmed_frames,stats.rollbacks,stats.replayed_frames};
                    if(!reported_view||*reported_view!=next_view) {
                        report({true,pump.view().wait,session.epoch(),stats.next_frame,stats.confirmed_frames,stats.rollbacks,stats.replayed_frames,"Experimental rollback"});
                        reported_view=next_view;
                    }
                    if(now>=next_save) {
                        const auto confirmed=world->confirmed_save();
                        if(!std::equal(confirmed.begin(),confirmed.end(),last_save_at.begin(),last_save_at.end())) {
                            if(!saves::commit_online_adventure(initial.host,launch->match_id,confirmed,message))throw std::runtime_error(message);
                            last_save_at.assign(confirmed.begin(),confirmed.end());
                        }
                        const auto paks=world->confirmed_paks();
                        if(!std::equal(paks.begin(),paks.end(),last_paks_at.begin(),last_paks_at.end())) {
                            if(!saves::commit_experimental_online_paks(initial.host,launch->match_id,paks,message))throw std::runtime_error(message);
                            last_paks_at.assign(paks.begin(),paks.end());
                        }
                        next_save=now+std::chrono::seconds(5);
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
                const auto& stats=session.statistics();
                std::fprintf(stderr,"[rollback][owned] epoch=%llu frame=%u confirmed=%u corrections=%u replayed=%u\n",
                    static_cast<unsigned long long>(session.epoch()),stats.next_frame,stats.confirmed_frames,stats.rollbacks,stats.replayed_frames);
            }
        }catch(const std::exception& e){
            // A match-end notification may race network.service() by one
            // packet. This is expected retirement, not a determinism failure.
            const auto lifecycle=lobby.runtime_view();
            if(lifecycle.active&&!lifecycle.owned_match_ending&&!simulation_started) {
                std::fprintf(stderr,"[rollback][startup-cancel] %s\n",e.what());
                lobby.request_owned_match_end(std::string("Startup stopped safely: ")+e.what());shared.stop=true;
            } else if(lifecycle.active&&!lifecycle.owned_match_ending)shared.fail(e.what());
            else shared.stop=true;
        }catch(...){shared.fail("The owned simulation worker failed.");}
        // Also run on a departure that races a network/publish operation. Only
        // confirmed EEPROM is retained; never persist speculative simulation.
        if(world) try {
            std::string message;
            const auto confirmed=world->confirmed_save();
            if(!saves::commit_online_adventure(initial.host,launch->match_id,confirmed,message))shared.fail(message);
            else shared.confirmed_save.assign(confirmed.begin(),confirmed.end());
            if(!saves::commit_experimental_online_paks(initial.host,launch->match_id,world->confirmed_paks(),message))shared.fail(message);
        } catch(const std::exception& e){shared.fail(e.what());}
          catch(...){shared.fail("The confirmed online save could not be retained.");}
    });
    const auto started=Network::Clock::now();
    unsigned overlay_check=0;
    std::fprintf(stderr,"[rollback][runtime] event loop started timeout=%u scripted=%d\n",timeout_seconds,scripted_check);
    while(!shared.stop&&!shared.simulation_done) {
        const auto motor=shared.confirmed_motor.exchange(-1,std::memory_order_acq_rel);
        if(motor>=0)platform::set_rumble(initial.local_slot,motor!=0);
        // Exercise the ordinary SDL shortcut and overlay in the isolated
        // integration check. Never synthesize input in a user's live match.
        if(scripted_check && overlay_check<2 &&
           Network::Clock::now()-started>=std::chrono::seconds(40+15*overlay_check)) {
            SDL_Event shortcut{};shortcut.type=SDL_KEYDOWN;
            shortcut.key.windowID=SDL_GetWindowID(static_cast<SDL_Window*>(platform::sdl_window()));
            shortcut.key.state=SDL_PRESSED;shortcut.key.keysym.scancode=SDL_SCANCODE_F1;
            shortcut.key.keysym.sym=SDLK_F1;
            if(SDL_PushEvent(&shortcut)!=1)shared.fail("The isolated overlay shortcut could not be queued.");
            std::fprintf(stderr,"[rollback][check] queued ordinary overlay shortcut step=%u\n",++overlay_check);
        }
        platform::pump_window_events(nullptr);platform::poll_input();
        std::uint16_t buttons=0;float x=0,y=0;bool blocked=false;
        platform::get_local_online_input(&buttons,&x,&y,&blocked);
        shared.input=pack(blocked?PackedInput{}:PackedInput{buttons,std::int8_t(std::clamp(x,-1.0F,1.0F)*80),std::int8_t(std::clamp(y,-1.0F,1.0F)*80)});
        const auto lifecycle=ui::lifecycle_request();
        if(!lobby.owned_game_active())shared.stop=true;
        const auto elapsed=Network::Clock::now()-started;
        const bool timed_out=timeout_seconds&&elapsed>=std::chrono::seconds(timeout_seconds);
        if(requested_stop||lifecycle!=ui::LifecycleRequest::None||timed_out) {
            std::fprintf(stderr,"[rollback][runtime] stop requested window=%d lifecycle=%d timeout=%d elapsed-ms=%lld\n",
                int(requested_stop.load()),int(lifecycle),timed_out,
                static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count()));
            if(requested_stop||lifecycle==ui::LifecycleRequest::Exit||lifecycle==ui::LifecycleRequest::Restart)
                lobby.disconnect(initial.host?"The host closed the game. The lobby has closed.":"A racer closed the game.");
            else if(lobby.owned_game_active())
                lobby.request_owned_match_end(initial.host?"The host stopped the game. The match has ended.":"A racer stopped the game. The match has ended.");
            shared.stop=true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    workers.retire();platform::set_online_input_routing(false);
    {std::scoped_lock lock(shared.mutex);error=shared.failure;}
    report({});
    const auto ended=lobby.runtime_view();
    if(error.empty()&&ended.active&&ended.owned_match_ending) {
        if(!lobby.complete_owned_match_end(launch_descriptor_hash(*launch),shared.confirmed_save,error))
            lobby.fail_runtime_start(error);
    } else if(error.empty()&&!ended.active) {
        const auto reason=ended.status.empty()?"The host left. The lobby has closed.":ended.status;
        lobby.disconnect(reason);
    }
    if(error.empty()&&ui::lifecycle_request()!=ui::LifecycleRequest::Exit&&ui::lifecycle_request()!=ui::LifecycleRequest::Restart)
        ui::report_online_game_end(ended.status.empty()?"The online match ended.":ended.status,lobby.active());
    return error.empty();
}
}
