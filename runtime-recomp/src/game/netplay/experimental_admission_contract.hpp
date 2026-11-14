#pragma once
#include "secure_channel.hpp"
#include <algorithm>
#include <span>
#include <string>
#include <vector>

namespace dkr::runtime::netplay::experimental {
// A canonical wire record, never sizeof(native struct) or host byte order.
// Keep the components separate so a rejected match identifies the cause.
struct AdmissionContract {
    secure::Key engine{}, rom{}, resources{};
    std::uint64_t schema = 0, state_bytes = 0, launch = 0, save = 0, epoch = 0;
    static constexpr std::size_t wire_bytes = 136;
    std::vector<std::uint8_t> encode() const {
        std::vector<std::uint8_t> out;
        out.reserve(wire_bytes);
        for (const auto* key : {&engine, &rom, &resources})
            out.insert(out.end(), key->begin(), key->end());
        for (const auto value : {schema, state_bytes, launch, save, epoch})
            for (unsigned i = 0; i < 8; ++i)
                out.push_back(std::uint8_t(value >> (8 * i)));
        return out;
    }
    static bool decode(std::span<const std::uint8_t> bytes, AdmissionContract& out) {
        if (bytes.size() != wire_bytes)
            return false;
        std::size_t at = 0;
        AdmissionContract next;
        for (auto* key : {&next.engine, &next.rom, &next.resources}) {
            std::copy_n(bytes.begin() + at, 32, key->begin());
            at += 32;
        }
        for (auto* value : {&next.schema, &next.state_bytes, &next.launch, &next.save, &next.epoch})
            for (unsigned i = 0; i < 8; ++i)
                *value |= std::uint64_t(bytes[at++]) << (8 * i);
        out = next;
        return true;
    }
    std::string difference(const AdmissionContract& peer) const {
        if (engine != peer.engine)
            return "replay engine";
        if (rom != peer.rom)
            return "Game Pak";
        if (resources != peer.resources)
            return "prepared mod resources";
        if (schema != peer.schema)
            return "checkpoint schema";
        if (state_bytes != peer.state_bytes)
            return "checkpoint size";
        if (launch != peer.launch)
            return "launch rules";
        if (save != peer.save)
            return "online save";
        if (epoch != peer.epoch)
            return "startup epoch";
        return {};
    }
    std::string diagnostics() const {
        const auto hex = [](const secure::Key& key) {
            std::string out;
            for (auto c : key) {
                out += "0123456789abcdef"[c >> 4];
                out += "0123456789abcdef"[c & 15];
            }
            return out;
        };
        return "engine=" + hex(engine) + " rom=" + hex(rom) + " resources=" + hex(resources) +
               " schema=" + std::to_string(schema) + " bytes=" + std::to_string(state_bytes) +
               " launch=" + std::to_string(launch) + " save=" + std::to_string(save) +
               " epoch=" + std::to_string(epoch);
    }
};
} // namespace dkr::runtime::netplay::experimental
