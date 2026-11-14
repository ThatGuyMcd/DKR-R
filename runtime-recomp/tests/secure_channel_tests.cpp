#include "secure_channel.hpp"

#include <cassert>
#include <cstdint>
#include <string>
#include <vector>

int main() {
    using namespace dkr::runtime::netplay::secure;
    const Key key = generate_key();
    Key decoded{};
    assert(decode_key(encode_key(key), decoded));
    assert(decoded == key);
    assert(!decode_key("not-a-key", decoded));

    const std::string message = "authenticated DKR-R frame inputs";
    const auto packet =
        seal(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(message.data()), message.size()), key,
             7U, 31U, 99U);
    std::uint64_t sender = 0;
    std::uint64_t sequence = 0;
    std::vector<std::uint8_t> plain;
    assert(open(packet, key, 99U, sender, sequence, plain));
    assert(sender == 7U && sequence == 31U);
    assert(std::string(plain.begin(), plain.end()) == message);

    auto tampered = packet;
    tampered.back() ^= 1U;
    assert(!open(tampered, key, 99U, sender, sequence, plain));
    assert(!open(packet, key, 100U, sender, sequence, plain));

    const KeyPair host = generate_key_pair();
    const KeyPair client = generate_key_pair();
    const Key capability = generate_key();
    const std::string request = "PipsyFan";
    Key client_peer_key{};
    const auto join_packet = seal_join_request(
        std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(request.data()), request.size()),
        client.secret, client.public_key, host.public_key, capability, 123U, 456U, client_peer_key);
    assert(!join_packet.empty());
    assert(is_join_request(join_packet));
    std::uint64_t inspected_join_sender = 0U;
    std::uint64_t inspected_join_match = 0U;
    assert(inspect_join_request(join_packet, inspected_join_sender, inspected_join_match));
    assert(inspected_join_sender == 123U);
    assert(inspected_join_match == 456U);
    Key opened_public{};
    Key host_peer_key{};
    assert(open_join_request(join_packet, host.secret, capability, 456U, sender, opened_public, host_peer_key, plain));
    assert(sender == 123U);
    assert(opened_public == client.public_key);
    assert(host_peer_key == client_peer_key);
    assert(std::string(plain.begin(), plain.end()) == request);
    const Key wrong_capability = generate_key();
    assert(!open_join_request(join_packet, host.secret, wrong_capability, 456U, sender, opened_public, host_peer_key,
                              plain));
}
