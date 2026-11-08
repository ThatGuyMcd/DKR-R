#include "camera_xr.hpp"
#include "plume_vulkan.h"
#define XR_USE_GRAPHICS_API_VULKAN
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <sstream>

namespace dkr::runtime::camera {
namespace {
void check(XrResult value,const char* operation) {
    if(XR_FAILED(value))throw mods::Error(std::string(operation)+" failed (OpenXR "+std::to_string(value)+").");
}
XrQuaternionf multiply(XrQuaternionf a,XrQuaternionf b) {
    return {a.w*b.x+a.x*b.w+a.y*b.z-a.z*b.y,
        a.w*b.y-a.x*b.z+a.y*b.w+a.z*b.x,
        a.w*b.z+a.x*b.y-a.y*b.x+a.z*b.w,
        a.w*b.w-a.x*b.x-a.y*b.y-a.z*b.z};
}
XrVector3f rotate(XrQuaternionf q,XrVector3f p) {
    auto r=multiply(multiply(q,{p.x,p.y,p.z,0}),{-q.x,-q.y,-q.z,q.w});return {r.x,r.y,r.z};
}
XrQuaternionf rotation(const DkrViewPose& p) {
    const float y=p.yaw*0.5F,x=-p.pitch*0.5F,z=p.roll*0.5F;
    return multiply(multiply({0,std::sin(y),0,std::cos(y)},{std::sin(x),0,0,std::cos(x)}),{0,0,std::sin(z),std::cos(z)});
}
bool finite(const DkrViewPose& p,float scale) {
    for(float v:p.position)if(!std::isfinite(v)||std::abs(v)>1.e7F)return false;
    return std::isfinite(p.yaw)&&std::isfinite(p.pitch)&&std::isfinite(p.roll)&&
        std::isfinite(p.near_plane)&&p.near_plane>=0.1F&&std::isfinite(p.far_plane)&&p.far_plane>p.near_plane&&
        std::isfinite(scale)&&scale>=1&&scale<=1000;
}
bool valid_pose(const XrPosef& pose) {
    const auto& q=pose.orientation;const auto& p=pose.position;
    const float length=q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w;
    return std::isfinite(length)&&std::abs(length-1.F)<0.01F&&
        std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z)&&
        std::abs(p.x)<1000&&std::abs(p.y)<1000&&std::abs(p.z)<1000;
}
bool valid_fov(const XrFovf& fov) {
    constexpr float limit=1.57079632679F;
    return std::isfinite(fov.angleLeft)&&std::isfinite(fov.angleRight)&&
        std::isfinite(fov.angleUp)&&std::isfinite(fov.angleDown)&&
        fov.angleLeft<fov.angleRight&&fov.angleDown<fov.angleUp&&
        fov.angleLeft>-limit&&fov.angleRight<limit&&
        fov.angleDown>-limit&&fov.angleUp<limit;
}
}
struct XrSession::Impl {
    XrInstance instance=XR_NULL_HANDLE;
    XrSystemId system=XR_NULL_SYSTEM_ID;
    ::XrSession session=XR_NULL_HANDLE;
    XrSpace local=XR_NULL_HANDLE,base=XR_NULL_HANDLE,view=XR_NULL_HANDLE;
    XrSessionState state=XR_SESSION_STATE_UNKNOWN;
    bool active=false,lost=false,enable2=false,tracking=false;
    XrEnvironmentBlendMode blend=XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    unsigned max_layers=0;
    std::uint64_t graphics_generation=1;
    Log log;
    std::atomic<bool> recenter_requested{true};
    std::string error;
    XrGraphicsRequirementsVulkanKHR requirements{XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN_KHR};
    PFN_xrGetVulkanInstanceExtensionsKHR instance_extensions=nullptr;
    PFN_xrGetVulkanDeviceExtensionsKHR device_extensions=nullptr;
    PFN_xrGetVulkanGraphicsDeviceKHR physical_device=nullptr;
    PFN_xrGetVulkanGraphicsDevice2KHR physical_device2=nullptr;
    PFN_xrCreateVulkanInstanceKHR create_instance=nullptr;
    PFN_xrCreateVulkanDeviceKHR create_device=nullptr;
    plume::VulkanDevice* device=nullptr;
    plume::VulkanCommandQueue* queue=nullptr;
    VkInstance selected_instance=VK_NULL_HANDLE;
    VkPhysicalDevice selected_device=VK_NULL_HANDLE;
    struct Chain {
        XrSwapchain handle=XR_NULL_HANDLE;
        unsigned width=0,height=0,index=0;
        bool acquired=false,waited=false;
        std::vector<std::unique_ptr<plume::VulkanTexture>> images;
    };
    std::array<Chain,3> chains;

    void note(const std::string& value) noexcept {
        try {if(log)log("[camera++][openxr] "+value);}catch(...){}
    }
    VkResult vulkan_result(XrResult xr,VkResult vk,const char* operation) {
        if(XR_SUCCEEDED(xr))return vk;
        error=std::string(operation)+" failed (OpenXR "+std::to_string(xr)+").";
        note(error);return VK_ERROR_INITIALIZATION_FAILED;
    }

    template<class T> T function(const char* name) {
        PFN_xrVoidFunction function=nullptr;check(xrGetInstanceProcAddr(instance,name,&function),name);
        if(!function)throw mods::Error(std::string("Missing OpenXR entry point: ")+name);
        return reinterpret_cast<T>(function);
    }
    template<class F> std::vector<std::string> extensions(F fn) {
        std::uint32_t size=0;check(fn(instance,system,0,&size,nullptr),"Read XR Vulkan extensions");
        if(size>16384)throw mods::Error("OpenXR Vulkan extension list exceeds its limit.");
        std::vector<char> text(size+1,0);check(fn(instance,system,size,&size,text.data()),"Read XR Vulkan extensions");
        std::istringstream words(text.data());std::vector<std::string> result;std::string word;
        while(words>>word)result.push_back(word);
        return result;
    }
    void poll() {
        XrEventDataBuffer event{XR_TYPE_EVENT_DATA_BUFFER};
        XrResult result;
        while((result=xrPollEvent(instance,&event))==XR_SUCCESS) {
            if(event.type==XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING)lost=true;
            if(event.type==XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
                const auto& changed=reinterpret_cast<const XrEventDataSessionStateChanged&>(event);
                if(changed.session==session) {
                    state=changed.state;
                    note("session-state="+std::to_string(state));
                    if(state==XR_SESSION_STATE_READY&&!active) {
                        XrSessionBeginInfo begin{XR_TYPE_SESSION_BEGIN_INFO};begin.primaryViewConfigurationType=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                        check(xrBeginSession(session,&begin),"Begin headset session");active=true;
                    } else if(state==XR_SESSION_STATE_STOPPING&&active) {
                        check(xrEndSession(session),"End headset session");active=false;
                    } else if(state==XR_SESSION_STATE_EXITING||state==XR_SESSION_STATE_LOSS_PENDING){active=false;lost=true;}
                }
            }
            event={XR_TYPE_EVENT_DATA_BUFFER};
        }
        if(result!=XR_EVENT_UNAVAILABLE)check(result,"Poll headset session");
    }
    bool acquire(Chain& chain) {
        if(!chain.acquired) {
            XrSwapchainImageAcquireInfo info{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
            check(xrAcquireSwapchainImage(chain.handle,&info,&chain.index),"Acquire headset image");chain.acquired=true;
        }
        if(chain.index>=chain.images.size())throw mods::Error("OpenXR returned an invalid swapchain image.");
        if(!chain.waited) {
            XrSwapchainImageWaitInfo wait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};wait.timeout=100000000; // 100 ms, presentation thread only.
            const auto result=xrWaitSwapchainImage(chain.handle,&wait);
            if(result==XR_TIMEOUT_EXPIRED)return false; // Retain acquisition; wait again next frame, never reacquire it.
            check(result,"Wait for headset image");chain.waited=true;
        }
        return true;
    }
    void release(Chain& chain) {
        XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
        check(xrReleaseSwapchainImage(chain.handle,&release),"Release headset image");chain.acquired=chain.waited=false;
    }
    void create_chain(Chain& chain,unsigned width,unsigned height,std::int64_t format) {
        chain.width=width;chain.height=height;
        XrSwapchainCreateInfo info{XR_TYPE_SWAPCHAIN_CREATE_INFO};
        info.usageFlags=XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT|XR_SWAPCHAIN_USAGE_SAMPLED_BIT|XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
        info.format=format;info.sampleCount=1;info.width=width;info.height=height;info.faceCount=info.arraySize=info.mipCount=1;
        check(xrCreateSwapchain(session,&info,&chain.handle),"Create headset swapchain");
        std::uint32_t count=0;check(xrEnumerateSwapchainImages(chain.handle,0,&count,nullptr),"Read headset swapchain");
        if(!count||count>16)throw mods::Error("Unsupported headset swapchain image count.");
        std::vector<XrSwapchainImageVulkanKHR> images(count,{XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR});
        check(xrEnumerateSwapchainImages(chain.handle,count,&count,reinterpret_cast<XrSwapchainImageBaseHeader*>(images.data())),"Read headset images");
        for(const auto& image:images) {
            auto texture=std::make_unique<plume::VulkanTexture>(device,image.image);
            auto render_format=format==VK_FORMAT_R8G8B8A8_UNORM?plume::RenderFormat::R8G8B8A8_UNORM:plume::RenderFormat::B8G8R8A8_UNORM;
            texture->desc=plume::RenderTextureDesc::ColorTarget(width,height,render_format);
            texture->imageFormat=VkFormat(format);texture->fillSubresourceRange();texture->createImageView(VkFormat(format));
            if(!texture->imageView)throw mods::Error("Unable to create a headset render view.");
            // VulkanTexture's borrowed-image constructor has ownership=false.
            // We destroy only our image view, never the runtime-owned VkImage.
            chain.images.push_back(std::move(texture));
        }
    }
};
XrSession::XrSession(Log log):impl_(std::make_unique<Impl>()){impl_->log=std::move(log);}
XrSession::~XrSession(){shutdown();}
const std::string& XrSession::error()const{return impl_->error;}
bool XrSession::running()const{return impl_->active&&!impl_->lost;}
bool XrSession::discover() noexcept {
    auto& p=*impl_;if(p.instance)return !p.lost;
    p.lost=false;
    try {
        std::uint32_t count=0;check(xrEnumerateInstanceExtensionProperties(nullptr,0,&count,nullptr),"Read OpenXR extensions");
        if(count>512)throw mods::Error("OpenXR extension count exceeds its limit.");
        std::vector<XrExtensionProperties> extensions(count,{XR_TYPE_EXTENSION_PROPERTIES});
        check(xrEnumerateInstanceExtensionProperties(nullptr,count,&count,extensions.data()),"Read OpenXR extensions");
        const auto supported=[&](const char* name){return std::any_of(extensions.begin(),extensions.end(),[&](const auto& e){return std::strcmp(e.extensionName,name)==0;});};
        p.enable2=supported(XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME);
        if(!p.enable2&&!supported(XR_KHR_VULKAN_ENABLE_EXTENSION_NAME))
            throw mods::Error("This headset runtime does not expose the supported Vulkan graphics binding.");
        const char* enabled[]{p.enable2?XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME:XR_KHR_VULKAN_ENABLE_EXTENSION_NAME};
        XrInstanceCreateInfo create{XR_TYPE_INSTANCE_CREATE_INFO};
        constexpr char application_name[]="DKR-R Camera++",engine_name[]="DKR-R";
        std::memcpy(create.applicationInfo.applicationName,application_name,sizeof(application_name));
        std::memcpy(create.applicationInfo.engineName,engine_name,sizeof(engine_name));
        create.applicationInfo.apiVersion=XR_MAKE_VERSION(1,0,0);create.enabledExtensionCount=1;create.enabledExtensionNames=enabled;
        check(xrCreateInstance(&create,&p.instance),"Open headset runtime");
        XrInstanceProperties properties{XR_TYPE_INSTANCE_PROPERTIES};check(xrGetInstanceProperties(p.instance,&properties),"Read headset runtime identity");
        p.note("runtime="+std::string(properties.runtimeName)+" version="+std::to_string(properties.runtimeVersion)+" binding="+enabled[0]);
        XrSystemGetInfo system{XR_TYPE_SYSTEM_GET_INFO};system.formFactor=XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
        check(xrGetSystem(p.instance,&system,&p.system),"Find headset");
        XrSystemProperties system_properties{XR_TYPE_SYSTEM_PROPERTIES};check(xrGetSystemProperties(p.instance,p.system,&system_properties),"Read headset system");
        p.max_layers=system_properties.graphicsProperties.maxLayerCount;
        if(!p.max_layers)throw mods::Error("Headset runtime cannot display composition layers.");
        p.note("system="+std::string(system_properties.systemName)+" max-layers="+std::to_string(p.max_layers));
        if(p.enable2) {
            p.physical_device2=p.function<PFN_xrGetVulkanGraphicsDevice2KHR>("xrGetVulkanGraphicsDevice2KHR");
            p.create_instance=p.function<PFN_xrCreateVulkanInstanceKHR>("xrCreateVulkanInstanceKHR");
            p.create_device=p.function<PFN_xrCreateVulkanDeviceKHR>("xrCreateVulkanDeviceKHR");
            auto requirements=p.function<PFN_xrGetVulkanGraphicsRequirements2KHR>("xrGetVulkanGraphicsRequirements2KHR");
            check(requirements(p.instance,p.system,&p.requirements),"Read headset graphics requirements");
        } else {
            p.instance_extensions=p.function<PFN_xrGetVulkanInstanceExtensionsKHR>("xrGetVulkanInstanceExtensionsKHR");
            p.device_extensions=p.function<PFN_xrGetVulkanDeviceExtensionsKHR>("xrGetVulkanDeviceExtensionsKHR");
            p.physical_device=p.function<PFN_xrGetVulkanGraphicsDeviceKHR>("xrGetVulkanGraphicsDeviceKHR");
            auto requirements=p.function<PFN_xrGetVulkanGraphicsRequirementsKHR>("xrGetVulkanGraphicsRequirementsKHR");
            check(requirements(p.instance,p.system,&p.requirements),"Read headset graphics requirements");
        }
        check(xrEnumerateEnvironmentBlendModes(p.instance,p.system,XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,0,&count,nullptr),"Read headset blend modes");
        if(!count||count>16)throw mods::Error("Unsupported headset blend-mode count.");
        std::vector<XrEnvironmentBlendMode> modes(count);
        check(xrEnumerateEnvironmentBlendModes(p.instance,p.system,XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,count,&count,modes.data()),"Read headset blend modes");
        p.blend=modes[0];
        if(std::find(modes.begin(),modes.end(),XR_ENVIRONMENT_BLEND_MODE_OPAQUE)!=modes.end())p.blend=XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
        p.error.clear();return true;
    }catch(const std::exception& e){p.error=e.what();p.note("unavailable: "+p.error);shutdown();return false;}
}
XrSession::Factory XrSession::graphics_factory() {
    auto& p=*impl_;
    if(!p.instance||p.lost)throw mods::Error("Discover the headset before graphics initialization.");
    plume::VulkanExternalConfiguration config;
    const auto generation=p.graphics_generation;
    if(p.enable2) {
        config.createInstance=[this,generation](const VkInstanceCreateInfo& create,VkInstance* instance) {
            auto& p=*impl_;
            if(!p.instance||p.lost||p.graphics_generation!=generation)return VK_ERROR_INITIALIZATION_FAILED;
            XrVulkanInstanceCreateInfoKHR info{XR_TYPE_VULKAN_INSTANCE_CREATE_INFO_KHR};
            info.systemId=p.system;info.pfnGetInstanceProcAddr=vkGetInstanceProcAddr;info.vulkanCreateInfo=&create;
            VkResult result=VK_ERROR_INITIALIZATION_FAILED;
            const auto xr=p.create_instance(p.instance,&info,instance,&result);
            return p.vulkan_result(xr,result,"Create headset Vulkan instance");
        };
        config.createDevice=[this,generation](VkPhysicalDevice physical,const VkDeviceCreateInfo& create,VkDevice* device) {
            auto& p=*impl_;
            if(!p.instance||p.lost||p.graphics_generation!=generation)return VK_ERROR_INITIALIZATION_FAILED;
            XrVulkanDeviceCreateInfoKHR info{XR_TYPE_VULKAN_DEVICE_CREATE_INFO_KHR};
            info.systemId=p.system;info.pfnGetInstanceProcAddr=vkGetInstanceProcAddr;info.vulkanPhysicalDevice=physical;info.vulkanCreateInfo=&create;
            VkResult result=VK_ERROR_INITIALIZATION_FAILED;
            const auto xr=p.create_device(p.instance,&info,device,&result);
            return p.vulkan_result(xr,result,"Create headset Vulkan device");
        };
    } else {
        config.instanceExtensions=p.extensions(p.instance_extensions);config.deviceExtensions=p.extensions(p.device_extensions);
    }
    const auto version=std::max<XrVersion>(p.requirements.minApiVersionSupported,XR_MAKE_VERSION(1,2,0));
    if(version>p.requirements.maxApiVersionSupported)throw mods::Error("Headset runtime does not support the renderer's Vulkan version.");
    config.apiVersion=VK_MAKE_VERSION(XR_VERSION_MAJOR(version),XR_VERSION_MINOR(version),0);
    config.selectPhysicalDevice=[this,generation](VkInstance instance) {
        auto& p=*impl_;VkPhysicalDevice device=VK_NULL_HANDLE;
        if(!p.instance||p.lost||p.graphics_generation!=generation)return device;
        XrResult result;
        if(p.enable2) {
            XrVulkanGraphicsDeviceGetInfoKHR info{XR_TYPE_VULKAN_GRAPHICS_DEVICE_GET_INFO_KHR};info.systemId=p.system;info.vulkanInstance=instance;
            result=p.physical_device2(p.instance,&info,&device);
        } else result=p.physical_device(p.instance,p.system,instance,&device);
        if(XR_FAILED(result)){p.note("GPU selection failed (OpenXR "+std::to_string(result)+").");return VkPhysicalDevice(VK_NULL_HANDLE);}
        p.selected_instance=instance;p.selected_device=device;return device;
    };
    return [config](plume::RenderWindow window)->std::unique_ptr<plume::RenderInterface> {
#if PLUME_SDL_VULKAN_ENABLED
        auto backend=std::make_unique<plume::VulkanInterface>(window,config);
#else
        (void)window;auto backend=std::make_unique<plume::VulkanInterface>(config);
#endif
        if(!backend->isValid())return {};
        return backend;
    };
}
bool XrSession::bind(plume::VulkanDevice& device,plume::VulkanCommandQueue& queue) noexcept {
    auto& p=*impl_;
    try {
        if(!p.instance||p.session||device.physicalDevice!=p.selected_device||device.renderInterface->instance!=p.selected_instance||queue.device!=&device||!queue.queue||!queue.queue->mutex)
            throw mods::Error("Renderer/headset graphics ownership does not match.");
        p.device=&device;p.queue=&queue;
        VkPhysicalDeviceProperties properties{};vkGetPhysicalDeviceProperties(device.physicalDevice,&properties);
        p.note("gpu="+std::string(properties.deviceName)+" queue-family="+std::to_string(queue.familyIndex)+" queue-index="+std::to_string(queue.queueIndex));
        XrGraphicsBindingVulkanKHR binding{XR_TYPE_GRAPHICS_BINDING_VULKAN_KHR};
        binding.instance=p.selected_instance;binding.physicalDevice=p.selected_device;binding.device=device.vk;
        binding.queueFamilyIndex=queue.familyIndex;binding.queueIndex=queue.queueIndex;
        XrSessionCreateInfo info{XR_TYPE_SESSION_CREATE_INFO};info.next=&binding;info.systemId=p.system;
        check(xrCreateSession(p.instance,&info,&p.session),"Create headset session");
        XrReferenceSpaceCreateInfo space{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};space.poseInReferenceSpace.orientation.w=1;
        space.referenceSpaceType=XR_REFERENCE_SPACE_TYPE_LOCAL;
        check(xrCreateReferenceSpace(p.session,&space,&p.base),"Create headset origin space");
        check(xrCreateReferenceSpace(p.session,&space,&p.local),"Create seated headset space");
        space.referenceSpaceType=XR_REFERENCE_SPACE_TYPE_VIEW;check(xrCreateReferenceSpace(p.session,&space,&p.view),"Create headset tracking space");
        std::uint32_t count=0;check(xrEnumerateViewConfigurationViews(p.instance,p.system,XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,0,&count,nullptr),"Read headset eye configuration");
        if(count!=2)throw mods::Error("This implementation requires a two-eye headset view configuration.");
        std::array<XrViewConfigurationView,2> views{{{XR_TYPE_VIEW_CONFIGURATION_VIEW},{XR_TYPE_VIEW_CONFIGURATION_VIEW}}};
        check(xrEnumerateViewConfigurationViews(p.instance,p.system,XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,2,&count,views.data()),"Read headset eye sizes");
        check(xrEnumerateSwapchainFormats(p.session,0,&count,nullptr),"Read headset image formats");
        if(count>256)throw mods::Error("Headset format count exceeds its limit.");
        std::vector<std::int64_t> formats(count);check(xrEnumerateSwapchainFormats(p.session,count,&count,formats.data()),"Read headset image formats");
        std::int64_t format=0;for(auto preferred:{VK_FORMAT_R8G8B8A8_UNORM,VK_FORMAT_B8G8R8A8_UNORM})
            if(std::find(formats.begin(),formats.end(),preferred)!=formats.end()){format=preferred;break;}
        if(!format)throw mods::Error("No supported headset colour format is available.");
        for(unsigned i=0;i<2;++i) {
            const auto w=views[i].recommendedImageRectWidth,h=views[i].recommendedImageRectHeight;
            if(!w||!h||w>8192||h>8192)throw mods::Error("Headset eye size exceeds the render budget.");
            p.create_chain(p.chains[i],w,h,format);
            p.note("eye="+std::to_string(i)+" size="+std::to_string(w)+"x"+std::to_string(h)+" format="+std::to_string(format));
        }
        p.create_chain(p.chains[2],1536,768,format);p.error.clear();return true;
    }catch(const std::exception& e){p.error=e.what();p.note("binding failed: "+p.error);shutdown();return false;}
}
bool XrSession::draw(const DkrViewPose& anchor,float scale,bool immersive,const RenderEye& render,const RenderPanel& panel) noexcept {
    auto& p=*impl_;if(!p.session||p.lost)return false;
    bool begun=false;XrTime frame_time=0;
    try {
        if(immersive&&!finite(anchor,scale))throw mods::Error("Invalid headset anchor or world scale.");
        p.poll();if(!p.active||p.lost)return false;
        XrFrameWaitInfo wait{XR_TYPE_FRAME_WAIT_INFO};XrFrameState frame{XR_TYPE_FRAME_STATE};
        check(xrWaitFrame(p.session,&wait,&frame),"Wait for headset display time");
        frame_time=frame.predictedDisplayTime;
        XrFrameBeginInfo begin{XR_TYPE_FRAME_BEGIN_INFO};check(xrBeginFrame(p.session,&begin),"Begin headset frame");begun=true;
        if(p.recenter_requested.load(std::memory_order_acquire)) {
            XrSpaceLocation head{XR_TYPE_SPACE_LOCATION};
            check(xrLocateSpace(p.view,p.base,frame_time,&head),"Locate seated headset origin");
            constexpr auto required=XR_SPACE_LOCATION_ORIENTATION_VALID_BIT|XR_SPACE_LOCATION_POSITION_VALID_BIT;
            if((head.locationFlags&required)==required&&valid_pose(head.pose)) {
                auto forward=rotate(head.pose.orientation,{0,0,-1});
                const float yaw=std::atan2(-forward.x,-forward.z)*0.5F;
                XrReferenceSpaceCreateInfo origin{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};origin.referenceSpaceType=XR_REFERENCE_SPACE_TYPE_LOCAL;
                origin.poseInReferenceSpace.position=head.pose.position;origin.poseInReferenceSpace.orientation={0,std::sin(yaw),0,std::cos(yaw)};
                XrSpace replacement=XR_NULL_HANDLE;check(xrCreateReferenceSpace(p.session,&origin,&replacement),"Recenter seated headset");
                xrDestroySpace(p.local);p.local=replacement;p.recenter_requested.store(false,std::memory_order_release);
            }
        }
        std::array<XrView,2> views{{{XR_TYPE_VIEW},{XR_TYPE_VIEW}}};XrViewState state{XR_TYPE_VIEW_STATE};
        XrViewLocateInfo locate{XR_TYPE_VIEW_LOCATE_INFO};locate.viewConfigurationType=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
        locate.displayTime=frame.predictedDisplayTime;locate.space=p.local;std::uint32_t count=0;
        check(xrLocateViews(p.session,&locate,&state,2,&count,views.data()),"Locate headset eyes");
        bool tracked=count==2&&(state.viewStateFlags&XR_VIEW_STATE_ORIENTATION_VALID_BIT)&&(state.viewStateFlags&XR_VIEW_STATE_POSITION_VALID_BIT);
        for(const auto& eye:views)tracked=tracked&&valid_pose(eye.pose)&&valid_fov(eye.fov);
        if(tracked!=p.tracking){p.tracking=tracked;p.note(tracked?"tracking-restored":"tracking-unavailable; no world layers submitted");}
        std::array<XrCompositionLayerProjectionView,2> projection_views{{{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW},{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW}}};
        XrCompositionLayerProjection world{XR_TYPE_COMPOSITION_LAYER_PROJECTION};world.space=p.local;world.viewCount=2;world.views=projection_views.data();
        XrCompositionLayerQuad quad{XR_TYPE_COMPOSITION_LAYER_QUAD};quad.space=p.local;quad.eyeVisibility=XR_EYE_VISIBILITY_BOTH;
        quad.pose.orientation.w=1;quad.pose.position={0,immersive?-0.32F:0,immersive?-0.95F:-2.0F};quad.size={immersive?0.9F:2.4F,immersive?0.45F:1.2F};
        quad.layerFlags=XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
        std::array<const XrCompositionLayerBaseHeader*,2> layers{};unsigned layer_count=0;
        bool world_ready=frame.shouldRender&&tracked&&immersive&&bool(render);
        if(world_ready) {
            auto base=rotation(anchor);
            for(unsigned i=0;i<2;++i) {
                auto& chain=p.chains[i];if(!p.acquire(chain)){world_ready=false;break;}
                XrEye eye;eye.eye=i;eye.width=chain.width;eye.height=chain.height;
                auto position=rotate(base,views[i].pose.position);auto orientation=multiply(base,views[i].pose.orientation);
                eye.position[0]=anchor.position[0]+position.x*scale;eye.position[1]=anchor.position[1]+position.y*scale;eye.position[2]=anchor.position[2]+position.z*scale;
                eye.orientation[0]=orientation.x;eye.orientation[1]=orientation.y;eye.orientation[2]=orientation.z;eye.orientation[3]=orientation.w;
                eye.left=views[i].fov.angleLeft;eye.right=views[i].fov.angleRight;eye.up=views[i].fov.angleUp;eye.down=views[i].fov.angleDown;
                eye.near_plane=anchor.near_plane;eye.far_plane=anchor.far_plane;
                bool rendered=render(eye,*chain.images[chain.index]);
                // Renderer callback contract: no outstanding GPU image use.
                p.release(chain);if(!rendered){world_ready=false;break;}
                projection_views[i].pose=views[i].pose;projection_views[i].fov=views[i].fov;
                projection_views[i].subImage.swapchain=chain.handle;projection_views[i].subImage.imageRect.extent={std::int32_t(chain.width),std::int32_t(chain.height)};
            }
        }
        if(world_ready)layers[layer_count++]=reinterpret_cast<const XrCompositionLayerBaseHeader*>(&world);
        if(frame.shouldRender&&tracked&&panel&&layer_count<p.max_layers) {
            auto& chain=p.chains[2];
            if(p.acquire(chain)) {
                bool ready=panel(*chain.images[chain.index],chain.width,chain.height,immersive);p.release(chain);
                if(ready) {
                    quad.subImage.swapchain=chain.handle;quad.subImage.imageRect.extent={std::int32_t(chain.width),std::int32_t(chain.height)};
                    layers[layer_count++]=reinterpret_cast<const XrCompositionLayerBaseHeader*>(&quad);
                }
            }
        }
        XrFrameEndInfo end{XR_TYPE_FRAME_END_INFO};end.displayTime=frame.predictedDisplayTime;
        end.environmentBlendMode=p.blend;end.layerCount=layer_count;end.layers=layers.data();
        // OpenXR and Plume share this exact native queue. Synchronize queue
        // consumption only here; never hold it during xrWaitFrame/rendering.
        begun=false; // An unsuccessful xrEndFrame must not be resubmitted.
        XrResult result;
        {std::scoped_lock lock(*p.queue->queue->mutex);result=xrEndFrame(p.session,&end);}
        check(result,"Submit headset frame");
        if(result==XR_SESSION_LOSS_PENDING){p.lost=true;p.note("session-loss-pending");return false;}
        p.error.clear();return layer_count!=0;
    }catch(const std::exception& e) {
        p.error=e.what();
        p.note("frame stopped: "+p.error);
        if(begun) {
            // No fabricated eye pose or stale world frame on tracking/error.
            XrFrameEndInfo end{XR_TYPE_FRAME_END_INFO};end.environmentBlendMode=p.blend;
            end.displayTime=frame_time;
            {std::scoped_lock lock(*p.queue->queue->mutex);xrEndFrame(p.session,&end);}
        }
        p.lost=true; // Owner recreates the session after its graphics drain.
        return false;
    }
}
bool XrSession::recenter() noexcept {
    impl_->recenter_requested.store(true,std::memory_order_release);
    return true; // Applied only at the next valid tracked display interval.
}
void XrSession::shutdown() noexcept {
    if(!impl_)return;
    auto& p=*impl_;
    // Caller must drain GPU consumers before this point, even after a frame
    // failure. The runtime owns swapchain images; wrappers own image views.
    for(auto& chain:p.chains){chain.images.clear();if(chain.handle)xrDestroySwapchain(chain.handle);chain=Impl::Chain{};}
    if(p.view)xrDestroySpace(p.view);
    if(p.local)xrDestroySpace(p.local);
    if(p.base)xrDestroySpace(p.base);
    if(p.session)xrDestroySession(p.session);
    if(p.instance)xrDestroyInstance(p.instance);
    p.view=p.local=p.base=XR_NULL_HANDLE;p.session=XR_NULL_HANDLE;p.instance=XR_NULL_HANDLE;p.system=XR_NULL_SYSTEM_ID;
    p.active=false;p.tracking=false;p.device=nullptr;p.queue=nullptr;p.state=XR_SESSION_STATE_UNKNOWN;
    p.selected_instance=VK_NULL_HANDLE;p.selected_device=VK_NULL_HANDLE;
    ++p.graphics_generation;
    p.recenter_requested.store(true,std::memory_order_release);
}
}
