#pragma once
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <map>

namespace dkr::online_stability::flow {
using Clock=std::chrono::steady_clock;
struct Owner {std::uint32_t have=0,offered=0,repair=0;std::array<std::uint32_t,4> ack{},sent{};};
struct Event {std::uint32_t frame=0;std::uint8_t peer=0,owner=0,kind=0;std::uint64_t local_ms=0;};
struct State {
    std::uint64_t match=0,epoch=0,received=0,duplicates=0,blocked=0;
    unsigned local=0,events=0;Clock::time_point report{},started{},now{};
    std::array<Owner,4> owners{};std::array<Event,64> recent{};
    void event(unsigned peer,unsigned owner,unsigned kind,std::uint32_t frame){const auto ms=now>=started?std::chrono::duration_cast<std::chrono::milliseconds>(now-started).count():0;recent[events++%recent.size()]={frame,std::uint8_t(peer),std::uint8_t(owner),std::uint8_t(kind),std::uint64_t(ms)};}
};
inline thread_local std::map<const void*,State> states;
inline State& state(const void* key){if(!states.contains(key)&&states.size()>=8)states.erase(states.begin());return states[key];}
inline void forget(const void* key){states.erase(key);}
inline void pulse(const void* key,std::uint64_t match,std::uint64_t epoch,unsigned local,Clock::time_point now){
    auto& s=state(key);if(s.match!=match||s.epoch!=epoch){s={};s.match=match;s.epoch=epoch;s.local=local;s.report=now+std::chrono::seconds(5);s.started=now;}s.now=now;
    if(now<s.report)return;s.report=now+std::chrono::seconds(5);
    for(unsigned o=0;o<4;++o){const auto& p=s.owners[o];
        std::fprintf(stderr,"[rollback][owner-flow] local=%u epoch=%llu owner=%u have=%u offered=%u repair=%u ack=%u/%u/%u/%u sent=%u/%u/%u/%u received=%llu duplicate=%llu blocked=%llu\n",local,
            static_cast<unsigned long long>(epoch),o,p.have,p.offered,p.repair,p.ack[0],p.ack[1],p.ack[2],p.ack[3],p.sent[0],p.sent[1],p.sent[2],p.sent[3],
            static_cast<unsigned long long>(s.received),static_cast<unsigned long long>(s.duplicates),static_cast<unsigned long long>(s.blocked));}
}
template<class Packet>void offer(const void* key,unsigned peer,const Packet& p,bool repair){
    auto& s=state(key);if(p.owner>=4||peer>=4)return;
    for(unsigned o=0;o<4;++o)s.owners[o].have=std::max(s.owners[o].have,p.received_next[o]);
    auto& owner=s.owners[p.owner];owner.offered=std::max(owner.offered,p.first_frame+p.count);if(repair&&p.count)owner.repair=p.first_frame;
    s.event(peer,p.owner,repair?2:1,p.first_frame);
}
template<class Packet>void received(const void* key,unsigned peer,const Packet& p,bool duplicate){
    auto& s=state(key);if(peer>=4||p.owner>=4)return;++s.received;s.duplicates+=duplicate;
    for(unsigned o=0;o<4;++o)s.owners[o].ack[peer]=std::max(s.owners[o].ack[peer],p.received_next[o]);
    s.event(peer,p.owner,3,p.first_frame);
}
template<class Packet>void sent(const void* key,unsigned peer,const Packet& p,bool accepted){
    auto& s=state(key);if(peer>=4||p.owner>=4)return;
    if(accepted)s.owners[p.owner].sent[peer]=std::max(s.owners[p.owner].sent[peer],p.first_frame+p.count);else ++s.blocked;
    s.event(peer,p.owner,accepted?4:5,p.first_frame);
}
inline void dump(const void* key){const auto i=states.find(key);if(i==states.end())return;const auto& s=i->second;
    const auto begin=s.events>64?s.events-64:0;
    for(auto n=begin;n<s.events;++n){const auto& e=s.recent[n%64];std::fprintf(stderr,"[rollback][input-flight] local=%u epoch=%llu peer=%u owner=%u kind=%u first=%u local-ms=%llu\n",s.local,static_cast<unsigned long long>(s.epoch),e.peer,e.owner,e.kind,e.frame,static_cast<unsigned long long>(e.local_ms));}}
}
