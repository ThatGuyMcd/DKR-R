#pragma once
#include "policy.hpp"
namespace dkr::feedback {
inline std::array<unsigned char,65> deck_effect(unsigned side,std::uint16_t strength) {
    std::array<unsigned char,65> b{};if(side<1||side>2)return b;
    b[1]=0xEA;b[2]=19;b[3]=std::uint8_t(side);b[4]=strength?4:0;
    const int gain=strength?std::clamp(int(20*std::log10(float(strength)/65535.F)),-40,0):-40;
    b[6]=std::uint8_t(std::int8_t(gain));b[7]=std::uint8_t(side==1?140:220);b[9]=180;
    return b;
}
inline std::array<float,1920*4> waveform_block(Output o,std::uint64_t& phase) {
    std::array<float,1920*4> data{};
    for(unsigned i=0;i<1920;++i) {
        const double t=double(phase++%48000)/48000.;
        data[i*4+2]=float(std::sin(t*6.283185307179586*110))*.25F*o.left/65535.F;
        data[i*4+3]=float(std::sin(t*6.283185307179586*165))*.25F*o.right/65535.F;
    }return data;
}
}
