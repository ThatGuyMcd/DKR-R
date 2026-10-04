// Separate opt-in owned backend. Its local bootstrap comes from the user's
// ROM at runtime. No ROM, RAM image or single-player save is distributed.
#include "owned_game.hpp"
#include "netplay/experimental_process.hpp"
#include "rom_revision.hpp"
#include "imgui/imgui.h"
#include "imgui/imgui_impl_sdl2_custom.h"
#include "imgui/backends/imgui_impl_sdlrenderer2.h"
#include <filesystem>
#include <algorithm>
#include "netplay/experimental_launch.hpp"
#include "netplay/experimental_pump.hpp"
#include "netplay/experimental_present_gate.hpp"
#include "netplay/runtime_state.hpp"
#include "probe_bridge.h"
#include "f3ddkr_rt64.hpp"
#include "renderer_snapshot.hpp"
#include "revision_addresses.hpp"
#include "runtime_enhancements.hpp"
#include "vi_presentation_policy.hpp"
#include "hle/rt64_application.h"
#include "hle/rt64_state.h"
#include "hle/rt64_workload_queue.h"
#include "hle/rt64_present_queue.h"
#include "render/rt64_framebuffer_renderer.h"
#include "render/rt64_buffer_uploader.h"
#include "monocypher.h"
#include <SDL.h>
#include <SDL_syswm.h>
#include <atomic>
#include <fstream>
#include <iostream>
#include <mutex>
#include <syncstream>
#include <thread>
#ifndef DKR_OWNED_FULL_SCENES
#define DKR_OWNED_FULL_SCENES 0
#endif
#if defined(_WIN32)
#include <Windows.h>
#include "netplay/experimental_arguments.hpp"
#endif

namespace {
using namespace dkr::runtime;
using namespace dkr::runtime::netplay;
using namespace dkr::runtime::netplay::experimental;
void require(bool ok,const char* error) {if(!ok)throw std::runtime_error(error);}
std::vector<std::uint8_t> read(const std::filesystem::path& path,std::size_t maximum) {
    std::ifstream file(path,std::ios::binary|std::ios::ate);
    if(!file || file.tellg()<=0 || file.tellg()>std::streamoff(maximum))throw std::runtime_error("Invalid local integration input file.");
    std::vector<std::uint8_t> bytes(std::size_t(file.tellg()));file.seekg(0);
    if(!file.read(reinterpret_cast<char*>(bytes.data()),bytes.size()))throw std::runtime_error("Cannot read local integration input.");return bytes;
}
secure::Key digest(std::span<const std::uint8_t> bytes) {
    secure::Key key;crypto_blake2b(key.data(),key.size(),bytes.data(),bytes.size());return key;
}
secure::Key digest(std::string_view text) {return digest({reinterpret_cast<const std::uint8_t*>(text.data()),text.size()});}
struct Options {
    bool host=false;std::string code;std::filesystem::path rom,data;
    // No automatic audible qualification. An explicit opt-in is required after
    // the independent PCM-format tests pass; private previews start muted.
    unsigned duration=0;bool script=false,mute=true,adventure=false;
};
Options options(int argc,char** argv) {
    Options result;bool mode=false;
    for(int i=1;i<argc;++i) {
        const std::string_view arg=argv[i];
        const auto value=[&] {if(++i>=argc)throw std::runtime_error("Missing integration option value.");return argv[i];};
        if(arg=="--host") {require(!mode,"Specify exactly one host/join mode.");mode=true;result.host=true;result.code=value();}
        else if(arg=="--join") {require(!mode,"Specify exactly one host/join mode.");mode=true;result.code=value();}
        else if(arg=="--rom")result.rom=std::filesystem::u8path(value());
        else if(arg=="--data")result.data=std::filesystem::u8path(value());
        else if(arg=="--seconds") {const std::string text=value();std::size_t end=0;result.duration=std::stoul(text,&end);require(end==text.size()&&result.duration<=120,"Integration duration must be 0-120 seconds.");}
        else if(arg=="--scripted-check")result.script=true;
        else if(arg=="--mute-output")result.mute=true;
        else if(arg=="--enable-audio")result.mute=false;
        else if(arg=="--adventure") {require(DKR_OWNED_FULL_SCENES,"Adventure requires the full-scene owned payload.");result.adventure=true;}
        else throw std::runtime_error("Unknown integration option.");
    }
    require(mode&&valid_quick_join_code(result.code)&&!result.rom.empty()&&!result.data.empty(),
        "Rollback race test: --host CODE | --join CODE --rom <US_v1.0_ROM> --data <separate_directory>");
    require(result.data.is_absolute(),"Experimental data directory must be absolute.");
    return result;
}
std::uint32_t pack(PackedInput p) {return p.buttons|(std::uint32_t(std::uint8_t(p.stick_x))<<16)|(std::uint32_t(std::uint8_t(p.stick_y))<<24);}
PackedInput unpack(std::uint32_t p) {return {std::uint16_t(p),std::int8_t(p>>16),std::int8_t(p>>24)};}
struct Shared {
    PresentationMailbox mailbox;
    std::atomic<bool> stop=false,renderer_ready=false,finished=false,audio_enabled=false;
    std::atomic<std::uint32_t> input=0,frames=0,confirmed=0,corrections=0;
    std::atomic<std::uint64_t> rendered=0;
    std::mutex status_mutex;std::string status="Preparing a separate local race",failure;
    void report(std::string text) {std::scoped_lock lock(status_mutex);status=std::move(text);}
    void fail(std::string text) {std::scoped_lock lock(status_mutex);if(failure.empty())failure=std::move(text);stop=true;}
};
struct StopWorkers {
    Shared& shared;std::thread& renderer;std::thread& simulation;
    ~StopWorkers() {
        shared.stop=true;
        if(simulation.joinable())simulation.join();
        if(renderer.joinable())renderer.join();
    }
};
struct Hardware {
    std::array<std::uint8_t,64> header{};std::array<std::uint8_t,4096> dmem{},imem{};
    std::array<std::uint32_t,9> dp{};std::array<std::uint32_t,14> vi{};
    RT64::Application::Core bind(SDL_Window* window,std::uint8_t* parked) {
        RT64::Application::Core core{};
#if defined(_WIN32)
        SDL_SysWMinfo info{};SDL_VERSION(&info.version);require(SDL_GetWindowWMInfo(window,&info)==SDL_TRUE,"Cannot obtain owned test window.");core.window=info.info.win.window;
#else
        core.window=window;
#endif
        core.HEADER=header.data();core.RDRAM=parked;core.DMEM=dmem.data();core.IMEM=imem.data();
        core.MI_INTR_REG=&dp[0];core.DPC_START_REG=&dp[1];core.DPC_END_REG=&dp[2];core.DPC_CURRENT_REG=&dp[3];core.DPC_STATUS_REG=&dp[4];
        core.DPC_CLOCK_REG=&dp[5];core.DPC_BUFBUSY_REG=&dp[6];core.DPC_PIPEBUSY_REG=&dp[7];core.DPC_TMEM_REG=&dp[8];
        core.VI_STATUS_REG=&vi[0];core.VI_ORIGIN_REG=&vi[1];core.VI_WIDTH_REG=&vi[2];core.VI_INTR_REG=&vi[3];core.VI_V_CURRENT_LINE_REG=&vi[4];
        core.VI_TIMING_REG=&vi[5];core.VI_V_SYNC_REG=&vi[6];core.VI_H_SYNC_REG=&vi[7];core.VI_LEAP_REG=&vi[8];core.VI_H_START_REG=&vi[9];
        core.VI_V_START_REG=&vi[10];core.VI_V_BURST_REG=&vi[11];core.VI_X_SCALE_REG=&vi[12];core.VI_Y_SCALE_REG=&vi[13];
        core.checkInterrupts=+[]{};return core; // Private register ACK, no guest scheduler event.
    }
    void present(const RenderDescriptor& d) {
        vi={presentation::kRetailNtscViStatus,presentation::retail_ntsc_vi_origin(d.framebuffer),320,2,0,0x03E52239,0x20D,0xC15,0x0C150C15,d.black?0U:0x006C02ECU,0x2501FF,0xE0204,0x200,0x400};
        vi[10]=presentation::canonicalise_dkr_v_region(vi[10],vi[13]);
    }
};
void drain(RT64::Application& app) {
    app.drawDataUploader->wait();app.transformsUploader->wait();app.tilesUploader->wait();app.state->framebufferRenderer->waitForUploaders();
    app.workloadQueue->waitForWorkloadId(app.state->workloadId);app.presentQueue->waitForPresentId(app.state->presentId);
    app.workloadQueue->waitForIdle();app.presentQueue->waitForIdle();
}
struct ApplicationEnd {RT64::Application& app;~ApplicationEnd(){app.end();}};
struct DecodeDrain {RT64::Application& app;~DecodeDrain(){drain(app);}};
void renderer(Shared& shared,SDL_Window* window,const Options& o) try {
    Hardware hardware;std::vector<std::uint8_t> parked(PresentationMailbox::kImageBytes);
    RT64::ApplicationConfiguration cfg{};cfg.appId="dkr-experimental-race-test";cfg.detectDataPath=false;cfg.useConfigurationFile=false;cfg.dataPath=o.data;
    // Registration survives explicit application worker shutdown.
    std::unique_ptr<present_gate::Registration> registration;
    RT64::Application app(hardware.bind(window,parked.data()),cfg);ApplicationEnd end{app};
    app.userConfig.graphicsAPI=RT64::UserConfiguration::GraphicsAPI::Vulkan;
    app.userConfig.antialiasing=RT64::UserConfiguration::Antialiasing::None;
    app.userConfig.aspectRatio=app.userConfig.extAspectRatio=RT64::UserConfiguration::AspectRatio::Original;
    app.userConfig.resolution=RT64::UserConfiguration::Resolution::Manual;app.userConfig.resolutionMultiplier=2;app.userConfig.downsampleMultiplier=1;
    app.userConfig.refreshRate=RT64::UserConfiguration::RefreshRate::Original;app.userConfig.refreshRateTarget=30;
    app.userConfig.internalColorFormat=RT64::UserConfiguration::InternalColorFormat::Standard;app.userConfig.idleWorkActive=false;
    app.enhancementConfig.presentation.mode=RT64::EnhancementConfiguration::Presentation::Mode::Console;
    app.enhancementConfig.presentation.removeBlackBorders=true;app.enhancementConfig.rect.fixRectLR=true;
    unsigned thread=0;
#if defined(_WIN32)
    thread=GetCurrentThreadId();
#endif
    require(app.setup(thread)==RT64::Application::SetupResult::Success,"Owned interactive RT64 setup failed.");
    registration=std::make_unique<present_gate::Registration>(app.state.get());shared.renderer_ready=true;
    F3DDKRRT64Bridge bridge;
    while(!shared.stop.load()) {
        auto lease=shared.mailbox.take();
        if(!lease){std::this_thread::sleep_for(std::chrono::milliseconds(1));continue;}
        auto workspace=DecodeWorkspace::create(lease);require(bool(workspace),"Owned frame decoder already claimed.");
        {
            RendererSnapshotScope snapshot(app.core.RDRAM,app.state->RDRAM,workspace->bytes().data());
            DecodeDrain consumers{app};present_gate::Scope submission(*registration,lease);
            if(!shared.mailbox.is_current(lease))continue;
            const auto& d=lease->descriptor();OSTask task{};task.t.type=M_GFXTASK;task.t.data_ptr=d.display_start;task.t.data_size=d.display_end-d.display_start;
            bridge.process(app,task);hardware.present(d);app.state->lastScreenVI=RT64::VI{};app.updateScreen();++shared.rendered;
        }
        // Only this renderer worker waits for GPU work. ALL asynchronous readers
        // drain before mutable RAM/lease release; CPU/UI/network never wait here.
    }
    const auto stats=registration->statistics();
    require(stats.tagged!=0||shared.frames==0,"Actual present queue was not generation-tagged.");
    std::osyncstream(std::cout)<<"owned renderer: decoded="<<shared.rendered<<" tagged="<<stats.tagged<<" accepted="<<stats.accepted<<" retired="<<stats.rejected<<'\n';
}catch(const std::exception& e){shared.fail(e.what());}
void simulation(Shared& shared,SDL_AudioDeviceID audio,const Options& o) try {
    std::string error;
    shared.report("Checking the installed ROM; no single-player saves or mods are used");
    const auto identity=rom::inspect(o.rom);
    require(identity.supported()&&identity.revision==rom::Revision::UsV77,
        "This first experimental race test requires US v1.0. Normal online play still supports both revisions.");
    std::filesystem::path canonical;
    if(!rom::materialize_canonical(o.rom,identity,o.data.parent_path()/"rom-cache",canonical,error))
        throw std::runtime_error(error);
    const auto rom_bytes=read(canonical,32U*1024U*1024U);
    char* base=SDL_GetBasePath();require(base!=nullptr,"Cannot locate the installed DKR-R executable.");
    auto executable_directory=std::filesystem::u8path(base);SDL_free(base);
#if defined(_WIN32)
    const auto boot_exe=executable_directory/"DKR-R.exe";
#else
    const auto boot_exe=executable_directory/"DKR-R";
#endif
    const auto boot_dir=o.data/"bootstrap", image=boot_dir/"local.dkr-bootstrap";
    const auto utf8=[](const auto& path){const auto u=path.u8string();return std::string(reinterpret_cast<const char*>(u.data()),u.size());};
    const std::vector<std::string> args{"--rom",utf8(canonical),"--config",utf8(boot_dir),
        "--rollback-bootstrap-output",utf8(image),"--timeout","30"};
    shared.report("Preparing Ancient Lake locally; native workers stop before rollback takes ownership");
    struct RemoveImage {std::filesystem::path path;~RemoveImage(){std::error_code e;std::filesystem::remove(path,e);}} remove_image{image};
    if(!run_child(boot_exe,args,[&]{return !shared.stop.load();},std::chrono::seconds(45),error)) {
        if(shared.stop)return;
        throw std::runtime_error(error);
    }
    const auto bootstrap=read(image,Launch::kMaximumBaseline);
    require(shared.mailbox.begin_epoch(DKR_OWNED_FULL_SCENES ? 90:91),"Owned initial render epoch refused.");
    auto world=make_owned_game(bootstrap,rom_bytes,91,shared.mailbox,[&](ConfirmedAudio block,std::string& message) {
        if(block.rate!=22050||block.pcm.size()>8192){message="Unsupported private confirmed audio format.";return false;}
        // Confirmed PCM only. Bound presentation latency without altering game
        // state. Queued audio belongs exclusively to this experimental device.
        if(shared.audio_enabled.load()) {
            if(SDL_GetQueuedAudioSize(audio)>22050U*4U*3U/4U)SDL_ClearQueuedAudio(audio);
            // Audition quietly without altering the confirmed PCM journal or
            // rollback state. SDL copies this bounded presentation-only block.
            std::vector<std::uint8_t> quiet(block.pcm.size());
            for(std::size_t b=0;b<quiet.size();b+=2) {
                const auto bits=std::uint16_t(block.pcm[b])|(std::uint16_t(block.pcm[b+1])<<8);
                const int sample=bits>=0x8000U?int(bits)-65536:int(bits);
                const auto reduced=std::uint16_t(sample/4);
                quiet[b]=std::uint8_t(reduced);quiet[b+1]=std::uint8_t(reduced>>8);
            }
            if(SDL_QueueAudio(audio,quiet.data(),unsigned(quiet.size()))!=0){message=SDL_GetError();return false;}
        }
        return true;
    },error,DKR_OWNED_FULL_SCENES!=0,o.adventure ? (1U<<24):0);if(!world)throw std::runtime_error(error);
    require(world->scene_view().owners==2,"The local bootstrap does not contain the agreed two-owner roster.");
    LaunchContract c;c.revision=DKR_OWNED_TEST_REVISION;c.schema=world->contract().schema;c.state_bytes=world->contract().state_bytes;c.epoch=91;c.prediction_window=6;c.input_delay=1;
    c.rom=digest(rom_bytes);c.build=digest(DKR_OWNED_BUILD_ID);
    c.rules=digest(DKR_OWNED_FULL_SCENES ?
        (o.adventure ? "retail-menus-confirmed-gameplay-rollback-no-mods-2-owner-adventure-fresh-accurate-4:3-delay1-window6":
        "retail-menus-confirmed-gameplay-rollback-no-mods-2-owner-tracks-fresh-accurate-4:3-delay1-window6"):
        "retail-no-mods-local-bootstrap-2-owner-accurate-4:3-delay1-window6");
    c.abi=digest("little-endian-x86-64-native-v1/"+std::to_string(sizeof(dkr_probe_native_state))+"/"+
        std::to_string(sizeof(SerializableContext))+"/"+std::to_string(c.schema)+"/"+std::to_string(c.state_bytes));
    auto transport=make_quick_join_session_transport(o.host,o.code,error);if(!transport||!transport->open(0,error))throw std::runtime_error(error);
    Launch launch(*transport);
    if(o.host) {std::vector<std::uint8_t> initial(c.state_bytes);if(!world->capture(initial,error)||!launch.host(c,std::move(initial),error))throw std::runtime_error(error);}
    else if(!launch.join(c,[&](auto state,std::string& message){
        // A cold client GPU setup must not acknowledge Ready and then leave
        // the host racing alone while its own owner pump is still parked.
        if(!shared.renderer_ready.load())return PreparationStep::Pending;
        return world->admit_initial(state,message)?PreparationStep::Ready:PreparationStep::Failed;
    },error))throw std::runtime_error(error);
    auto began=Network::Clock::now();
    while(!shared.stop && !launch.is_open()) {
        if(!launch.service_launch())throw std::runtime_error(launch.error());
        if(o.host&&launch.view().phase==LaunchPhase::Ready&&shared.renderer_ready) {
            // Independent local channel readiness can lag briefly.
            launch.release(error);
        }
        const auto phase=launch.view().phase;std::string progress;
        if(phase==LaunchPhase::AwaitingPeer)progress="Waiting for the other racer; send them the code above";
        else if(phase==LaunchPhase::Authenticating)progress="Authenticating and matching build / ROM / simulation contract";
        else if(phase==LaunchPhase::ReceivingBaseline)progress="Synchronising the race: "+std::to_string(launch.view().baseline_received/1024)+" / "+std::to_string(c.state_bytes/1024)+" KiB";
        else if(phase==LaunchPhase::PreparingWorld)progress="Verifying the initial race state";
        else if(phase==LaunchPhase::Ready||phase==LaunchPhase::Releasing)progress="Both racers prepared; agreeing the same start";
        else progress="Connecting through Quick Join";
        shared.report(std::move(progress));
        if(o.duration&&Network::Clock::now()-began>std::chrono::seconds(60))throw std::runtime_error("Private interactive admission timed out.");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if(shared.stop)return;
    while(!shared.renderer_ready&&!shared.stop){launch.service();std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    if(shared.stop)return;
    Session session(*world);if(!session.start({{91,2,6},std::uint8_t(o.host?0:1),1},error))throw std::runtime_error(error);
    Network network(session,launch);
    if(!network.start(*launch.network_configuration(),error))throw std::runtime_error(error);Pump pump(session,network);
    SDL_PauseAudioDevice(audio,0);began=Network::Clock::now();std::uint64_t published_epoch=0;std::uint32_t published_frame=UINT32_MAX,published_replays=UINT32_MAX;
    OwnedSceneView observed=world->scene_view();
    std::uint32_t menu_since=0;
    std::uint64_t observed_epoch=session.epoch();
    while(!shared.stop) {
        const auto now=Network::Clock::now();
        const auto scene=world->scene_view();
        if(scene.menu!=observed.menu || (scene.menu && scene.menu_id!=observed.menu_id) || observed_epoch!=session.epoch()) {
            observed=scene;observed_epoch=session.epoch();menu_since=session.frontier();
            std::cout<<"owned scene: epoch="<<observed_epoch<<" menu="<<scene.menu<<" id="<<scene.menu_id
                     <<" owners="<<unsigned(scene.owners)<<" level="<<scene.level<<" type="<<scene.race_type
                     <<" viewports="<<unsigned(scene.viewports)<<'\n';
        }
        if(!pump.pulse(now,[&] {
            if(o.script) {
                if(DKR_OWNED_FULL_SCENES && scene.menu) {
                    const auto age=session.frontier()-menu_since;
                    if(scene.menu_id==0 && age>=150 && age%30==0)return PackedInput{0x1000,0,0};
                    if((scene.menu_id==3 || scene.menu_id==15) && age>=50 && age%30==20)return PackedInput{0x8000,0,0};
                    if(o.adventure && (scene.menu_id==19 || scene.menu_id==6) && age>=30 && age%30==20 && o.host)
                        return PackedInput{0x8000,0,0};
                    return PackedInput{};
                }
                return PackedInput{0x8000,std::int8_t((session.frontier()/15)%2?30:-30),0};
            }
            return unpack(shared.input.load());
        }))throw std::runtime_error(pump.error());
        const auto& s=session.statistics();shared.frames=s.next_frame;shared.confirmed=s.confirmed_frames;shared.corrections=s.rollbacks;
        if(session.presentation_ready()&&(session.epoch()!=published_epoch||s.next_frame-1!=published_frame||s.replayed_frames!=published_replays)) {
            if(!world->publish_current(session.epoch(),s.next_frame-1,error))throw std::runtime_error(error);
            published_epoch=session.epoch();published_frame=s.next_frame-1;published_replays=s.replayed_frames;
        }
        shared.report(pump.view().waiting_for_clients()?"Waiting for the other racer (controls and connection remain serviced)":
            scene.menu ? "Retail menus: both racers agree each menu input before resource changes":
            "Playing with genuine rollback; late inputs rewind and replay the owned simulation");
        if(!DKR_OWNED_FULL_SCENES &&
           (pump.view().last_step==SessionStep::WaitingForPeers||pump.view().last_step==SessionStep::PreparingScene)) {
            shared.report("Race test finished at a confirmed scene boundary. Close this test and host a new one to race again.");
            shared.finished=true;break;
        }
        if(o.duration&&now-began>=std::chrono::seconds(o.duration)){shared.stop=true;break;}
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    std::osyncstream(std::cout)<<"owned interactive: frames="<<session.statistics().next_frame<<" confirmed="<<session.statistics().confirmed_frames
             <<" corrections="<<session.statistics().rollbacks<<'\n';
    launch.close();transport->close();
}catch(const std::exception& e){shared.fail(e.what());}
PackedInput input(SDL_GameController* pad) {
    PackedInput p{};const auto* keys=SDL_GetKeyboardState(nullptr);
    if(keys[SDL_SCANCODE_SPACE])p.buttons|=0x8000;
    if(keys[SDL_SCANCODE_LCTRL])p.buttons|=0x4000;
    if(keys[SDL_SCANCODE_LSHIFT])p.buttons|=0x0010;
    if(keys[SDL_SCANCODE_Q])p.buttons|=0x2000;
    if(keys[SDL_SCANCODE_RETURN])p.buttons|=0x1000;
    p.stick_x=std::int8_t((keys[SDL_SCANCODE_RIGHT]-keys[SDL_SCANCODE_LEFT])*80);
    p.stick_y=std::int8_t((keys[SDL_SCANCODE_UP]-keys[SDL_SCANCODE_DOWN])*80);
    if(pad&&SDL_GameControllerGetAttached(pad)) {
        if(SDL_GameControllerGetButton(pad,SDL_CONTROLLER_BUTTON_A))p.buttons|=0x8000;
        if(SDL_GameControllerGetButton(pad,SDL_CONTROLLER_BUTTON_B))p.buttons|=0x4000;
        if(SDL_GameControllerGetButton(pad,SDL_CONTROLLER_BUTTON_RIGHTSHOULDER))p.buttons|=0x0010;
        if(SDL_GameControllerGetButton(pad,SDL_CONTROLLER_BUTTON_LEFTSHOULDER))p.buttons|=0x2000;
        if(SDL_GameControllerGetButton(pad,SDL_CONTROLLER_BUTTON_START))p.buttons|=0x1000;
        const auto axis=[&](SDL_GameControllerAxis a){const int v=SDL_GameControllerGetAxis(pad,a);return std::abs(v)<6000?0:v*80/32768;};
        const int x=axis(SDL_CONTROLLER_AXIS_LEFTX),y=-axis(SDL_CONTROLLER_AXIS_LEFTY);if(x)p.stick_x=std::int8_t(x);if(y)p.stick_y=std::int8_t(y);
    }
    return p;
}
}
class LockedLogBuffer final:public std::streambuf {
    std::streambuf* target_;std::mutex mutex_;
    std::streamsize xsputn(const char* bytes,std::streamsize count) override {
        std::scoped_lock lock(mutex_);return target_->sputn(bytes,count);
    }
    int_type overflow(int_type value) override {
        if(traits_type::eq_int_type(value,traits_type::eof()))return traits_type::not_eof(value);
        std::scoped_lock lock(mutex_);return target_->sputc(traits_type::to_char_type(value));
    }
    int sync() override {std::scoped_lock lock(mutex_);return target_->pubsync();}
public:
    explicit LockedLogBuffer(std::streambuf* target):target_(target){}
};
struct RunLog {
    std::ofstream file;LockedLogBuffer buffer;std::streambuf *out,*err;
    explicit RunLog(const std::filesystem::path& path):file(path,std::ios::out|std::ios::trunc),buffer(file.rdbuf()),out(nullptr),err(nullptr) {
        require(bool(file),"Cannot create separate race diagnostic log");
        // All writes, including native diagnostic imports, share a locked sink.
        // osyncstream additionally keeps each worker's final summary atomic.
        out=std::cout.rdbuf(&buffer);err=std::cerr.rdbuf(&buffer);
        std::cout.setf(std::ios::unitbuf);std::cerr.setf(std::ios::unitbuf);
    }
    ~RunLog(){std::cout.rdbuf(out);std::cerr.rdbuf(err);}
};
int main(int argc,char** argv) {
    std::unique_ptr<RunLog> log;bool automatic=false;
    try {
#if defined(_WIN32)
    WindowsUtf8Arguments unicode;
    require(unicode.valid,"Invalid Unicode application arguments");
    argc=int(unicode.values.size());argv=unicode.values.data();
#endif
    auto o=options(argc,argv);
    automatic=o.duration!=0;
    const auto random=secure::generate_key();std::string suffix;
    static constexpr char hex[]="0123456789abcdef";
    for(unsigned i=0;i<12;++i){suffix+=hex[random[i]>>4];suffix+=hex[random[i]&15];}
    o.data/="run-"+suffix;
    require(std::filesystem::create_directories(o.data),"Cannot create exclusive experimental session directory");
    log=std::make_unique<RunLog>(o.data/"rollback-test.log");
    std::cout<<"Experimental race test "<<DKR_RELEASE_VERSION<<"; "<<(o.host?"host":"client")<<"; fresh isolated local session\n";
#if !defined(_WIN32)
    std::filesystem::permissions(o.data,std::filesystem::perms::owner_all,std::filesystem::perm_options::replace);
#endif
    require(revision_addresses::select(DKR_OWNED_TEST_REVISION==77?rom::Revision::UsV77:rom::Revision::UsV80),"Integration revision refused.");
    enhancements::set_presentation_profile(enhancements::PresentationProfile::Accurate);
    require(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_EVENTS|SDL_INIT_AUDIO|SDL_INIT_GAMECONTROLLER)==0,SDL_GetError());
    if(char* base=SDL_GetBasePath()) {
        const auto mappings=(std::filesystem::u8path(base)/"assets"/"controllers"/"gamecontrollerdb.txt").u8string();
        SDL_GameControllerAddMappingsFromFile(reinterpret_cast<const char*>(mappings.c_str()));
        SDL_free(base);
    }
    std::uint32_t flags=SDL_WINDOW_SHOWN|SDL_WINDOW_RESIZABLE;
#if !defined(_WIN32)
    flags|=SDL_WINDOW_VULKAN;
#endif
    auto* window=SDL_CreateWindow(DKR_OWNED_FULL_SCENES ? "DKR-R Experimental rollback - retail menus":
        "DKR-R Experimental rollback - Ancient Lake",SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,800,600,flags);
    require(window!=nullptr,SDL_GetError());
    SDL_AudioSpec wanted{},obtained{};wanted.freq=22050;wanted.format=AUDIO_S16LSB;wanted.channels=2;wanted.samples=1024;
    const auto audio=SDL_OpenAudioDevice(nullptr,0,&wanted,&obtained,0);require(audio!=0,SDL_GetError());
    SDL_GameController* pad=nullptr;Shared shared;shared.audio_enabled=!o.mute;
    auto* status_window=SDL_CreateWindow("DKR-R Experimental rollback - connection and controls",
        SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,640,450,SDL_WINDOW_SHOWN|SDL_WINDOW_RESIZABLE);
    require(status_window!=nullptr,SDL_GetError());
    auto* ui_renderer=SDL_CreateRenderer(status_window,-1,SDL_RENDERER_SOFTWARE);
    require(ui_renderer!=nullptr,SDL_GetError());
    ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.FontGlobalScale=1.15F;
    io.ConfigFlags|=ImGuiConfigFlags_NavEnableKeyboard;
    require(ImGui_ImplSDL2_InitForSDLRenderer(status_window,ui_renderer),"Cannot initialise test status input");
    require(ImGui_ImplSDLRenderer2_Init(ui_renderer),"Cannot initialise test status drawing");
    // Owner/renderer lifetime is bounded by joining BEFORE SDL/window teardown.
    std::thread render,owner;
    StopWorkers shutdown{shared,render,owner};
    render=std::thread(renderer,std::ref(shared),window,std::cref(o));
    owner=std::thread(simulation,std::ref(shared),audio,std::cref(o));
    while(!shared.stop) {
        SDL_Event event;while(SDL_PollEvent(&event)) {
            if(event.type==SDL_QUIT||(event.type==SDL_WINDOWEVENT&&event.window.event==SDL_WINDOWEVENT_CLOSE)||
               (event.type==SDL_KEYDOWN&&event.key.keysym.scancode==SDL_SCANCODE_ESCAPE))shared.stop=true;
            if(event.type==SDL_KEYDOWN&&event.key.keysym.scancode==SDL_SCANCODE_TAB)SDL_RaiseWindow(status_window);
            // Game steering/accelerate keys must not activate buttons in the
            // separate status panel (including End test) while it is unfocused.
            const auto status_id=SDL_GetWindowID(status_window);
            bool status_event=false;
            switch(event.type) {
            case SDL_WINDOWEVENT:status_event=event.window.windowID==status_id;break;
            case SDL_KEYDOWN:case SDL_KEYUP:status_event=event.key.windowID==status_id;break;
            case SDL_TEXTINPUT:status_event=event.text.windowID==status_id;break;
            case SDL_TEXTEDITING:status_event=event.edit.windowID==status_id;break;
            case SDL_MOUSEMOTION:status_event=event.motion.windowID==status_id;break;
            case SDL_MOUSEBUTTONDOWN:case SDL_MOUSEBUTTONUP:status_event=event.button.windowID==status_id;break;
            case SDL_MOUSEWHEEL:status_event=event.wheel.windowID==status_id;break;
            default:break;
            }
            if(status_event)ImGui_ImplSDL2_ProcessEvent(&event);
        }
        if(pad&&!SDL_GameControllerGetAttached(pad)){SDL_GameControllerClose(pad);pad=nullptr;}
        if(!pad)for(int i=0;i<SDL_NumJoysticks();++i)if(SDL_IsGameController(i)){pad=SDL_GameControllerOpen(i);if(pad)break;}
        if(shared.finished)shared.input=0;
        else if(SDL_GetKeyboardFocus()==window)shared.input=pack(input(pad));
        else if(pad) {
            PackedInput p{};
            if(SDL_GameControllerGetButton(pad,SDL_CONTROLLER_BUTTON_A))p.buttons|=0x8000;
            if(SDL_GameControllerGetButton(pad,SDL_CONTROLLER_BUTTON_B))p.buttons|=0x4000;
            if(SDL_GameControllerGetButton(pad,SDL_CONTROLLER_BUTTON_LEFTSHOULDER))p.buttons|=0x2000;
            if(SDL_GameControllerGetButton(pad,SDL_CONTROLLER_BUTTON_RIGHTSHOULDER))p.buttons|=0x10;
            if(SDL_GameControllerGetButton(pad,SDL_CONTROLLER_BUTTON_START))p.buttons|=0x1000;
            const int x=SDL_GameControllerGetAxis(pad,SDL_CONTROLLER_AXIS_LEFTX),y=-int(SDL_GameControllerGetAxis(pad,SDL_CONTROLLER_AXIS_LEFTY));
            p.stick_x=std::int8_t(std::abs(x)<6000?0:x*80/32768);p.stick_y=std::int8_t(std::abs(y)<6000?0:y*80/32768);
            shared.input=pack(p);
        } else shared.input=0;
        std::string title;
        {std::scoped_lock lock(shared.status_mutex);title=(o.host?"HOST | ":"CLIENT | ")+shared.status;}
        title+=" | frame "+std::to_string(shared.frames)+" confirmed "+std::to_string(shared.confirmed)+" corrections "+std::to_string(shared.corrections);
        SDL_SetWindowTitle(window,title.c_str());
        ImGui_ImplSDLRenderer2_NewFrame();ImGui_ImplSDL2_NewFrame();ImGui::NewFrame();
        ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin("Connection",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoSavedSettings);
        ImGui::TextColored({1.0F,0.72F,0.2F,1.0F},DKR_OWNED_FULL_SCENES ?
            "EXPERIMENTAL ROLLBACK - MENU INTEGRATION CANDIDATE": "EXPERIMENTAL ROLLBACK - FIRST RACE TEST");
        ImGui::TextWrapped(DKR_OWNED_FULL_SCENES ? (o.adventure ?
            "Two players / fresh Adventure / US v 1.0 / no mods / separate session. Menus confirmed; gameplay rollback. Whole-game release qualification is NOT complete.":
            "Two players / retail Track Select / US v 1.0 / no mods / separate session. Menus confirmed; gameplay rollback. Whole-game release qualification is NOT complete."):
            "Two players / Ancient Lake / US v 1.0 / no mods / separate session. Normal online play is unchanged.");
        ImGui::Separator();ImGui::Text("%s Quick Join code: %s",o.host?"HOST":"JOINING",o.code.c_str());
        if(ImGui::Button("Copy code"))SDL_SetClipboardText(o.code.c_str());
        ImGui::SameLine();if(ImGui::Button("Show game"))SDL_RaiseWindow(window);
        ImGui::Separator();
        if(!shared.finished)ImGui::Text("Working %c", "|/-\\"[(SDL_GetTicks()/125)%4]);
        {std::scoped_lock lock(shared.status_mutex);ImGui::TextWrapped("%s",shared.status.c_str());}
        ImGui::Text("Frame: %u  Confirmed: %u  Corrections: %u",shared.frames.load(),shared.confirmed.load(),shared.corrections.load());
        ImGui::Separator();
        ImGui::TextWrapped("Controller: A accelerate, B brake, left stick steer, LB use item, RB drift. Keyboard: Space accelerate, Left Ctrl brake, arrows steer, Q use item, Left Shift drift. Tab shows this panel. Escape closes the test.");
        bool audible=shared.audio_enabled.load();
        if(ImGui::Checkbox("Enable quiet game audio (starts muted)",&audible)) {
            shared.audio_enabled=audible;if(!audible)SDL_ClearQueuedAudio(audio);
        }
        if(ImGui::Button("End test and return to the launcher"))shared.stop=true;
        ImGui::End();ImGui::Render();
        SDL_SetRenderDrawColor(ui_renderer,9,25,35,255);SDL_RenderClear(ui_renderer);
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData());SDL_RenderPresent(ui_renderer);
        SDL_Delay(16);
    }
    owner.join();render.join();
    ImGui_ImplSDLRenderer2_Shutdown();ImGui_ImplSDL2_Shutdown();ImGui::DestroyContext();
    SDL_DestroyRenderer(ui_renderer);SDL_DestroyWindow(status_window);
    if(pad)SDL_GameControllerClose(pad);SDL_CloseAudioDevice(audio);SDL_DestroyWindow(window);SDL_Quit();
    if(!shared.failure.empty())throw std::runtime_error(shared.failure);
    if(o.duration)require(shared.frames>0&&shared.rendered>0,"The automatic race check stopped before rendered gameplay.");
    return 0;
}catch(const std::exception& e){
    std::cerr<<"Experimental race test stopped safely: "<<e.what()<<'\n';
    if(!automatic)SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,"DKR-R Experimental rollback",e.what(),nullptr);
    return 3;
}
}
