#pragma once
#include "camera_presentation.hpp"
#include "plume_render_interface.h"
#include <memory>

namespace plume {struct VulkanDevice;struct VulkanCommandQueue;}
namespace dkr::runtime::camera {
struct XrEye {
    unsigned eye=0,width=0,height=0;
    float position[3]{},orientation[4]{0,0,0,1}; // World units, quaternion XYZW.
    float left=0,right=0,up=0,down=0; // Asymmetric angular FOV, radians.
    float near_plane=1,far_plane=30000;
};
// Graphics/presentation-thread owner. It never reads simulation RAM, waits on
// a network mutex, polls controllers or drives race cadence. The caller must
// drain graphics work before destroying this owner or its associated device.
class XrSession final {
public:
    using Factory=std::function<std::unique_ptr<plume::RenderInterface>(plume::RenderWindow)>;
    // Each callback must finish GPU use before returning true. Eye images must
    // be left in Vulkan COLOR_ATTACHMENT_OPTIMAL for the OpenXR compositor.
    using RenderEye=std::function<bool(const XrEye&,plume::RenderTexture&)>;
    using RenderPanel=std::function<bool(plume::RenderTexture&,unsigned,unsigned,bool)>;
    // No account names/serial numbers. A supplied logger must not block.
    using Log=std::function<void(const std::string&)>;
    explicit XrSession(Log log={});~XrSession();
    XrSession(const XrSession&)=delete;XrSession& operator=(const XrSession&)=delete;
    bool discover() noexcept;
    Factory graphics_factory();
    bool bind(plume::VulkanDevice&,plume::VulkanCommandQueue&) noexcept;
    bool draw(const DkrViewPose& anchor,float units_per_metre,bool immersive,
        const RenderEye&,const RenderPanel&) noexcept;
    bool recenter() noexcept;
    bool running()const;
    const std::string& error()const;
    void shutdown() noexcept;
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
}
