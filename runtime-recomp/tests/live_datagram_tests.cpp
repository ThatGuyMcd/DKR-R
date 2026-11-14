#include "netplay/peer_relay/live_datagram.hpp"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>

using namespace dkr::relay;
using namespace std::chrono_literals;
namespace {
void check(bool value, const char* why) {
    if (!value) {
        std::fprintf(stderr, "Datagram regression: %s\n", why);
        std::abort();
    }
}
Context context(Id local) {
    Context c;
    c.active = c.quick = true;
    c.local = local;
    c.host = 1;
    c.room = 77;
    c.members = {{1, "Host"}, {2, "Client"}};
    return c;
}
} // namespace
int main() {
    LiveDatagram host, client;
    auto h = context(1), c = context(2);
    host.configure(h, {});
    client.configure(c, {});
    secure::Key key{};
    key.fill(0x42);
    const Bytes input{1, 2, 3, 4};
    const auto from_host = secure::seal(input, key, 1, 1, 77), from_client = secure::seal(input, key, 2, 1, 77);
    check(!host.send(2, from_host, 2), "unestablished routes use the existing fallback");
#if defined(DKR_TEST_LIVE_DATAGRAM_ICE)
    bool received_host = false, received_client = false, active_host = false, active_client = false;
    Bytes description;
    const auto deadline = Clock::now() + 8s;
    while (Clock::now() < deadline && !(active_host && active_client && received_host && received_client)) {
        for (const auto& signal : host.controls()) {
            description = signal.bytes;
            check(!client.control(3, signal.bytes), "unknown sender cannot negotiate a route");
            auto wrong = signal.bytes;
            wrong[6] ^= 1;
            check(!client.control(1, wrong), "foreign room cannot negotiate a route");
            wrong = signal.bytes;
            wrong.push_back(0);
            check(!client.control(1, wrong), "embedded NUL description is rejected");
            check(client.control(1, signal.bytes), "authenticated host description admitted");
        }
        for (const auto& signal : client.controls())
            check(host.control(2, signal.bytes), "authenticated client description admitted");
        active_host |= host.send(2, from_host, 2);
        active_client |= client.send(1, from_client, 3);
        for (auto* path : {&host, &client}) {
            Id source = 0;
            unsigned lane = 0;
            Bytes sealed;
            while (path->receive(source, sealed, lane)) {
                const auto expected = path == &host ? 2U : 1U;
                Id sender = 0, sequence = 0;
                Bytes plain;
                check(source == expected && lane == (expected == 1 ? 2U : 3U), "source and race lane preserved");
                check(secure::open(sealed, key, 77, sender, sequence, plain) && sender == expected && plain == input,
                      "original AEAD envelope arrives byte intact");
                if (path == &host)
                    received_host = true;
                else
                    received_client = true;
            }
        }
        std::this_thread::sleep_for(10ms);
    }
    check(active_host && active_client && received_host && received_client,
          "real bidirectional ICE datagrams bypass SCTP");
    for (unsigned lane : {0U, 1U, 4U})
        check(!host.send(2, from_host, lane), "reliable setup and lifecycle traffic remain on the existing transport");
    auto oversized = from_host;
    oversized.resize(1201);
    check(!host.send(2, oversized, 2), "large packets use the existing transport");
    check(!host.send(3, from_host, 2), "a route cannot select an unadmitted destination");
    auto tampered = from_host;
    tampered.back() ^= 1;
    host.send(2, tampered, 2);
    bool rejected = false;
    const auto bad_deadline = Clock::now() + 1s;
    while (Clock::now() < bad_deadline && !rejected) {
        Id source = 0, sender = 0, sequence = 0;
        unsigned lane = 0;
        Bytes sealed, plain;
        while (client.receive(source, sealed, lane))
            if (sealed == tampered)
                rejected = !secure::open(sealed, key, 77, sender, sequence, plain);
        std::this_thread::sleep_for(5ms);
    }
    check(rejected, "datagram delivery cannot bypass the existing authentication check");
    std::this_thread::sleep_for(800ms);
    check(!host.send(2, from_host, 2), "a silent route duplicates through the existing fallback");
    h.members.resize(1);
    host.configure(h, {});
    check(!host.control(2, description), "removed peers cannot revive their old route");
    client.reset();
    check(!client.control(1, description), "retired room cannot accept delayed negotiation");
    std::puts("ICE datagram handshake, sealed delivery, scope, size, lane and silent-route fallback checks passed.");
#else
    check(host.controls().empty() && client.controls().empty(), "platforms without ICE keep the existing transport");
    std::puts("Datagram fallback policy passed; ICE support unavailable in this configuration.");
#endif
}
