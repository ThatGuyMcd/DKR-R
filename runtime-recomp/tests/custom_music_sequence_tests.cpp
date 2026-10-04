#include "custom_music_sequence.hpp"

#include <array>
#include <cassert>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace dkr::runtime::custom_music;

namespace {

std::vector<std::uint8_t> from_hex(const std::string& hex) {
    std::vector<std::uint8_t> bytes;
    for (std::size_t i = 0; i + 1 < hex.size(); i += 2) {
        bytes.push_back(static_cast<std::uint8_t>(std::stoul(hex.substr(i, 2), nullptr, 16)));
    }
    return bytes;
}

} // namespace

int main() {
    // The addon's validator wrote these cases; both sides must agree on every
    // code (tools/blender/tests/test_music_sequence.py --write-fixtures).
    std::ifstream fixtures(DKR_MUSIC_FIXTURES "/music_sequences.txt");
    assert(fixtures && "missing music_sequences.txt");
    std::string line;
    int cases = 0;
    int failures = 0;
    std::vector<std::uint8_t> minimal;
    while (std::getline(fixtures, line)) {
        if (line.empty() || line[0] == '#') {
            continue;
        }
        std::istringstream fields(line);
        std::string expected, name, hex;
        fields >> expected >> name >> hex;
        const std::vector<std::uint8_t> data = from_hex(hex);
        const SequenceCheck result = validate_sequence(data);
        ++cases;
        if (result.code != expected) {
            std::fprintf(stderr, "%s: got %s, expected %s (%s)\n", name.c_str(),
                         result.code.c_str(), expected.c_str(), result.message.c_str());
            ++failures;
        }
        if (name == "minimal") {
            minimal = data;
        }
    }
    assert(cases >= 30);
    assert(failures == 0);

    // The report of the smallest valid song.
    assert(!minimal.empty());
    const SequenceCheck ok = validate_sequence(minimal);
    assert(ok.ok() && ok.report.notes == 2U && ok.report.channel_mask == 1U);
    assert(ok.report.division == 384U && ok.report.initial_tempo == 500000U);
    assert(!ok.report.loops_forever);

    // A program change to a null bank slot crashes __setInstChanState. The
    // minimal song selects program 1.
    std::array<bool, 128> programs{};
    programs.fill(true);
    assert(validate_sequence(minimal, kRetailSequenceCapacity, &programs).ok());
    programs[1] = false;
    assert(validate_sequence(minimal, kRetailSequenceCapacity, &programs).code ==
           "program-missing");

    // The capacity is the caller's: the ROM's own buffer, not a constant.
    assert(validate_sequence(minimal, static_cast<std::uint32_t>(minimal.size() - 1U)).code ==
           "too-large");

    std::printf("custom music sequence tests passed (%d fixtures)\n", cases);
    return 0;
}
