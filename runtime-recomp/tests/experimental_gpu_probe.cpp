// PRIVATE: input contains retail assets and must never enter a package.
// Real RT64/F3DDKR decoding of an owned CPU frame; not live rollback admission,
// modern interpolation qualification, or a substitute guest scheduler.
#include "f3ddkr_rt64.hpp"
#include "renderer_snapshot.hpp"
#include "revision_addresses.hpp"
#include "runtime_enhancements.hpp"
#include "netplay/experimental_presentation.hpp"
#include "netplay/experimental_present_gate.hpp"
#include "netplay/experimental_checkpoint_hash.hpp"
#include "netplay/runtime_state.hpp"
#include "probe_bridge.h"
#include "vi_presentation_policy.hpp"
#include "hle/rt64_application.h"
#include "hle/rt64_state.h"
#include "hle/rt64_workload_queue.h"
#include "hle/rt64_present_queue.h"
#include "render/rt64_framebuffer_renderer.h"
#include "render/rt64_buffer_uploader.h"
#include "plume_vulkan.h"
#include <SDL.h>
#include <SDL_syswm.h>
#include <array>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#if defined(_WIN32)
#include <Windows.h>
#endif

namespace {
using namespace dkr::runtime;
using namespace dkr::runtime::netplay;
using namespace dkr::runtime::netplay::experimental;
void require(bool ok,const char* why) {if(!ok)throw std::runtime_error(why);}
std::uint32_t word(std::span<const std::uint8_t> ram,unsigned offset) {
    require(offset<=ram.size()-4,"Invalid private word");
    std::uint32_t value;std::memcpy(&value,ram.data()+offset,4);return value;
}
std::uint64_t integer(std::ifstream& file) {
    std::array<std::uint8_t,8> b{};require(bool(file.read(reinterpret_cast<char*>(b.data()),8)),"Truncated header");
    std::uint64_t value=0;for(unsigned i=0;i<8;++i)value|=std::uint64_t(b[i])<<(8*i);return value;
}
struct PrivateFrame {
    unsigned revision=0,players=0;
    std::vector<std::uint8_t> ram;
    RenderDescriptor descriptor{};
};
PrivateFrame load(const char* path) {
    std::ifstream file(std::filesystem::u8path(path),std::ios::binary);
    std::array<char,8> magic{};require(bool(file.read(magic.data(),8))&&std::memcmp(magic.data(),"DKRCOMP2",8)==0,"Not a current ABI-tagged private component; regenerate the private proof");
    const auto revision=integer(file),players=integer(file),bytes=integer(file),journals=integer(file);
    require((revision==77||revision==80)&&players>=2&&players<=4&&bytes<32U*1024U*1024U&&journals<=320,"Invalid private header");
    const auto schema=integer(file),native_bytes=integer(file),context_bytes=integer(file),cpu_bytes=integer(file);
    RuntimeState decoder;
    // Current fully owned payload: mode, scene cuts, synchronous audio,
    // inputs, authored CPU, full scenes and confirmed-frontier schema marker.
    // Never reinterpret an older fixture after native participant changes.
    require(schema==0x434F4D504F4E454EULL+4+8+4096+128+768+8192+16384&&native_bytes==sizeof(dkr_probe_native_state)&&
            context_bytes==sizeof(SerializableContext)&&cpu_bytes==decoder.snapshot_size(),"Incompatible private CPU/native participant schema");
    std::vector<std::uint8_t> state(bytes);require(bool(file.read(reinterpret_cast<char*>(state.data()),state.size())),"Truncated private state");
    require(bytes>=decoder.snapshot_size()+sizeof(dkr_probe_native_state),"Missing private participants");
    recomp_context cpu{},audio{};decoder.register_context(&cpu);decoder.register_context(&audio);
    PrivateFrame result;result.revision=unsigned(revision);result.players=unsigned(players);result.ram.resize(kRollbackMemoryBytes);
    std::uint32_t tag=0;require(decoder.restore(result.ram.data(),std::span(state).first(decoder.snapshot_size()),tag),"Invalid private CPU state");
    dkr_probe_native_state native{};std::memcpy(&native,state.data()+decoder.snapshot_size(),sizeof(native));
    require(native.video_enabled==1&&native.video_frames&&!native.pending_scene_site,"Not a completed retail frame");
    const auto slot=1-word(result.ram,revision==77?0x1234E8:0x123A68),base=revision==77?0x1211F0U:0x121770U;
    require(slot<=1,"Invalid completed task parity");
    result.descriptor={native.video_frames-1,word(result.ram,base+slot*4),word(result.ram,base+8),native.video_framebuffer,native.video_depthbuffer,native.video_black};
    return result;
}
// Hardware ownership is private and distinct from rewindable guest memory.
struct Hardware {
    std::array<std::uint8_t,64> header{};
    std::array<std::uint8_t,4096> dmem{},imem{};
    std::array<std::uint32_t,9> dp{};
    std::array<std::uint32_t,14> vi{};
    RT64::Application::Core bind(SDL_Window* window,std::uint8_t* parked_ram) {
        RT64::Application::Core core{};
#if defined(_WIN32)
        SDL_SysWMinfo info{};SDL_VERSION(&info.version);
        require(SDL_GetWindowWMInfo(window,&info)==SDL_TRUE,"Missing native test window");
        core.window=info.info.win.window;
#else
        core.window=window;
#endif
        core.HEADER=header.data();core.RDRAM=parked_ram;core.DMEM=dmem.data();core.IMEM=imem.data();
        core.MI_INTR_REG=&dp[0];core.DPC_START_REG=&dp[1];core.DPC_END_REG=&dp[2];core.DPC_CURRENT_REG=&dp[3];
        core.DPC_STATUS_REG=&dp[4];core.DPC_CLOCK_REG=&dp[5];core.DPC_BUFBUSY_REG=&dp[6];
        core.DPC_PIPEBUSY_REG=&dp[7];core.DPC_TMEM_REG=&dp[8];
        core.VI_STATUS_REG=&vi[0];core.VI_ORIGIN_REG=&vi[1];core.VI_WIDTH_REG=&vi[2];core.VI_INTR_REG=&vi[3];
        core.VI_V_CURRENT_LINE_REG=&vi[4];core.VI_TIMING_REG=&vi[5];core.VI_V_SYNC_REG=&vi[6];core.VI_H_SYNC_REG=&vi[7];
        core.VI_LEAP_REG=&vi[8];core.VI_H_START_REG=&vi[9];core.VI_V_START_REG=&vi[10];core.VI_V_BURST_REG=&vi[11];
        core.VI_X_SCALE_REG=&vi[12];core.VI_Y_SCALE_REG=&vi[13];
        // RT64's emulated MI callback acknowledges only these private registers;
        // no guest OS completion is manufactured or delivered by this target.
        core.checkInterrupts=+[] {};
        return core;
    }
    void present(const RenderDescriptor& d) {
        // Reviewed NTSC progressive 320x240 VI; preserve the game's draw/black
        // choice. Native VI scheduling itself is NOT qualified by this fixture.
        vi={dkr::runtime::presentation::kRetailNtscViStatus,dkr::runtime::presentation::retail_ntsc_vi_origin(d.framebuffer),320,2,0,0x03E52239,0x20D,0xC15,0x0C150C15,
            d.black ? 0U:0x006C02ECU,0x2501FF,0xE0204,0x200,0x400};
        vi[10]=presentation::canonicalise_dkr_v_region(vi[10],vi[13]);
    }
};
void drain(RT64::Application& app) {
    app.drawDataUploader->wait();app.transformsUploader->wait();app.tilesUploader->wait();
    app.state->framebufferRenderer->waitForUploaders();
    app.workloadQueue->waitForWorkloadId(app.state->workloadId);
    app.presentQueue->waitForPresentId(app.state->presentId);
    app.workloadQueue->waitForIdle();app.presentQueue->waitForIdle();
}
std::vector<std::uint8_t> pixels(RT64::Application& app,const RenderDescriptor& d) {
    using namespace plume;
    auto& targets=app.sharedQueueResources->renderTargetManager;
    const RT64::RenderTargetKey key(d.framebuffer&0x007FFFFFU,320,G_IM_SIZ_16b,RT64::Framebuffer::Type::Color);
    const auto found=targets.targetMap.find(key.hash());
    require(found!=targets.targetMap.end()&&!found->second->isEmpty(),"Actual color target missing");
    auto& target=*found->second;
    require(target.format==RenderFormat::R8G8B8A8_UNORM&&target.width&&target.height,"Unexpected private target format");
    const auto size=std::uint64_t(target.width)*target.height*4;
    require(size<16U*1024U*1024U,"Unbounded private readback");
    auto queue=app.device->createCommandQueue(RenderCommandListType::DIRECT);
    auto list=queue->createCommandList();auto fence=app.device->createCommandFence();
    auto output=app.device->createBuffer(RenderBufferDesc::ReadbackBuffer(size));
    require(queue&&list&&fence&&output,"Readback resource allocation failed");
    auto* source=target.getResolvedTexture();auto* commands=static_cast<VulkanCommandList*>(list.get());
    list->begin();list->barriers(RenderBarrierStage::COPY,RenderTextureBarrier(source,RenderTextureLayout::COPY_SOURCE));
    list->barriers(RenderBarrierStage::COPY,RenderBufferBarrier(output.get(),RenderBufferAccess::WRITE));
    VkBufferImageCopy copy{};copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={target.width,target.height,1};
    vkCmdCopyImageToBuffer(commands->vk,static_cast<VulkanTexture*>(source)->vk,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        static_cast<VulkanBuffer*>(output.get())->vk,1,&copy);
    list->barriers(RenderBarrierStage::GRAPHICS,RenderTextureBarrier(source,RenderTextureLayout::SHADER_READ));
    list->end();queue->executeCommandLists(list.get(),fence.get());queue->waitForCommandFence(fence.get());
    const RenderRange range{0,size},none{0,0};
    auto* read=static_cast<const std::uint8_t*>(output->map(0,&range));require(read!=nullptr,"Readback mapping failed");
    std::vector<std::uint8_t> result(read,read+size);output->unmap(0,&none);
    std::size_t lit=0;for(std::size_t i=0;i<result.size();i+=4)if(result[i]||result[i+1]||result[i+2])++lit;
    require(lit>4096,"Actual retail GPU output is blank");
    std::cout<<"  GPU readback "<<target.width<<"x"<<target.height<<" lit="<<lit<<" hash="<<std::hex<<checkpoint_hash(result)<<std::dec<<std::endl;
    return result;
}
struct ApplicationEnd {
    RT64::Application& app;
    ~ApplicationEnd() {app.end();}
};
struct DecodeDrain {
    RT64::Application& app;
    ~DecodeDrain() {drain(app);}
};
}
int main(int argc,char** argv) {
    try {
        require(argc==3||argc==4,"Usage: PRIVATE-PROBE private.dkr-component private-renderer-directory [different-obsolete.dkr-component]");
        const auto original=load(argv[1]);
        const auto alternate=argc==4?load(argv[3]):original;
        const bool modern=std::getenv("DKR_GPU_CHECK_MODERN")!=nullptr;
        const auto revision=original.revision;const auto& ram=original.ram;
        require(alternate.revision==revision,"Mixed-revision render qualification refused");
        auto descriptor=original.descriptor;
        require(revision_addresses::select(revision==77?rom::Revision::UsV77:rom::Revision::UsV80),"Revision refused");
        enhancements::set_presentation_profile(modern?enhancements::PresentationProfile::Modern:enhancements::PresentationProfile::Accurate);
        PresentationMailbox mailbox;require(mailbox.begin_epoch(91)&&mailbox.publish(91,descriptor,ram),"Completed frame validation failed");
        auto lease=mailbox.take();require(bool(lease),"Missing immutable lease");
        const auto original_hash=checkpoint_hash(ram);
        require(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_EVENTS)==0,SDL_GetError());
        // Linux RT64 asks SDL to create the Vulkan surface. SDL refuses that
        // request unless this window was explicitly created for Vulkan. The
        // Windows path instead supplies the HWND to the native surface owner.
        std::uint32_t window_flags=SDL_WINDOW_SHOWN|SDL_WINDOW_RESIZABLE;
#if !defined(_WIN32)
        window_flags|=SDL_WINDOW_VULKAN;
#endif
        SDL_Window* window=SDL_CreateWindow("DKR-R private rollback GPU ownership test",SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,960,720,window_flags);
        require(window!=nullptr,SDL_GetError());
        // This parked image is never a CPU simulation owner or a decoder target.
        std::vector<std::uint8_t> parked(kRollbackMemoryBytes);
        Hardware hardware;RT64::ApplicationConfiguration configuration{};
        configuration.appId="dkr-private-rollback-gpu";configuration.detectDataPath=false;
        configuration.useConfigurationFile=false;configuration.dataPath=std::filesystem::u8path(argv[2]);
        {
            // Registration outlives ApplicationEnd's worker shutdown.
            std::unique_ptr<present_gate::Registration> registration;
            RT64::Application app(hardware.bind(window,parked.data()),configuration);
            // The RT64 destructor alone is not its worker shutdown contract.
            // End the application before member destruction on ALL exits.
            ApplicationEnd shutdown{app};
            app.userConfig.graphicsAPI=RT64::UserConfiguration::GraphicsAPI::Vulkan;
            app.userConfig.antialiasing=RT64::UserConfiguration::Antialiasing::None;
            app.userConfig.aspectRatio=app.userConfig.extAspectRatio=RT64::UserConfiguration::AspectRatio::Original;
            app.userConfig.resolution=RT64::UserConfiguration::Resolution::Manual;
            app.userConfig.resolutionMultiplier=modern?4:2;app.userConfig.downsampleMultiplier=1;
            app.userConfig.refreshRate=modern?RT64::UserConfiguration::RefreshRate::Manual:RT64::UserConfiguration::RefreshRate::Original;
            app.userConfig.refreshRateTarget=modern?120:30;
            app.userConfig.internalColorFormat=RT64::UserConfiguration::InternalColorFormat::Standard;
            app.userConfig.idleWorkActive=false;
            app.enhancementConfig.presentation.mode=RT64::EnhancementConfiguration::Presentation::Mode::Console;
            app.enhancementConfig.presentation.removeBlackBorders=true;app.enhancementConfig.rect.fixRectLR=true;
            unsigned thread_id=0;
#if defined(_WIN32)
            thread_id=GetCurrentThreadId();
#endif
            require(app.setup(thread_id)==RT64::Application::SetupResult::Success,"Actual RT64 setup failed");
            // Compare a single shader path. Background specialization may
            // legitimately change raster rounding after the first draw.
            // This is a PRIVATE comparison setting, not a runtime policy.
            app.workloadQueue->ubershadersOnly=true;
#if defined(DKR_EXPERIMENTAL_RENDER_QUALIFICATION) || defined(DKR_EXPERIMENTAL_RACE_TEST)
            registration=std::make_unique<present_gate::Registration>(app.state.get());
#endif
            std::cout<<"Actual RT64 initialized; private revision="<<revision<<" framebuffer="<<std::hex<<descriptor.framebuffer<<" display="<<descriptor.display_start<<std::dec<<" black="<<descriptor.black<<std::endl;
            F3DDKRRT64Bridge bridge;
            std::vector<std::uint8_t> expected_pixels;
            for(unsigned iteration=0;iteration<5;++iteration) {
                auto workspace=DecodeWorkspace::create(lease);require(bool(workspace),"Decode lease already owned");
                {
                    RendererSnapshotScope scope(app.core.RDRAM,app.state->RDRAM,workspace->bytes().data());
                    // Guard before calling a decoder which can throw. Failed
                    // assertions must not free RAM retained by upload workers.
                    DecodeDrain consumers{app};
                    std::unique_ptr<present_gate::Scope> submission_scope;
                    if(registration)submission_scope=std::make_unique<present_gate::Scope>(*registration,lease);
                    // Destroy/unpark BEFORE the drain guard on every exit.
                    std::unique_ptr<present_gate::QueueHoldForTest> hold;
                    OSTask task{};task.t.type=M_GFXTASK;task.t.data_ptr=descriptor.display_start;task.t.data_size=descriptor.display_end-descriptor.display_start;
                    const auto tasks_before=completed_f3ddkr_task_count(),workload_before=app.state->workloadId,present_before=app.state->presentId;
                    bridge.process(app,task);
                    std::cout<<"  decoded iteration="<<iteration<<" workload="<<app.state->workloadId<<" present="<<app.state->presentId<<std::endl;
                    require(completed_f3ddkr_task_count()==tasks_before+1&&app.state->workloadId>workload_before,"No actual graphics workload");
                    // Console mode must not auto-present obsolete decode work.
                    require(app.state->presentId==present_before,"Decode bypassed explicit present ownership");
                    if(iteration==1) {
                        if(registration) {
                            hold=std::make_unique<present_gate::QueueHoldForTest>(*registration);
                            hardware.present(descriptor);
                            // Force an explicit repeated VI in this private
                            // queue test. Runtime coalescing is NOT changed.
                            app.state->lastScreenVI=RT64::VI{};
                            app.updateScreen();
                            require(app.state->presentId>present_before&&hold->wait(std::chrono::seconds(10)),
                                    "Actual queued present did not reach the private interlock");
                        }
                        require(mailbox.retire_for_restore(91,descriptor.frame),"Correction retirement refused");
                        require(!mailbox.is_current(lease),"Obsolete image still current");
                        hold.reset(); // Worker must reject at its real consumption boundary.
                    }else if(iteration==3 && registration) {
                        require(mailbox.is_current(lease),"Started image unexpectedly retired");
                        hold=std::make_unique<present_gate::QueueHoldForTest>(*registration,present_gate::HoldPoint::AfterAdmission);
                        hardware.present(descriptor);app.state->lastScreenVI=RT64::VI{};app.updateScreen();
                        require(app.state->presentId>present_before&&hold->wait(std::chrono::seconds(10)),
                                "Started present did not reach the private interlock");
                        require(!mailbox.submissions_drained(),"Started operation lost its retirement permit");
                        require(mailbox.retire_for_restore(92,descriptor.frame)&&!mailbox.is_current(lease),
                                "Started operation retirement refused");
                        require(!mailbox.submissions_drained(),"Retirement falsely reported an active operation drained");
                        require(checkpoint_hash(ram)==original_hash,"Waiting for presentation changed simulation bytes");
                        hold.reset(); // Permit may complete; CPU continuation remains parked until drain.
                    }else {
                        require(mailbox.is_current(lease),"Current image unexpectedly retired");
                        if(registration)app.state->lastScreenVI=RT64::VI{};
                        hardware.present(descriptor);app.updateScreen();
                        // Repeated identical VI/content may correctly coalesce.
                        require(app.state->presentId>=present_before,"VI present cursor rewound");
                    }
                }
                const auto actual_pixels=pixels(app,descriptor);
                require(!registration || registration->completed(app.state->workloadId,app.state->presentId),
                        "Full workload/present completion callbacks did not release the exact submission");
                if(iteration==0)expected_pixels=actual_pixels;
                else if(iteration==1&&argc==4)require(actual_pixels!=expected_pixels,"Alternate obsolete frame is not visibly different");
                else require(actual_pixels==expected_pixels,"Obsolete GPU history contaminated corrected output");
                require(app.core.RDRAM==parked.data()&&app.state->RDRAM==parked.data(),"Decode pointers leaked");
                require(mailbox.submissions_drained(),"GPU/WSI submission did not drain before owner continuation");
                require(checkpoint_hash(lease->bytes())==lease->hash()&&checkpoint_hash(ram)==original_hash,"GPU mutated immutable simulation image");
                workspace.reset();lease.reset();
                if(iteration<4) {
                    if(iteration==1)require(mailbox.begin_epoch(92),"GPU lease did not drain for new scene");
                    if(iteration==3)require(mailbox.begin_epoch(93),"Started GPU operation did not drain for new scene");
                    const auto next_frame=descriptor.frame+1;
                    const auto& next=iteration==0?alternate:original;
                    descriptor=next.descriptor;descriptor.frame=next_frame;
                    if(!mailbox.publish(iteration>=3?93:iteration>=1?92:91,descriptor,next.ram)) {
                        std::cerr<<"Rejected private descriptor frame="<<descriptor.frame<<" start="<<std::hex
                                 <<descriptor.display_start<<" end="<<descriptor.display_end<<" fb="
                                 <<descriptor.framebuffer<<" depth="<<descriptor.depthbuffer<<std::dec
                                 <<" black="<<descriptor.black<<'\n';
                        if(descriptor.display_end>=0x80000010U&&descriptor.display_end<=0x81000000U)
                            for(unsigned i=16;i>0;i-=4)std::cerr<<std::hex<<word(next.ram,descriptor.display_end-0x80000000U-i)<<' ';
                        std::cerr<<std::dec<<'\n';
                        require(false,"Replacement publication refused");
                    }
                    lease=mailbox.take();require(bool(lease),"Replacement lease missing");
                }
                SDL_Event event;while(SDL_PollEvent(&event)) {}
            }
            require(mailbox.quiescent(),"Final render lease retained");
            if(registration) {
                const auto stats=registration->statistics();
                require(stats.tagged==5&&stats.accepted==4&&stats.rejected==1,"Actual present-queue retirement counts disagree");
                std::cout<<"  actual present queue: tagged="<<stats.tagged<<" accepted="<<stats.accepted<<" rejected="<<stats.rejected
                         <<"; rejected event still completed original queue/barrier waits\n";
            }
        }
        SDL_DestroyWindow(window);SDL_Quit();
        std::cout<<"PASS PRIVATE actual Vulkan/F3DDKR: 5 real workloads, queued retirement rejected, started presentation drains before owner continuation, corrected nonblank GPU readback unchanged by obsolete history; exact full-operation callbacks completed, all queue/upload consumers drained, immutable CPU image unchanged. "
                 <<(modern?"Modern 4x/120 FPS":"Accurate 2x/30 FPS")<<"; no live adapter admission.\n";
        return 0;
    }catch(const std::exception& error) {std::cerr<<"Private GPU qualification failed: "<<error.what()<<'\n';return 3;}
}
