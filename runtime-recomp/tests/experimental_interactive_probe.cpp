// Integration-only interactive Quick Join frontend. Normal launcher admission
// stays fenced until LOCAL boot and complete handoff qualify. Never distribute
// a retail fixture or advertise this target as completed launcher integration.
#include "owned_game.hpp"
#include "impaired_transport.hpp"
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
#include <thread>
#if defined(_WIN32)
#include <Windows.h>
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
    bool host=false;std::string code;std::filesystem::path rom,bootstrap,data;
    // No automatic audible qualification. An explicit opt-in is required after
    // the independent PCM-format tests pass; private previews start muted.
    unsigned duration=0;bool script=false,impair=false,mute=true;
};
Options options(int argc,char** argv) {
    Options result;bool mode=false;
    for(int i=1;i<argc;++i) {
        const std::string_view arg=argv[i];
        const auto value=[&] {if(++i>=argc)throw std::runtime_error("Missing integration option value.");return argv[i];};
        if(arg=="--host") {require(!mode,"Specify exactly one host/join mode.");mode=true;result.host=true;result.code=value();}
        else if(arg=="--join") {require(!mode,"Specify exactly one host/join mode.");mode=true;result.code=value();}
        else if(arg=="--rom")result.rom=std::filesystem::u8path(value());
        else if(arg=="--bootstrap")result.bootstrap=std::filesystem::u8path(value());
        else if(arg=="--data")result.data=std::filesystem::u8path(value());
        else if(arg=="--seconds") {const std::string text=value();std::size_t end=0;result.duration=std::stoul(text,&end);require(end==text.size()&&result.duration<=120,"Integration duration must be 0-120 seconds.");}
        else if(arg=="--scripted-check")result.script=true;
        else if(arg=="--impaired-check")result.impair=true;
        else if(arg=="--mute-output")result.mute=true;
        else if(arg=="--enable-audio")result.mute=false;
        else throw std::runtime_error("Unknown integration option.");
    }
    require(mode&&valid_quick_join_code(result.code)&&!result.rom.empty()&&!result.bootstrap.empty()&&!result.data.empty(),
        "PRIVATE integration: --host CODE | --join CODE --rom user.z64 --bootstrap LOCAL.dkr-probe --data PRIVATE_DIR [--seconds 10 --scripted-check]");
    return result;
}
std::uint32_t pack(PackedInput p) {return p.buttons|(std::uint32_t(std::uint8_t(p.stick_x))<<16)|(std::uint32_t(std::uint8_t(p.stick_y))<<24);}
PackedInput unpack(std::uint32_t p) {return {std::uint16_t(p),std::int8_t(p>>16),std::int8_t(p>>24)};}
struct Shared {
    PresentationMailbox mailbox;
    std::atomic<bool> stop=false,renderer_ready=false;
    std::atomic<std::uint32_t> input=0,frames=0,confirmed=0,corrections=0;
    std::atomic<std::uint64_t> rendered=0;
    std::mutex status_mutex;std::string status="Preparing private owned world",failure;
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
        vi={0x311E,d.framebuffer&0x7FFFFFU,320,2,0,0x03E52239,0x20D,0xC15,0x0C150C15,d.black?0U:0x006C02ECU,0x2501FF,0xE0204,0x200,0x400};
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
    RT64::ApplicationConfiguration cfg{};cfg.appId="dkr-private-interactive-rollback";cfg.detectDataPath=false;cfg.useConfigurationFile=false;cfg.dataPath=o.data;
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
    std::cout<<"owned renderer: decoded="<<shared.rendered<<" tagged="<<stats.tagged<<" accepted="<<stats.accepted<<" retired="<<stats.rejected<<'\n';
}catch(const std::exception& e){shared.fail(e.what());}
void simulation(Shared& shared,SDL_AudioDeviceID audio,const Options& o) try {
    const auto rom=read(o.rom,32U*1024U*1024U),bootstrap=read(o.bootstrap,Launch::kMaximumBaseline);
    std::string error;require(shared.mailbox.begin_epoch(91),"Owned initial render epoch refused.");
    auto world=make_owned_game(bootstrap,rom,91,shared.mailbox,[&](ConfirmedAudio block,std::string& message) {
        if(block.rate!=22050||block.pcm.size()>8192){message="Unsupported private confirmed audio format.";return false;}
        // Confirmed PCM only. Bound presentation latency without altering game
        // state. Queued audio belongs exclusively to this experimental device.
        if(!o.mute) {
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
    },error);if(!world)throw std::runtime_error(error);
    LaunchContract c;c.revision=DKR_OWNED_TEST_REVISION;c.schema=world->contract().schema;c.state_bytes=world->contract().state_bytes;c.epoch=91;c.prediction_window=6;c.input_delay=1;
    c.rom=digest(rom);c.build=digest(DKR_OWNED_BUILD_ID);
    c.rules=digest("retail-no-mods-local-bootstrap-2-owner-accurate-4:3-delay1-window6");
    c.abi=digest("little-endian-x86-64-native-v1/"+std::to_string(sizeof(dkr_probe_native_state))+"/"+
        std::to_string(sizeof(SerializableContext))+"/"+std::to_string(c.schema)+"/"+std::to_string(c.state_bytes));
    auto transport=make_quick_join_session_transport(o.host,o.code,error);if(!transport||!transport->open(0,error))throw std::runtime_error(error);
    Launch launch(*transport);
    if(o.host) {std::vector<std::uint8_t> initial(c.state_bytes);if(!world->capture(initial,error)||!launch.host(c,std::move(initial),error))throw std::runtime_error(error);}
    else if(!launch.join(c,[&](auto state,std::string& message){return world->admit_initial(state,message)?PreparationStep::Ready:PreparationStep::Failed;},error))throw std::runtime_error(error);
    auto began=Network::Clock::now();
    while(!shared.stop && !launch.is_open()) {
        if(!launch.service_launch())throw std::runtime_error(launch.error());
        if(o.host&&launch.view().phase==LaunchPhase::Ready&&shared.renderer_ready) {
            // Independent local channel readiness can lag briefly.
            launch.release(error);
        }
        shared.report("Quick Join "+o.code+" | preparing "+std::to_string(launch.view().baseline_received/1024)+" / "+std::to_string(c.state_bytes/1024)+" KiB");
        if(o.duration&&Network::Clock::now()-began>std::chrono::seconds(60))throw std::runtime_error("Private interactive admission timed out.");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if(shared.stop)return;
    while(!shared.renderer_ready&&!shared.stop){launch.service();std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    if(shared.stop)return;
    Session session(*world);if(!session.start({{91,2,6},std::uint8_t(o.host?0:1),1},error))throw std::runtime_error(error);
    ImpairedTransport impaired(launch);
    Network network(session,o.impair?static_cast<SessionTransport&>(impaired):static_cast<SessionTransport&>(launch));
    if(!network.start(*launch.network_configuration(),error))throw std::runtime_error(error);Pump pump(session,network);
    SDL_PauseAudioDevice(audio,0);began=Network::Clock::now();std::uint64_t published_epoch=0;std::uint32_t published_frame=UINT32_MAX,published_replays=UINT32_MAX;
    while(!shared.stop) {
        const auto now=Network::Clock::now();
        if(!pump.pulse(now,[&] {
            if(o.script)return PackedInput{0x8000,std::int8_t((session.frontier()/15)%2?30:-30),0};
            return unpack(shared.input.load());
        }))throw std::runtime_error(pump.error());
        const auto& s=session.statistics();shared.frames=s.next_frame;shared.confirmed=s.confirmed_frames;shared.corrections=s.rollbacks;
        if(session.presentation_ready()&&(session.epoch()!=published_epoch||s.next_frame-1!=published_frame||s.replayed_frames!=published_replays)) {
            if(!world->publish_current(session.epoch(),s.next_frame-1,error))throw std::runtime_error(error);
            published_epoch=session.epoch();published_frame=s.next_frame-1;published_replays=s.replayed_frames;
        }
        shared.report(pump.view().waiting_for_clients()?"Waiting for clients (UI/network active)":"Owned rollback | Space/A: accelerate | arrows/stick: steer | Escape: close");
        if(o.duration&&now-began>=std::chrono::seconds(o.duration)){shared.stop=true;break;}
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    std::cout<<"owned interactive: frames="<<session.statistics().next_frame<<" confirmed="<<session.statistics().confirmed_frames
             <<" corrections="<<session.statistics().rollbacks<<" simulated_drops="<<impaired.dropped()<<'\n';
    if(o.impair)require(session.statistics().rollbacks>0,"Impaired rendered gameplay did not exercise correction.");
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
int main(int argc,char** argv) try {
    const auto o=options(argc,argv);
    require(revision_addresses::select(DKR_OWNED_TEST_REVISION==77?rom::Revision::UsV77:rom::Revision::UsV80),"Integration revision refused.");
    enhancements::set_presentation_profile(enhancements::PresentationProfile::Accurate);
    require(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_EVENTS|SDL_INIT_AUDIO|SDL_INIT_GAMECONTROLLER)==0,SDL_GetError());
    std::uint32_t flags=SDL_WINDOW_SHOWN|SDL_WINDOW_RESIZABLE;
#if !defined(_WIN32)
    flags|=SDL_WINDOW_VULKAN;
#endif
    auto* window=SDL_CreateWindow("DKR-R PRIVATE owned Quick Join integration",SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,800,600,flags);
    require(window!=nullptr,SDL_GetError());
    SDL_AudioSpec wanted{},obtained{};wanted.freq=22050;wanted.format=AUDIO_S16LSB;wanted.channels=2;wanted.samples=1024;
    const auto audio=SDL_OpenAudioDevice(nullptr,0,&wanted,&obtained,0);require(audio!=0,SDL_GetError());
    SDL_GameController* pad=nullptr;Shared shared;
    // Owner/renderer lifetime is bounded by joining BEFORE SDL/window teardown.
    std::thread render,owner;
    StopWorkers shutdown{shared,render,owner};
    render=std::thread(renderer,std::ref(shared),window,std::cref(o));
    owner=std::thread(simulation,std::ref(shared),audio,std::cref(o));
    while(!shared.stop) {
        SDL_Event event;while(SDL_PollEvent(&event))if(event.type==SDL_QUIT||(event.type==SDL_KEYDOWN&&event.key.keysym.scancode==SDL_SCANCODE_ESCAPE))shared.stop=true;
        if(pad&&!SDL_GameControllerGetAttached(pad)){SDL_GameControllerClose(pad);pad=nullptr;}
        if(!pad)for(int i=0;i<SDL_NumJoysticks();++i)if(SDL_IsGameController(i)){pad=SDL_GameControllerOpen(i);if(pad)break;}
        shared.input=pack(input(pad));
        std::string title;
        {std::scoped_lock lock(shared.status_mutex);title=(o.host?"HOST | ":"CLIENT | ")+shared.status;}
        title+=" | frame "+std::to_string(shared.frames)+" confirmed "+std::to_string(shared.confirmed)+" corrections "+std::to_string(shared.corrections);
        SDL_SetWindowTitle(window,title.c_str());SDL_Delay(5);
    }
    owner.join();render.join();
    if(pad)SDL_GameControllerClose(pad);SDL_CloseAudioDevice(audio);SDL_DestroyWindow(window);SDL_Quit();
    if(!shared.failure.empty())throw std::runtime_error(shared.failure);
    require(shared.frames>0&&shared.rendered>0,"Private interactive test stopped before rendered gameplay.");
    return 0;
}catch(const std::exception& e){std::cerr<<"Private interactive integration failed: "<<e.what()<<'\n';return 3;}
