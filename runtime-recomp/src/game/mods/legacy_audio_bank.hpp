#pragma once
#include "legacy_mod_format.hpp"
#include <map>

namespace dkr::mods {
struct SequenceRecord {
    std::uint32_t offset = 0, length = 0, loaded_length = 0;
    std::string digest;
};
struct SequenceDirectory {
    std::vector<SequenceRecord> records;
    std::uint32_t maximum_loaded_length = 0;
};
// Validate table, DMA allocation and compact-MIDI header/track offsets only.
// Does NOT certify the event streams, loops, programs or instrument bank.
SequenceDirectory inspect_sequence_directory(View bank);
// Repack selected sequences over the original bank. Never replace unrelated
// songs because an exporter shifted or padded their offsets in a ROM patch.
Bytes build_sequence_bank(View original, const std::map<unsigned, Bytes>& replacements,
                          std::uint32_t allocated_sequence_capacity);
// Certify changed streams in a retained prepared bank, not just its header.
void validate_changed_sequences(View original, View prepared);
// Compare resolved musical instruments, not ROM addresses or exporter padding.
// Both graphs are fully bounds checked. Equality permits retaining the original
// live bank while playing an imported sequence; it never licenses a bank swap.
bool equivalent_music_banks(View original_control, View original_samples, View imported_control, View imported_samples);
} // namespace dkr::mods
