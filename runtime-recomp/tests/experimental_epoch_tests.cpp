#include "netplay/experimental_epoch.hpp"
#include "netplay/secure_channel.hpp"
#include "netplay/sequence_window.hpp"
#include <cassert>
#include <deque>
#include <iostream>

using namespace dkr::runtime::netplay;
using namespace dkr::runtime::netplay::experimental;
namespace {
void codec() {
    for(unsigned kind=0;kind<5;++kind) {
        EpochMessage m{EpochMessageKind(kind),4,90,kind?91U:0U,120,7,8,kind?9U:0U,kind>1?10U:0U};
        auto bytes=encode_epoch_message(m);
        assert(bytes.size()==60 && decode_epoch_message(bytes)==m);
        for(unsigned n=0;n<60;++n) assert(!decode_epoch_message(std::span(bytes).first(n)));
        bytes.push_back(0); assert(!decode_epoch_message(bytes)); bytes.pop_back();
        bytes[7]=1; assert(!decode_epoch_message(bytes)); bytes[7]=0;
        bytes[5]=5; assert(!decode_epoch_message(bytes));
    }
    EpochMessage invalid;
    assert(encode_epoch_message(invalid).empty());
}
void lost_release_ack() {
    EpochGate host,client;
    assert(host.begin(1,2,0,7) && client.begin(1,2,1,7));
    assert(host.confirm_boundary(3,11) && client.confirm_boundary(3,11));
    assert(host.receive_authenticated(1,*client.message_for(0))==EpochResult::Accepted);
    assert(host.propose(2,14));
    const auto prepare=*host.message_for(1);
    assert(client.receive_authenticated(0,prepare)==EpochResult::Accepted);
    assert(host.mark_prepared(99) && client.mark_prepared(99));
    assert(host.receive_authenticated(1,*client.message_for(0))==EpochResult::Accepted);
    const auto release=*host.message_for(1);
    assert(release.kind==EpochMessageKind::Release && !host.released());
    assert(client.receive_authenticated(0,release)==EpochResult::Accepted && client.released());
    // Lose the client's first ACK. The host cannot discard its release retry.
    assert(!host.begin(2,2,0,7));
    assert(client.begin(2,2,1,7));
    assert(client.receive_authenticated(0,release)==EpochResult::Duplicate);
    const auto repair=*client.previous_message_for(0);
    assert(repair.kind==EpochMessageKind::Acknowledged);
    assert(host.receive_authenticated(1,repair)==EpochResult::Accepted && host.released());
    assert(host.begin(2,2,0,7));
    assert(host.receive_authenticated(1,repair)==EpochResult::Duplicate);
    assert(!host.propose(3,25) && !client.can_prepare_scene());
}
void failures() {
    EpochGate host,client;
    assert(host.begin(5,2,0,7) && client.begin(5,2,1,7));
    assert(client.confirm_boundary(8,19));
    // A peer can reach the boundary first. Keep its vote bounded, but do not
    // unload anything or propose a scene before the host reaches that point.
    const auto vote=*client.message_for(0);
    assert(host.receive_authenticated(1,vote)==EpochResult::Accepted);
    assert(!host.all_boundaries_confirmed() && !host.propose(6,25) && !host.can_prepare_scene());
    assert(host.confirm_boundary(8,19) && host.propose(6,25));
    auto bad=*host.message_for(1);
    assert(host.receive_authenticated(1,bad)==EpochResult::Rejected); // Clients cannot propose.
    assert(client.receive_authenticated(1,bad)==EpochResult::Rejected); // No unauthenticated local echo.
    assert(client.receive_authenticated(0,bad)==EpochResult::Accepted);
    assert(!client.mark_prepared(0));
    assert(host.mark_prepared(100) && client.mark_prepared(101));
    assert(host.receive_authenticated(1,*client.message_for(0))==EpochResult::Conflict);
    assert(host.failed() && !host.released() && !host.message_for(1) && !host.begin(6,2,0,7));
    // Differing authenticated boundary votes must never release either scene.
    EpochGate other;
    assert(other.begin(5,2,0,7) && other.confirm_boundary(8,20));
    assert(other.receive_authenticated(1,vote)==EpochResult::Conflict && other.failed());
}
void distributed(unsigned players) {
    constexpr unsigned rounds=5;
    std::array<EpochGate,4> gates;
    std::array<unsigned,4> round{},began{},preparations{};
    std::array<std::array<secure::Key,4>,4> keys;
    std::array<std::array<std::uint64_t,4>,4> sequences{};
    // The production SecureChannel primitives authenticate each host-star
    // route. This harness does not attach a new lane to live DirectSession.
    std::array<std::array<ReceiveSequenceWindow,4>,4> replay;
    for(unsigned p=0;p<players;++p) {
        assert(gates[p].begin(100,players,p,7));
        for(unsigned q=p+1;q<players;++q) keys[p][q]=keys[q][p]=secure::generate_key();
    }
    struct Packet { unsigned source,destination,at; std::vector<std::uint8_t> bytes; };
    std::deque<Packet> packets;
    unsigned sends=0,rejected_tampering=0;
    for(unsigned time=0;time<1500;++time) {
        for(auto it=packets.begin();it!=packets.end();) {
            if(it->at>time) { ++it; continue; }
            std::uint64_t sender=0,sequence=0; std::vector<std::uint8_t> plain;
            if(secure::open(it->bytes,keys[it->source][it->destination],42,sender,sequence,plain)) {
                assert(sender==1000+it->source);
                if(replay[it->destination][it->source].accept(sequence)) {
                    const auto decoded=decode_epoch_message(plain); assert(decoded);
                    const auto result=gates[it->destination].receive_authenticated(it->source,*decoded);
                    assert(result!=EpochResult::Conflict && result!=EpochResult::Rejected);
                }
            } else ++rejected_tampering;
            it=packets.erase(it);
        }
        for(unsigned p=0;p<players;++p) {
            auto& gate=gates[p];
            if(round[p]<rounds && time-began[p]>=p*3+2)
                assert(gate.confirm_boundary(10+round[p],500+round[p]));
            if(!p && round[p]<rounds && gate.all_boundaries_confirmed())
                assert(gate.propose(101+round[p],700+round[p]));
            if(gate.can_prepare_scene() && time-began[p]>=p*7+15) {
                ++preparations[p]; assert(gate.mark_prepared(900+round[p]));
            }
            for(unsigned q=0;q<players;++q) {
                if(q==p || (p && q)) continue;
                for(const auto message:{gate.message_for(q),gate.previous_message_for(q)}) {
                    if(!message) continue;
                    auto bytes=secure::seal(encode_epoch_message(*message),keys[p][q],1000+p,++sequences[p][q],42);
                    assert(!bytes.empty());
                    if(++sends%5==0) continue; // 20% loss, including release/ACK repair.
                    packets.push_back({p,q,time+3+sends%5,bytes});
                    if(sends%7==0) packets.push_front({p,q,time+4+sends%5,bytes});
                    if(sends%11==0) { bytes.back()^=1; packets.push_back({p,q,time+2,bytes}); }
                }
            }
            if(gate.released() && round[p]<rounds) {
                ++round[p];
                if(round[p]<rounds) { assert(gate.begin(100+round[p],players,p,7)); began[p]=time; }
            }
        }
        bool done=true;
        for(unsigned p=0;p<players;++p) done&=round[p]==rounds;
        if(done) {
            for(unsigned p=0;p<players;++p) assert(preparations[p]==rounds && gates[p].released());
            assert(rejected_tampering>0);
            std::cout<<"Experimental epoch protocol: "<<players<<" peers, 5 consecutive scenes, 20% loss, authenticated/tampered/reordered control, exact-once preparation.\n";
            return;
        }
    }
    assert(false && "Epoch protocol did not release all peers");
}
}
int main() {
    codec(); lost_release_ack(); failures();
    for(unsigned players:{2,3,4}) distributed(players);
}
