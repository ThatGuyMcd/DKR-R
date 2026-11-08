#pragma once
#include "overlay_lifetime.hpp"
// C++17 renderer seam: observe the existing completed present-worker fence.
namespace dkr::store::overlay {
inline Lifetime timing;
inline void completed() {timing.finish();}
}
