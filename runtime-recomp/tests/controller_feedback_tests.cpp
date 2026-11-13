#include "controller_feedback/settings.hpp"
#include "controller_feedback/race_details.hpp"
#include "controller_feedback/signals.hpp"
#include "controller_feedback/input_protocol.hpp"
#include <cassert>
#include <limits>
#include <sstream>
#include <iostream>

using namespace dkr::feedback;
int main() {
    assert(racing_owner(false,255,0,3));assert(racing_owner(true,2,1,1));
    assert(!racing_owner(true,255,0,0));assert(!racing_owner(true,2,1,2));
    const auto deck=deck_effect(1,65535);assert(deck[1]==0xEA&&deck[2]==19&&deck[3]==1&&deck[4]==4&&deck[6]==0&&deck[9]==180);
    assert(deck_effect(2,0)[4]==0);assert((deck_effect(3,1)==std::array<unsigned char,65>{}));
    std::uint64_t phase=0;Output full;full.left=full.right=65535;const auto pcm=waveform_block(full,phase);
    assert(phase==1920);for(unsigned i=0;i<1920;++i){assert(pcm[i*4]==0&&pcm[i*4+1]==0);assert(std::abs(pcm[i*4+2])<=.25F&&std::abs(pcm[i*4+3])<=.25F);}
    Settings s;s.mode=2;s.strength=500;s.balance=-500;s.resistance=20;
    s=bounded(s);assert(s.strength==100&&s.balance==-100&&s.resistance==3);
    Output o;o.trigger_mask=3;o.resistance=3;o.led_mode=1;o.brightness=50;o.red=200;o.left=65535;o.right=32768;
    const auto bytes=dualsense_effect(o);
    assert(bytes[10]==0x21&&bytes[21]==0x21&&bytes[2]==128&&bytes[3]==255);
    assert(bytes[38]==4&&bytes[44]==100&&bytes[8]==0&&bytes[9]==0); // never touch mute/audio
    auto off=dualsense_effect({});assert(off[10]==0&&off[21]==0&&off[2]==0&&off[3]==0&&off[1]==0);
    assert(dualsense_effect({},true,true)[1]==0x18);
    o.resistance=255;assert(!valid(o));assert((dualsense_effect(o)==std::array<std::uint8_t,47>{}));
    o={};o.left=65535;assert(dualsense_effect(o,false)[3]==127);o.waveform=1;assert((dualsense_effect(o)[0]&3)==0);
    Mixer mixer;s={};s.mode=2;
    Sample sample{};sample.driving=true;sample.scene=1;sample.frame=1;sample.generation=1;
    assert(!mixer.observe(sample,1));sample.frame=2;sample.impact=true;assert(mixer.observe(sample,1));
    assert(mixer.mix(s,1.02,false,true).left>0);
    assert(!mixer.observe(sample,1.03));sample.frame=1;assert(!mixer.observe(sample,1.04));
    assert(mixer.mix(s,2,false,true).left==0);assert(mixer.mix(s,1.02,false,false).left==0);
    sample.scene=2;sample.frame=0;assert(!mixer.observe(sample,3));assert(mixer.mix(s,3.02,false,true).left==0);
    mixer.pulse(Cue::Impact,4,std::numeric_limits<float>::quiet_NaN());assert(mixer.mix(s,4.01,false,true).left==0);
    s.mode=1;assert(mixer.mix(s,5,true,true).left==65535);s.balance=100;assert(mixer.mix(s,5,true,true).left==0);
    s.mode=0;assert(mixer.mix(s,5,true,true)==Output{});
    using namespace dkr::runtime::sdl3_input::protocol;
    std::array<std::uint8_t,kTokenBytes> token{};FeedbackPacket packet{};initialise(packet,PacketType::Feedback,token);
    assert(valid_header(packet.header,PacketType::Feedback,sizeof(packet),token));packet.header.version=1;
    assert(!valid_header(packet.header,PacketType::Feedback,sizeof(packet),token));
    assert(dkr::runtime::controllers::kSnapshotButtonCount==26&&dkr::runtime::controllers::kSnapshotAxisCount==6);
    for(const auto revision:{dkr::runtime::rom::Revision::UsV77,dkr::runtime::rom::Revision::UsV80}) {
        std::vector<std::uint8_t> ram(0x800000);
        const auto& a=revision==dkr::runtime::rom::Revision::UsV77?dkr::runtime::revision_addresses::kUsV77:dkr::runtime::revision_addresses::kUsV80;
        const auto word=[&](std::uint32_t p,std::uint32_t v){std::memcpy(ram.data()+p-0x80000000U,&v,4);};
        const auto byte=[&](std::uint32_t p,std::uint8_t v){ram[(p-0x80000000U)^3U]=v;};
        word(a.NumberOfRacers,1);word(a.RacersByPort,0x80060000);word(0x80060000,0x80040000);word(0x80040064,0x80050000);
        byte(0x80050185,9);byte(0x80050173,3);byte(0x800501DC,13);byte(0x800501E2,4);word(a.RaceStartTimer,60);
        Sample read;race_details(ram,revision,0,read);assert(read.bananas==9&&read.ammunition==3&&read.surface==13&&read.grounded&&read.countdown==60);
        word(a.NumberOfRacers,99);read={};race_details(ram,revision,0,read);assert(read.surface==255);
        word(a.NumberOfRacers,1);word(0x80040064,0xFFFFFFFC);read={};race_details(ram,revision,0,read);assert(read.bananas==0);
    }
    assert(load_setting("feedback_p2_k1_strength",80));assert(service().stored(1,1).strength==80);
    assert(service().stored(1,2).strength==100);assert(!load_setting("feedback_p9_k1_strength",80));
    assert(load_setting("feedback_p2_k1_waveform",1));assert(service().stored(1,1).waveform==0);
    std::ostringstream saved;save_settings(saved);assert(saved.str().find("feedback_p2_k1_strength=80")!=std::string::npos);
    // Actual worker, bounded output lifetime and duplicate physical ownership.
    Service worker;std::mutex mutex;std::vector<std::pair<int,Output>> writes;
    worker.sink([&](const Device& d,Output v){std::lock_guard l(mutex);writes.emplace_back(d.instance,v);return true;});
    Device device{};device.instance=42;device.allowed=true;device.capabilities=Rumble;
    // The worker writes from its own thread, so poll for each state instead of trusting one fixed sleep on a loaded runner.
    const auto eventually=[&](auto&& reached){for(int i=0;i<400;++i){{std::lock_guard l(mutex);if(reached())return true;}std::this_thread::sleep_for(std::chrono::milliseconds(5));}return false;};
    worker.input(0,device);worker.input(1,device);worker.test(0,1,100);
    assert(eventually([&]{return !writes.empty()&&writes.back().second.left==65535&&writes.back().second.right==0;}));
    worker.stop(0);
    assert(eventually([&]{return writes.back().second==Output{};}));
    worker.test(0,0,100);std::this_thread::sleep_for(std::chrono::milliseconds(520));
    assert(eventually([&]{return writes.back().second==Output{};}));
    worker.shutdown();
    // A confirmed frame is consumed once; replaying it cannot create another pulse.
    Service confirmed(false);confirmed.input(0,device);s={};s.mode=2;confirmed.settings(0,s);
    sample={};sample.driving=true;sample.scene=5;sample.epoch=10;sample.frame=1;sample.generation=1;
    confirmed.sample(0,sample,1,true,1);sample.frame=2;sample.impact=true;confirmed.sample(0,sample,1,true,1.1);
    assert(confirmed.mixed(0,1.12).left==0);
    sample.frame=3;confirmed.sample(0,sample,2,true,1.2);
    assert(confirmed.mixed(0,1.22).left>0);
    sample.frame=2;confirmed.sample(0,sample,3,true,1.6);
    assert(confirmed.mixed(0,1.62).left==0);
    confirmed.shutdown();
    Service reconnect(false);reconnect.input(0,device);s={};s.waveform=1;reconnect.settings(0,s);
    assert(reconnect.status(0).settings.waveform==1);device.generation=2;reconnect.input(0,device);
    assert(reconnect.status(0).settings.waveform==0);
    std::cout<<"Feedback bounds, typed reports, migration, confirmation, ownership and watchdog passed\n";
}
