#pragma once
#include "legacy_mod_format.hpp"
#include "legacy_character_limits.hpp"
#include <string>
#include <vector>

namespace dkr::mods::online {
inline constexpr unsigned ManifestSchema = 1;
inline constexpr std::size_t MaxManifestBytes = 2 * 1024 * 1024;
inline constexpr std::size_t MaxPackages = MaxEnabledCharacters + 512;
inline constexpr std::size_t MaxTracks = 512;
inline constexpr std::size_t MaxCharacters = MaxEnabledCharacters;
inline constexpr std::size_t MaxTransferBytes = MaxStaged;
enum class Kind { Track, Character, TrackLab };
enum class PayloadKind { Xdelta, TrackLab };
struct Payload {
    PayloadKind kind = PayloadKind::Xdelta;
    std::string digest;
    std::string source_revision;
    std::uint64_t size = 0;
    std::string source_label;
    bool operator==(const Payload&) const = default;
};
struct Content {
    Kind kind = Kind::Track;
    std::string id, name, payload, artifact, bank;
    unsigned adapter = 0;
    bool operator==(const Content&) const = default;
};
// No paths, user identities, ROM bytes or machine-local activation state.
// Names are only labels; all activation and namespace ordering uses IDs.
struct Manifest {
    unsigned schema = ManifestSchema;
    std::string revision;
    bool custom_ai = true;
    std::vector<Payload> payloads;
    std::vector<Content> content;
    bool operator==(const Manifest&) const = default;
};
bool valid_digest(std::string_view value) noexcept;
Manifest canonical(Manifest value);
Bytes encode(const Manifest& value);
Manifest decode(View value);
std::string identity(const Manifest& value);
std::uint64_t transfer_size(const Manifest& value);
}
