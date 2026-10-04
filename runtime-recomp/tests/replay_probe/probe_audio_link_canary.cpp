#include <cstdint>
#include <algorithm>
// No private headers here. These are original stable DSP link symbols in a
// distinct translation unit, not an implementation of the experimental DSP.
enum class RspExitReason {Invalid,Broke,ImemOverrun,UnhandledJumpTarget,Unsupported,SwapOverlay,UnhandledResumeTarget};
std::uint8_t dmem[4096];
std::uint16_t rspReciprocals[512],rspInverseSquareRoots[512];
static unsigned calls;
RspExitReason dkrAspMain(std::uint8_t*,std::uint32_t) {++calls;return RspExitReason::Unsupported;}
extern "C" int dkr_probe_audio_link_canaries_begin() {
    std::fill_n(dmem,4096,0x5a);std::fill_n(rspReciprocals,512,0x1234);std::fill_n(rspInverseSquareRoots,512,0x5678);
    calls=0;return dkrAspMain(nullptr,0)==RspExitReason::Unsupported && calls==1;
}
extern "C" int dkr_probe_audio_link_canaries_unchanged() {
    return calls==1 && std::all_of(dmem,dmem+4096,[](auto b){return b==0x5a;}) &&
        std::all_of(rspReciprocals,rspReciprocals+512,[](auto b){return b==0x1234;}) &&
        std::all_of(rspInverseSquareRoots,rspInverseSquareRoots+512,[](auto b){return b==0x5678;});
}
