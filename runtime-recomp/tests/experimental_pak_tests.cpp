#include "netplay/experimental_pak.hpp"
#include "netplay/experimental_rollback.hpp"
#include "netplay/replay_qualification.hpp"
#include <algorithm>
#include <cassert>
#include <cstring>
#include <iostream>

using namespace dkr::runtime::netplay;
using namespace dkr::runtime::netplay::experimental;
namespace {
Paks::Identity identity(unsigned n) {
    Paks::Identity id;id.company=0x3459;id.game=0x4E445945;id.name[0]=std::uint8_t(n+1);return id;
}
std::vector<std::uint8_t> snapshot(const Paks& paks) {
    std::vector<std::uint8_t> bytes(Paks::kCheckpointBytes);assert(paks.capture(bytes));return bytes;
}
void transactions() {
    Paks paks;const auto blank=Paks::blank_images();unsigned slot=99;std::uint32_t count=99;
    assert(!paks.start(0,15,blank) && !paks.start(1,16,blank) && !paks.start(1,15,std::span(blank).first(100)));
    auto bad=blank;bad[127]^=1;assert(!paks.start(1,15,bad));assert(paks.start(1,15,blank) && !paks.start(2,15,blank));
    const auto initial=snapshot(paks);
    assert(paks.allocate(0,identity(0),1024,slot)==Paks::OwnerFailure && slot==99);
    assert(paks.status(4)==Paks::NoPak && paks.num_files(4,count)==Paks::NoPak && count==99);
    assert(paks.begin_frame(0) && !paks.begin_frame(0));
    assert(!paks.capture(bad) && !paks.begin_epoch(2));
    assert(paks.allocate(0,identity(0),0,slot)==Paks::Invalid);
    assert(paks.allocate(0,identity(0),UINT32_MAX,slot)==Paks::DataFull);
    assert(paks.allocate(0,identity(0),1024,slot)==Paks::Ok && slot==0);
    assert(paks.allocate(0,identity(0),256,slot)==Paks::Exists);
    assert(paks.num_files(0,count)==Paks::Ok && count==1);
    std::uint32_t space;assert(paks.free_bytes(0,space)==Paks::Ok && space==31488-1024);
    Paks::FileState state;assert(paks.file_state(0,0,state)==Paks::Ok && state.identity==identity(0) && state.bytes==1024);
    const std::array<std::uint8_t,8> data{1,2,3,4,5,6,7,8};std::array<std::uint8_t,8> read{};
    assert(paks.write(0,0,1016,data)==Paks::Ok && paks.read(0,0,1016,read)==Paks::Ok && read==data);
    assert(paks.read(0,0,1017,read)==Paks::Invalid && paks.write(0,0,SIZE_MAX,data)==Paks::Invalid);
    assert(std::equal(blank.begin(),blank.end(),paks.confirmed_images().begin()));
    assert(paks.restore(initial) && paks.num_files(0,count)==Paks::Ok && count==0);
    assert(paks.begin_frame(0) && paks.allocate(0,identity(0),1024,slot)==Paks::Ok && paks.write(0,0,1016,data)==Paks::Ok);
    std::vector<std::uint8_t> journal;assert(paks.end_frame(journal) && !paks.end_frame(journal));
    const auto written=snapshot(paks);
    for(unsigned offset:{0,4,5,6,8,16,20,21,22,24,26,28}) {
        auto corrupt=journal;corrupt[offset]^=0xFF;
        assert(!paks.commit(1,0,corrupt) && std::equal(blank.begin(),blank.end(),paks.confirmed_images().begin()));
    }
    auto extended=journal;extended.push_back(0);
    assert(!paks.commit(1,0,extended) && !paks.commit(2,0,journal) && !paks.commit(1,1,journal));
    assert(!paks.commit(1,0,std::span(journal).first(journal.size()-1)));
    assert(paks.commit(1,0,journal) && !paks.commit(1,0,journal) && !paks.restore(initial));
    assert(paks.begin_epoch(2) && !paks.restore(written));
    assert(paks.begin_frame(0));
    for(unsigned i=1;i<16;++i)assert(paks.allocate(0,identity(i),1,slot)==Paks::Ok);
    assert(paks.allocate(0,identity(16),1,slot)==Paks::DirFull);
    assert(paks.erase(0,identity(0))==Paks::Ok && paks.erase(0,identity(0))==Paks::Invalid);
    assert(paks.end_frame(journal) && paks.commit(2,0,journal));
    assert(paks.begin_frame(1) && paks.reformat(0)==Paks::Ok && paks.end_frame(journal) && paks.commit(2,1,journal));
    assert(paks.num_files(0,count)==Paks::Ok && count==0);
    const auto stable=snapshot(paks);
    for(std::size_t offset:std::array<std::size_t,11>{0,4,5,6,8,16,20,32,32+16,32+127,Paks::kCheckpointBytes-1}) {
        auto corrupt=stable;corrupt[offset]^=0xFF;assert(!paks.restore(corrupt) && snapshot(paks)==stable);
    }
    assert(paks.begin_epoch(3) && paks.begin_frame(0));
    for(unsigned i=0;i<128;++i)assert(paks.reformat(0)==Paks::Ok);
    assert(paks.reformat(0)==Paks::OwnerFailure);
    assert(paks.end_frame(journal) && paks.commit(3,0,journal));
    assert(paks.begin_epoch(4) && paks.begin_frame(0));
    assert(paks.allocate(0,identity(0),31488,slot)==Paks::Ok && paks.allocate(0,identity(1),1,slot)==Paks::DataFull);
    std::vector<std::uint8_t> full(31488,0xAB);
    assert(paks.write(0,0,0,full)==Paks::Ok);
    // A second whole-image mutation would exceed the bounded journal. It must
    // fail BEFORE modifying working bytes, not silently truncate an effect.
    std::fill(full.begin(),full.end(),0xCD);assert(paks.write(0,0,0,full)==Paks::OwnerFailure);
    assert(paks.read(0,0,0,read)==Paks::Ok && read[0]==0xAB);
    assert(paks.end_frame(journal) && paks.commit(4,0,journal));
    Paks disabled;assert(disabled.start(1,0,blank) && disabled.status(0)==Paks::NoPak);
    assert(disabled.begin_frame(0) && disabled.reformat(0)==Paks::NoPak && disabled.end_frame(journal) && disabled.commit(1,0,journal));
}
class PakWorld final : public Simulation {
public:
    Paks paks;std::array<std::uint32_t,4> counters{};unsigned deliveries=0;
    PakWorld() { assert(paks.start(1,15,Paks::blank_images())); }
    SimulationContract contract() const override { return {0x50414B4F574E4552ULL,Paks::kCheckpointBytes+sizeof(counters),kRequiredStateDomains,true,true,true}; }
    bool capture(std::span<std::uint8_t> bytes,std::string&) override {
        if(bytes.size()!=contract().state_bytes || !paks.capture(bytes.subspan(sizeof(counters))))return false;
        std::memcpy(bytes.data(),counters.data(),sizeof(counters));return true;
    }
    bool restore(std::span<const std::uint8_t> bytes,std::string&) override {
        if(bytes.size()!=contract().state_bytes || !paks.restore(bytes.subspan(sizeof(counters))))return false;
        std::memcpy(counters.data(),bytes.data(),sizeof(counters));return true;
    }
    bool tick(std::uint32_t frame,const FrameInputs& inputs,TickOutput& output,std::string&) override {
        if(!paks.begin_frame(frame))return false;
        for(unsigned owner=0;owner<4;++owner) {
            unsigned index=0;std::array<std::uint8_t,8> bytes{};
            if(!frame && paks.allocate(owner,identity(owner),1024,index)!=Paks::Ok)return false;
            const unsigned offset=((frame*7+owner)%128)*8;
            if(paks.read(owner,0,offset,bytes)!=Paks::Ok)return false;
            counters[owner]+=bytes[0]+inputs[owner].buttons;
            bytes[0]=std::uint8_t(inputs[owner].buttons);bytes[1]=std::uint8_t(frame);
            if(paks.write(owner,0,offset,bytes)!=Paks::Ok)return false;
        }output={};return paks.end_frame(output.effects);
    }
    bool commit(std::uint64_t epoch,std::uint32_t frame,std::span<const std::uint8_t> effects,bool boundary,std::string&) override {
        if(boundary || !paks.commit(epoch,frame,effects))return false;++deliveries;return true;
    }
};
FrameInputs input(unsigned frame) {
    FrameInputs result{};for(unsigned p=0;p<4;++p)result[p].buttons=std::uint16_t(frame*7+p+1);return result;
}
void correction() {
    PakWorld qualification;std::array<FrameInputs,12> sequence;
    for(unsigned f=0;f<12;++f)sequence[f]=input(f);
    assert(qualify_replay(qualification,sequence).passed() && !qualification.deliveries);
    PakWorld reference,corrected;Driver driver(corrected);std::string error;
    assert(driver.start({1,4,6,128*1024*1024},error));
    for(unsigned f=0;f<150;++f) {
        TickOutput output;assert(reference.tick(f,input(f),output,error) && reference.commit(1,f,output.effects,false,error));
        assert(driver.receive(1,0,f,input(f)[0])==InputResult::Accepted);
        if(f>=4)for(unsigned p=1;p<4;++p)assert(driver.receive(1,p,f-4,input(f-4)[p])==InputResult::Accepted);
        for(unsigned budget=0;driver.statistics().next_frame<=f && budget<20;++budget)assert(driver.step()!=Step::Failed);
        assert(driver.statistics().next_frame==f+1);
    }
    for(unsigned f=146;f<150;++f)for(unsigned p=1;p<4;++p)assert(driver.receive(1,p,f,input(f)[p])==InputResult::Accepted);
    for(unsigned budget=0;driver.statistics().confirmed_frames<150 && budget<100;++budget)assert(driver.step(false)!=Step::Failed);
    assert(driver.statistics().confirmed_frames==150 && driver.statistics().rollbacks>0);
    std::vector<std::uint8_t> a(reference.contract().state_bytes),b(a.size());
    assert(reference.capture(a,error) && corrected.capture(b,error) && a==b && reference.deliveries==corrected.deliveries);
    assert(std::equal(reference.paks.confirmed_images().begin(),reference.paks.confirmed_images().end(),corrected.paks.confirmed_images().begin()));
}
}
int main() { transactions();correction();std::cout<<"Experimental Pak transactions: bounded atomic images, corrupt/duplicate refusal, 78 suffix replays and 150 corrected four-owner frames passed. No file/native Pak access.\n"; }
