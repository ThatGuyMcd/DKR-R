#pragma once
#include <atomic>
#include <cstdint>

// Presentation-only observation ABI. The renderer borrows TMEM only during
// this call. It never holds a game pointer, runs a guest callback or does I/O.
namespace dkr::texture_capture {
struct Tile {
    std::uint64_t native_hash=0, rice_alias=0;
    std::uint32_t width=0,height=0,tlut=0,fmt=0,siz=0,line=0,tmem=0,palette=0;
    std::uint32_t cms=0,cmt=0,masks=0,maskt=0,shifts=0,shiftt=0;
    std::uint32_t uls=0,ult=0,lrs=0,lrt=0;
    char rice_name[64]{};
};
using Observer=void(*)(const Tile&,const std::uint8_t*) noexcept;
using Gate=bool(*)(std::uint64_t) noexcept;
inline std::atomic<Observer> observer{nullptr};
inline std::atomic<Gate> gate{nullptr};
}
