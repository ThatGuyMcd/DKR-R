#include "recomp.h"
#include <cstddef>
#include <cstdint>

// Offline adapter regression tests intentionally have no live online owner.
// Actual frozen routing/checkpoint coverage lives in NativeModWorldTests.
namespace dkr::runtime::legacy {
bool frozen_online_resources(){return false;}
bool frozen_course_hook(const char*,std::uint8_t*,recomp_context*){return false;}
bool frozen_music_hook(const char*,std::uint8_t*,recomp_context*){return false;}
bool frozen_music_render(float*,std::size_t,unsigned){return false;}
}
