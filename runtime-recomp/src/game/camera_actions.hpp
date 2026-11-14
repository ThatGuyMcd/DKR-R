#pragma once
#include "camera_presentation.hpp"
#include <array>
#include <mutex>

namespace dkr::runtime::camera {
// Normalized camera actions, after the existing host resolves device bindings.
// Never opens controllers or writes the N64 input broker.
struct ActionSample {
    std::uint32_t held=0;
    float orbit_x=0,orbit_y=0,zoom=0;
    bool blocked=true,connected=false;
};
class Actions final {
public:
    void publish(unsigned physical_profile,const ActionSample&);
    void mouse(unsigned physical_profile,float x_pixels,float y_pixels);
    DkrViewInput consume(unsigned physical_profile,float dt);
    void reset();
private:
    struct Port {
        ActionSample latest;
        std::uint32_t pressed=0,suppressed=0;
        float mouse_x=0,mouse_y=0;
        bool neutral=true;
    };
    std::mutex mutex_;
    std::array<Port,4> ports_;
};
}
