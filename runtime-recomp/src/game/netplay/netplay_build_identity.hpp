#pragma once

#include "authoritative_state.hpp"
#include "netplay_types.hpp"
#include "../mods/online_mod_wire_version.hpp"
#include "../mods/online_mod_manifest.hpp"

#include <string>
#include <string_view>

namespace dkr::runtime::netplay {

// Package labels are allowed to differ between CI jobs (for example a local
// Windows RC and a Linux AppImage produced from the same source). Admission is
// based on this platform-neutral network ABI identity instead of the package
// filename or compiler platform.
inline std::string canonical_network_release(std::string_view source_version) {
    while (!source_version.empty() && (source_version.front() == ' ' || source_version.front() == '\t' ||
                                       source_version.front() == '\r' || source_version.front() == '\n')) {
        source_version.remove_prefix(1U);
    }
    while (!source_version.empty() && (source_version.back() == ' ' || source_version.back() == '\t' ||
                                       source_version.back() == '\r' || source_version.back() == '\n')) {
        source_version.remove_suffix(1U);
    }
    return source_version.empty() ? "development" : std::string(source_version);
}

inline std::string canonical_network_build_fingerprint(std::string_view source_version) {
    return "DKR-R/" + canonical_network_release(source_version) + "/netplay-protocol-" +
           std::to_string(kProtocolVersion) + "/authority-schema-" + std::to_string(kAuthoritativeStateSchema) +
           "/mod-transfer-" + std::to_string(dkr::mods::online::WireVersion) + "/mod-manifest-" +
           std::to_string(dkr::mods::online::ManifestSchema) + "/behaviour-" + std::to_string(dkr::mods::BehaviourAbi) +
           "/native-guest-lifetime-1" + "/runtime-ae1ffbb9/gekkonet-5924b5c7";
}

} // namespace dkr::runtime::netplay
