#include "netplay/experimental_owner_inputs.hpp"
#include <algorithm>
#include <cassert>
#include <deque>
#include <iostream>

using namespace dkr::runtime::netplay;
using namespace dkr::runtime::netplay::experimental;
namespace {
PackedInput sample(unsigned owner, unsigned frame) {
    return {std::uint16_t((frame * 17 + owner * 3) & 0xFFFF),
            std::int8_t(int(frame % 161) - 80), std::int8_t(int((frame + owner) % 161) - 80)};
}
void codec_tests() {
    OwnerInputPacket p;
    p.epoch = 0x123456789ABCDEF0ULL; p.players = 4; p.owner = 3; p.count = 8; p.first_frame = 41;
    p.received_next = {2,3,5,7};
    for (unsigned i = 0; i < 8; ++i) p.inputs[i] = sample(3, i);
    const auto bytes = encode_owner_inputs(p);
    assert(bytes.size() == kOwnerInputMaximumBytes && bytes[8] == 0xF0 && bytes[15] == 0x12);
    const auto decoded = decode_owner_inputs(bytes);
    assert(decoded && decoded->epoch == p.epoch && decoded->inputs == p.inputs && decoded->received_next == p.received_next);
    for (unsigned length = 0; length < bytes.size(); ++length) assert(!decode_owner_inputs(std::span(bytes).first(length)));
    auto invalid = bytes; invalid.push_back(0); assert(!decode_owner_inputs(invalid));
    for (unsigned at : {0U,4U,5U,6U,7U}) {
        invalid = bytes; invalid[at] = 255; assert(!decode_owner_inputs(invalid));
    }
    p.players = 2; p.owner = 1; p.received_next = {}; p.count = 0; p.first_frame = 0;
    assert(encode_owner_inputs(p).size() == 52);
    p.received_next[2] = 1; assert(encode_owner_inputs(p).empty());
    p.received_next = {}; p.count = 8; p.first_frame = UINT32_MAX - 3; assert(encode_owner_inputs(p).empty());
}
void immutability_and_admission() {
    OwnerInputHistory host, client;
    assert(host.begin(1, 2, 0) && client.begin(1, 2, 1));
    assert(!host.begin(1, 2, 0) && !host.begin(0, 2, 0) && !host.begin(2, 5, 0));
    assert(host.publish(1, sample(0,1)) == OwnerInputResult::Rejected);
    assert(host.publish(0, sample(0,0)) == OwnerInputResult::Accepted);
    assert(host.publish(0, sample(0,0)) == OwnerInputResult::Duplicate);
    auto p = *host.packet_for(0,1);
    std::vector<FinalOwnerInput> received;
    auto forged = p; forged.epoch = 2;
    assert(client.receive_authenticated(0,forged,received) == OwnerInputResult::StaleEpoch);
    assert(!client.actual(0,0));
    forged = p; forged.owner = 1;
    assert(client.receive_authenticated(0,forged,received) == OwnerInputResult::Rejected);
    assert(client.receive_authenticated(0,p,received) == OwnerInputResult::Accepted && received.size() == 1);
    assert(client.receive_authenticated(0,p,received) == OwnerInputResult::Duplicate && received.empty());
    p.inputs[0].buttons ^= 1;
    assert(client.receive_authenticated(0,p,received) == OwnerInputResult::Conflict && client.failed());
    assert(host.publish(0, sample(1,0)) == OwnerInputResult::Conflict && host.failed());
    assert(host.begin(2,2,0) && !host.failed());
    assert(client.begin(2,4,1));
    // This is explicitly a star lane. A client cannot publish or relay to
    // another client: retention only requires the authenticated host's ACK.
    assert(!client.packet_for(1,2) && !client.packet_for(2,0));
    assert(client.packet_for(1,0));
    OwnerInputPacket direct;
    direct.epoch=2; direct.players=4; direct.owner=2; direct.count=1;
    assert(client.receive_authenticated(2,direct,received)==OwnerInputResult::Rejected);
    assert(client.receive_authenticated(0,direct,received)==OwnerInputResult::Accepted);
}
void atomic_ack_and_holes() {
    OwnerInputHistory host, client;
    assert(host.begin(1,2,0) && client.begin(1,2,1));
    for (unsigned f = 0; f < 8; ++f) {
        assert(host.publish(f,sample(0,f)) == OwnerInputResult::Accepted);
        assert(client.publish(f,sample(1,f)) == OwnerInputResult::Accepted);
    }
    const auto full = *host.packet_for(0,1);
    auto late = full; late.first_frame = 4; late.count = 4;
    for (unsigned i = 0; i < 4; ++i) late.inputs[i] = sample(0,i+4);
    std::vector<FinalOwnerInput> received;
    assert(client.receive_authenticated(0,late,received) == OwnerInputResult::Accepted);
    assert(client.received_next(0) == 0); // No cumulative ACK across a hole.
    auto ack = *client.packet_for(1,0); assert(ack.received_next[0] == 0);
    assert(host.receive_authenticated(1,ack,received) == OwnerInputResult::Accepted);
    auto repair = *host.packet_for(0,1); assert(repair.first_frame == 0 && repair.count == 8);
    assert(client.receive_authenticated(0,repair,received) == OwnerInputResult::Accepted && received.size() == 4);
    assert(client.received_next(0) == 8);
    ack = *client.packet_for(1,0); ack.received_next[0] = 9; // Never sent frame 8.
    assert(host.receive_authenticated(1,ack,received) == OwnerInputResult::Rejected);
    assert(host.packet_for(0,1)->first_frame == 0); // Invalid ACK did not retire history.
    ack.received_next[0] = 8;
    assert(host.receive_authenticated(1,ack,received) == OwnerInputResult::Duplicate);
    assert(host.packet_for(0,1)->count == 0);
    repair.epoch = 99; repair.inputs[0].buttons ^= 1;
    assert(client.receive_authenticated(0,repair,received) == OwnerInputResult::StaleEpoch && !client.failed());
}
void retention_backpressure() {
    OwnerInputHistory history;
    assert(history.begin(1,2,0));
    for (unsigned f = 0; f < 128; ++f) {
        assert(history.set_simulation_cursor(f));
        assert(history.publish(f,sample(0,f)) == OwnerInputResult::Accepted);
    }
    assert(history.set_simulation_cursor(128));
    assert(history.publish(128,sample(0,128)) == OwnerInputResult::Backpressure);
    assert(history.actual(0,0) == sample(0,0));
    assert(!history.set_simulation_cursor(1000));
}
void star_network(unsigned players, unsigned delay) {
    std::array<OwnerInputHistory,4> peers;
    for (unsigned p = 0; p < players; ++p) assert(peers[p].begin(7,players,p));
    struct Envelope { unsigned source, destination, when; std::vector<std::uint8_t> bytes; };
    std::deque<Envelope> queue;
    unsigned sends = 0, deliveries = 0;
    constexpr unsigned frames = 1500;
    std::vector<FinalOwnerInput> received;
    for (unsigned time = 0; time < frames + 250; ++time) {
        for (unsigned p = 0; p < players; ++p) {
            if (time < frames) {
                assert(peers[p].set_simulation_cursor(time));
                const auto published = peers[p].publish(time,sample(p,time));
                if (published != OwnerInputResult::Accepted) {
                    std::cerr << "publish failed players=" << players << " delay=" << delay
                              << " peer=" << p << " frame=" << time << " result=" << int(published) << '\n';
                    for (unsigned diagnostic_peer = 0; diagnostic_peer < players; ++diagnostic_peer) {
                        std::cerr << "  peer=" << diagnostic_peer;
                        for (unsigned owner = 0; owner < players; ++owner)
                            std::cerr << " owner" << owner << "_next=" << peers[diagnostic_peer].received_next(owner);
                        std::cerr << '\n';
                    }
                    const auto diagnostic = peers[p].packet_for(p,p ? 0 : 1,OwnerInputSend::Repair);
                    if (diagnostic) std::cerr << "oldest_unacked=" << diagnostic->first_frame << " count=" << int(diagnostic->count) << '\n';
                }
                assert(published == OwnerInputResult::Accepted);
            }
            for (unsigned destination = 0; destination < players; ++destination) {
                if (p == destination || (p && destination)) continue;
                for (unsigned owner = 0; owner < players; ++owner) {
                    for (auto lane : {OwnerInputSend::Live,OwnerInputSend::Repair}) {
                        if (lane == OwnerInputSend::Repair && time % 4 != 0) continue;
                        auto packet = peers[p].packet_for(owner,destination,lane);
                        if (!packet) continue;
                        ++sends;
                        if (sends % 5 == 0) continue; // Twenty percent message loss, even in repair tests.
                        const auto when = time + delay + sends % 4;
                        auto bytes = encode_owner_inputs(*packet);
                        queue.push_back({p,destination,when,bytes});
                        if (sends % 7 == 0) queue.push_front({p,destination,when+1,bytes});
                    }
                }
            }
        }
        for (auto it = queue.begin(); it != queue.end();) {
            if (it->when > time) { ++it; continue; }
            const auto packet = decode_owner_inputs(it->bytes); assert(packet);
            const auto result = peers[it->destination].receive_authenticated(it->source,*packet,received);
            if (result != OwnerInputResult::Accepted && result != OwnerInputResult::Duplicate) {
                std::cerr << "receive failed players=" << players << " delay=" << delay << " time=" << time
                          << " from=" << it->source << " to=" << it->destination << " owner=" << int(packet->owner)
                          << " first=" << packet->first_frame << " count=" << int(packet->count)
                          << " result=" << int(result) << '\n';
            }
            assert(result == OwnerInputResult::Accepted || result == OwnerInputResult::Duplicate);
            for (const auto& final : received) assert(final.input == sample(final.owner,final.frame));
            ++deliveries; it = queue.erase(it);
        }
    }
    for (unsigned p = 0; p < players; ++p) for (unsigned owner = 0; owner < players; ++owner)
        assert(peers[p].received_next(owner) == frames);
    std::cout << "owner lane players=" << players << " delay=" << delay << " delivered=" << deliveries << '\n';
}
}
void relay_does_not_wait_for_a_gap() {
    OwnerInputHistory host,client;std::vector<FinalOwnerInput> received;
    assert(host.begin(19,3,0)&&client.begin(19,3,2));
    OwnerInputPacket late;late.epoch=19;late.players=3;late.owner=1;late.first_frame=4;late.count=4;
    for(unsigned i=0;i<4;++i)late.inputs[i]=sample(1,4+i);
    assert(host.receive_authenticated(1,late,received)==OwnerInputResult::Accepted);
    assert(host.received_next(1)==0);
    const auto live=host.packet_for(1,2);assert(live&&live->first_frame==4&&live->count==4);
    assert(client.receive_authenticated(0,*live,received)==OwnerInputResult::Accepted);
    assert(client.actual(1,7)==sample(1,7)&&client.received_next(1)==0);
    const auto receipts=client.packet_for(2,0);
    assert(receipts&&receipts->received_bits[1]==0xF0);
    assert(host.receive_authenticated(2,*receipts,received)==OwnerInputResult::Duplicate);
    late.first_frame=0;for(unsigned i=0;i<4;++i)late.inputs[i]=sample(1,i);
    assert(host.receive_authenticated(1,late,received)==OwnerInputResult::Accepted);
    const auto repair=host.packet_for(1,2,OwnerInputSend::Repair);
    assert(repair&&repair->first_frame==0&&repair->count==4); // No resend of selectively ACKed 4..7.
    assert(client.receive_authenticated(0,*repair,received)==OwnerInputResult::Accepted);
    assert(client.received_next(1)==8);
}
int main() {
    relay_does_not_wait_for_a_gap();
    codec_tests(); immutability_and_admission(); atomic_ack_and_holes(); retention_backpressure();
    for (unsigned players : {2,3,4}) for (unsigned delay : {0,2,6,12}) star_network(players,delay);
    std::cout << "Experimental owner-input checks passed; not live netplay admission.\n";
}
