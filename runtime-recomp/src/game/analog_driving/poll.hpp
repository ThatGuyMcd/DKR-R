#pragma once
#include "settings.hpp"
namespace dkr::runtime::driving {
struct Polled {Pedals pedals{};bool accelerate=false,braking=false;};
inline std::array<bool,4> accelerator_held{},brake_held{};
inline void clear(unsigned player) {
    if(player>=4)return;accelerator_held[player]=brake_held[player]=false;
    preview[player].store(0);
}
inline Polled poll_calibrated(unsigned player,float accelerator,float brake,float digital_a,float digital_b) {
    Polled p;if(player>=4)return p;const auto settings=profile(player);
    if(!settings.enabled){clear(player);return p;}
    if(digital_a>.5F)accelerator=1;
    if(digital_b>.5F)brake=1;
    p.accelerate=accelerator_held[player]=held(accelerator,accelerator_held[player]);
    p.braking=brake_held[player]=held(brake,brake_held[player]);
    p.pedals={quantize(p.accelerate?accelerator:0),quantize(p.braking?brake:0),Enabled};
    preview[player].store(pack(p.pedals));return p;
}
inline Polled poll(unsigned player,float accelerator,float brake,float digital_a,float digital_b,bool connected=true) {
    const auto p=profile(player);
    return poll_calibrated(player,connected?calibrate(accelerator,p.accelerator):0,
        connected?calibrate(brake,p.brake):0,digital_a,digital_b);
}
}
