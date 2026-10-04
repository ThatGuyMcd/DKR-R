#include "netplay/experimental_eeprom.hpp"
#include "netplay/experimental_rollback.hpp"
#include "netplay/replay_qualification.hpp"
#include <algorithm>
#include <cassert>
#include <cstring>
#include <iostream>

using namespace dkr::runtime::netplay;
using namespace dkr::runtime::netplay::experimental;
namespace {
std::array<std::uint8_t,512> seed() {
    std::array<std::uint8_t,512> image{};
    for(unsigned b=0;b<image.size();++b) image[b]=std::uint8_t(b*19); return image;
}
void transactions() {
    Eeprom save; const auto initial=seed();
    assert(!save.start(0,initial) && !save.start(1,std::span(initial).first(511)) && save.start(1,initial));
    assert(!save.start(2,initial));
    std::array<std::uint8_t,Eeprom::kCheckpointBytes> before{},after{};
    assert(save.capture(before));
    assert(save.begin_frame(0) && !save.begin_frame(0));
    assert(!save.capture(after));
    const std::array<std::uint8_t,8> data{1,2,3,4,5,6,7,8};
    assert(!save.write(1,data) && !save.write(512,data) && !save.write(SIZE_MAX,data));
    assert(save.write(504,data));
    std::array<std::uint8_t,8> read{}; assert(save.read(504,read) && read==data);
    assert(std::equal(save.confirmed_image().begin(),save.confirmed_image().end(),initial.begin()));
    assert(!save.begin_epoch(2));
    // A failed tick's open write is recoverable without touching confirmation.
    assert(save.restore(before)); assert(save.read(504,read) && read!=data);
    assert(save.begin_frame(0) && save.write(504,data));
    std::vector<std::uint8_t> journal; assert(save.end_frame(journal) && !save.end_frame(journal));
    assert(save.capture(after));
    for(unsigned corruption:{0,4,5,6,8,16,20,22}) {
        auto bad=journal; bad[corruption]^=0xFF; assert(!save.commit(1,0,bad));
        assert(std::equal(save.confirmed_image().begin(),save.confirmed_image().end(),initial.begin()));
    }
    auto extra=journal; extra.push_back(0); assert(!save.commit(1,0,extra));
    assert(!save.commit(1,0,std::span(journal).first(journal.size()-1)) && !save.commit(2,0,journal));
    assert(save.commit(1,0,journal) && !save.commit(1,0,journal));
    assert(!save.restore(before)); // no rewind behind a confirmed disk intent
    assert(std::equal(data.begin(),data.end(),save.confirmed_image().begin()+504));
    auto wrong_epoch=after; wrong_epoch[0]=2; assert(!save.restore(wrong_epoch));
    assert(save.begin_epoch(2) && !save.restore(after));
    assert(save.begin_frame(0));
    for(unsigned i=0;i<128;++i) assert(save.write(0,data));
    assert(!save.write(0,data)); // bounded operation count
    assert(save.end_frame(journal) && save.commit(2,0,journal));
    assert(save.begin_epoch(3));
    assert(save.begin_frame(0) && save.end_frame(journal));
    assert(!save.begin_epoch(4)); // even an identical-byte speculative tail must drain
    assert(save.commit(3,0,journal) && save.begin_epoch(4));
}
struct SaveWorld final : Simulation {
    Eeprom save;
    std::array<std::uint32_t,4> counters{};
    unsigned external_deliveries=0;
    SaveWorld() { assert(save.start(1,seed())); }
    SimulationContract contract() const override { return {0x53415645, sizeof(counters)+Eeprom::kCheckpointBytes,kRequiredStateDomains,true,true,true}; }
    bool capture(std::span<std::uint8_t> bytes,std::string&) override {
        if(bytes.size()!=contract().state_bytes || !save.capture(bytes.subspan(sizeof(counters)))) return false;
        std::memcpy(bytes.data(),counters.data(),sizeof(counters)); return true;
    }
    bool restore(std::span<const std::uint8_t> bytes,std::string&) override {
        if(bytes.size()!=contract().state_bytes || !save.restore(bytes.subspan(sizeof(counters)))) return false;
        std::memcpy(counters.data(),bytes.data(),sizeof(counters)); return true;
    }
    bool tick(std::uint32_t frame,const FrameInputs& input,TickOutput& output,std::string&) override {
        if(!save.begin_frame(frame)) return false;
        for(unsigned owner=0;owner<4;++owner) {
            const unsigned offset=((frame+owner*7)%64)*8;
            std::array<std::uint8_t,8> bytes{};
            if(!save.read(offset,bytes)) return false;
            counters[owner]+=bytes[0]+input[owner].buttons;
            bytes[0]=std::uint8_t(input[owner].buttons); bytes[1]=std::uint8_t(frame);
            if(input[owner].buttons && !save.write(offset,bytes)) return false;
        }
        output={}; return save.end_frame(output.effects);
    }
    bool commit(std::uint64_t epoch,std::uint32_t frame,std::span<const std::uint8_t> effects,bool boundary,std::string&) override {
        if(boundary || !save.commit(epoch,frame,effects)) return false;
        ++external_deliveries; return true;
    }
};
FrameInputs input(unsigned frame) {
    FrameInputs result{}; for(unsigned p=0;p<4;++p) result[p].buttons=std::uint16_t(frame*7+p+1); return result;
}
void corrected_saves() {
    // Model logic for EEPROM reads/writes, not a real DKR EEPROM import test.
    SaveWorld qualification; std::array<FrameInputs,12> sequence{};
    for(unsigned f=0;f<sequence.size();++f) sequence[f]=input(f);
    assert(qualify_replay(qualification,sequence).passed());
    assert(!qualification.external_deliveries);
    SaveWorld reference,corrected; Driver driver(corrected); std::string error;
    assert(driver.start({1,4,6},error));
    for(unsigned f=0;f<150;++f) {
        TickOutput out; assert(reference.tick(f,input(f),out,error) && reference.commit(1,f,out.effects,false,error));
    }
    for(unsigned time=0;time<155;++time) {
        if(time<150) assert(driver.receive(1,0,time,input(time)[0])==InputResult::Accepted);
        if(time>=5) for(unsigned p=1;p<4;++p) assert(driver.receive(1,p,time-5,input(time-5)[p])==InputResult::Accepted);
        for(unsigned n=0;n<6;++n) {
            const auto step=driver.step(driver.statistics().next_frame<150);
            assert(step!=Step::Failed); if(step!=Step::Replayed) break;
        }
    }
    assert(driver.statistics().confirmed_frames==150 && driver.statistics().rollbacks && corrected.external_deliveries==150);
    assert(corrected.counters==reference.counters &&
           std::equal(corrected.save.confirmed_image().begin(),corrected.save.confirmed_image().end(),reference.save.confirmed_image().begin()));
}
}
int main() { transactions(); corrected_saves(); std::cout<<"Owned experimental EEPROM: reversible writes, atomic confirmed journal, exactly-once model commits passed; no live save integration.\n"; }
