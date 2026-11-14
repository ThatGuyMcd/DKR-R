#include "experimental_lobby_admission.hpp"
#include <array>
#include <cassert>
#include <deque>
#include <iostream>
using namespace dkr::runtime::netplay;
using namespace dkr::runtime::netplay::experimental;
namespace {
struct Packet {
    PeerAddress from;
    std::vector<std::uint8_t> bytes;
};
struct Bus {
    std::array<std::deque<Packet>, 4> received;
    unsigned sends = 0;
    bool impaired = true;
};
struct Route final : SessionTransport {
    Bus& bus;
    unsigned owner;
    bool opened = true;
    Route(Bus& b, unsigned o) : bus(b), owner(o) {}
    bool open(std::uint16_t, std::string&) override {
        return opened = true;
    }
    void close() override {
        opened = false;
    }
    bool is_open() const override {
        return opened;
    }
    std::uint16_t local_port() const override {
        return 0;
    }
    std::size_t maximum_plaintext_datagram_bytes() const override {
        return 12352;
    }
    DatagramSendStatus send_status(const PeerAddress& a, std::span<const std::uint8_t> b, TransportTrafficClass,
                                   std::string&) override {
        assert(a.size == 4 && a.storage[3] < 4 && a.storage[3] != owner);
        ++bus.sends;
        if (bus.impaired && bus.sends % 7 == 0)
            return DatagramSendStatus::WouldBlock;
        if (bus.impaired && bus.sends % 11 == 0)
            return DatagramSendStatus::Sent;
        Packet p{LobbyAdmission::address(std::uint8_t(owner)), {b.begin(), b.end()}};
        auto& queue = bus.received[a.storage[3]];
        if (bus.impaired && bus.sends % 5 == 0)
            queue.push_front(p);
        else
            queue.push_back(p);
        if (bus.impaired && bus.sends % 13 == 0)
            queue.push_back(p);
        return DatagramSendStatus::Sent;
    }
    bool receive(PeerAddress& a, std::vector<std::uint8_t>& b, std::string& e) override {
        e.clear();
        auto& queue = bus.received[owner];
        if (queue.empty())
            return false;
        a = queue.front().from;
        b = std::move(queue.front().bytes);
        queue.pop_front();
        return true;
    }
};
void check(unsigned players) {
    Bus bus;
    AdmissionContract contract;
    contract.engine[0] = 45;
    contract.rom[0] = 7;
    contract.schema = 1;
    contract.state_bytes = 213713;
    std::vector<std::uint8_t> baseline(213713);
    for (std::size_t i = 0; i < baseline.size(); ++i)
        baseline[i] = std::uint8_t(i * 31);
    std::array<std::unique_ptr<Route>, 4> routes;
    std::array<std::unique_ptr<LobbyAdmission>, 4> admissions;
    std::array<unsigned, 4> preparations{};
    std::string error;
    for (unsigned p = 0; p < players; ++p) {
        routes[p] = std::make_unique<Route>(bus, p);
        admissions[p] =
            std::make_unique<LobbyAdmission>(*routes[p], p == 0, std::uint8_t(players), std::uint8_t(p), contract);
        assert(admissions[p]->begin(
            p == 0 ? baseline : std::vector<std::uint8_t>{},
            [&, p](auto b, std::string&) {
                ++preparations[p];
                return std::equal(b.begin(), b.end(), baseline.begin(), baseline.end());
            },
            error));
    }
    auto now = LobbyAdmission::Clock::now();
    bool ready = false;
    // Unequal boot/renderer speeds: no consumer has sent Hello yet. The host
    // must retain its baseline locally, not fill an idle peer's bounded inbox.
    for (unsigned t = 0; t < 100; ++t) {
        now += std::chrono::milliseconds(100);
        assert(admissions[0]->service_admission(now));
        for (unsigned p = 1; p < players; ++p)
            assert(bus.received[p].empty());
    }
    assert(bus.sends == 0);
    for (unsigned t = 0; t < 30000 && !ready; ++t) {
        now += std::chrono::milliseconds(1);
        for (unsigned p = 0; p < players; ++p)
            assert(admissions[p]->service_admission(now));
        ready = admissions[0]->prepared();
    }
    if (!ready)
        for (unsigned p = 0; p < players; ++p)
            std::cerr << "admission peer=" << p << " received=" << admissions[p]->received_bytes()
                      << " total=" << admissions[p]->total_bytes() << " prepared=" << admissions[p]->prepared()
                      << " sends=" << bus.sends << " pending=" << bus.received[p].size() << '\n';
    assert(ready);
    for (unsigned p = 1; p < players; ++p) {
        assert(preparations[p] == 1);
        assert(admissions[p]->baseline_identity() == admissions[0]->baseline_identity());
        assert(admissions[p]->incarnation() == admissions[0]->incarnation());
    }
    // Late duplicated Ready/Offer/Chunk is idempotent, and an early owned
    // input survives the normal lobby Loaded/Start release.
    std::vector<std::uint8_t> input(118, 57);
    bus.impaired = false;
    assert(routes[1]->send_status(LobbyAdmission::address(0), input, TransportTrafficClass::Realtime, error) ==
           DatagramSendStatus::Sent);
    assert(admissions[0]->service_admission(now + std::chrono::milliseconds(1)));
    PeerAddress source;
    std::vector<std::uint8_t> received;
    assert(admissions[0]->receive(source, received, error) && received == input &&
           source == LobbyAdmission::address(1));
    for (unsigned t = 0; t < 500; ++t)
        for (unsigned p = 0; p < players; ++p)
            assert(admissions[p]->service_admission(now + std::chrono::milliseconds(t + 2)));
    for (unsigned p = 1; p < players; ++p)
        assert(preparations[p] == 1);
}
void mismatch() {
    Bus bus;
    bus.impaired = false;
    Route host(bus, 0), client(bus, 1);
    AdmissionContract h;
    h.engine[0] = 1;
    h.rom[0] = 7;
    h.schema = 1;
    h.state_bytes = 3;
    auto c = h;
    c.engine[0] = 2;
    LobbyAdmission a(host, true, 2, 0, h), b(client, false, 2, 1, c);
    std::string error;
    assert(a.begin({1, 2, 3}, {}, error));
    assert(b.begin({}, [](auto, std::string&) { return true; }, error));
    const auto now = LobbyAdmission::Clock::now();
    assert(a.service_admission(now));
    assert(b.service_admission(now));
    assert(!a.service_admission(now + std::chrono::milliseconds(1)));
    assert(!b.prepared());
    assert(!a.error().empty());
    assert(a.error().find("replay engine") != std::string::npos);
}
void contracts() {
    AdmissionContract a;
    a.engine[0] = 2;
    a.rom[31] = 2;
    a.resources[5] = 3;
    a.schema = 0x0102030405060708ULL;
    a.state_bytes = 3;
    a.launch = 43;
    a.save = 44;
    a.epoch = 45;
    const auto wire = a.encode();
    assert(wire.size() == 136 && wire[96] == 8 && wire[103] == 1);
    AdmissionContract b;
    assert(AdmissionContract::decode(wire, b) && a.difference(b).empty());
    assert(!AdmissionContract::decode(std::span(wire).first(135), b));
    for (unsigned field = 0; field < 8; ++field) {
        auto changed = wire;
        changed[field < 3 ? field * 32 : 96 + (field - 3) * 8] ^= 1;
        assert(AdmissionContract::decode(changed, b));
        assert(!a.difference(b).empty());
        Bus bus;
        bus.impaired = false;
        Route host(bus, 0), client(bus, 1);
        LobbyAdmission x(host, true, 2, 0, a), y(client, false, 2, 1, b);
        std::string error;
        assert(x.begin({1, 2, 3}, {}, error));
        assert(y.begin(
            {},
            [](auto, std::string&) {
                assert(false);
                return false;
            },
            error));
        const auto now = LobbyAdmission::Clock::now();
        assert(y.service_admission(now));
        assert(!x.service_admission(now));
        assert(x.error().find(a.difference(b)) != std::string::npos);
        assert(!x.prepared() && !y.prepared());
        // The client must diagnose a differing Offer just as precisely, and
        // reject it before allocating/calling the checkpoint preparation hook.
        std::vector<std::uint8_t> offer{'D', 'K', 'X', 'A', 4, 2};
        const auto record = a.encode();
        offer.insert(offer.end(), record.begin(), record.end());
        offer.resize(6 + 136 + 73);
        bus.received[1].push_back({LobbyAdmission::address(0), std::move(offer)});
        assert(!y.service_admission(now + std::chrono::milliseconds(1)));
        assert(y.error().find(a.difference(b)) != std::string::npos && y.total_bytes() == 0);
    }
}
} // namespace
int main() {
    contracts();
    for (unsigned p = 2; p <= 4; ++p)
        check(p);
    mismatch();
    std::cout
        << "Owned normal-lobby admission: named component mismatches, canonical wire, 2/3/4 owners, impairment, integrity, idempotence and early-input preservation passed.\n";
}
