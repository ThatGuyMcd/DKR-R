#define XXH_INLINE_ALL
#include "xxHash/xxhash.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <vector>

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Usage: DKRRomHash <canonical-z64-rom>\n";
        return 2;
    }

    const std::filesystem::path path = std::filesystem::u8path(argv[1]);
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        std::cerr << "Could not open ROM: " << path.string() << '\n';
        return 3;
    }

    std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    if (bytes.size() < 0x40 || bytes[0] != 0x80 || bytes[1] != 0x37 || bytes[2] != 0x12 || bytes[3] != 0x40) {
        std::cerr << "Input is not a canonical big-endian N64 ROM.\n";
        return 4;
    }

    const std::uint64_t hash = XXH3_64bits(bytes.data(), bytes.size());
    std::cout << "0x" << std::uppercase << std::hex << std::setw(16) << std::setfill('0') << hash << "ULL\n";
    return 0;
}
