#include <cassert>
#include <cctype>
#include <fstream>
#include <map>
#include <string>
#include <string_view>

namespace {

bool ValidGuid(std::string_view guid) {
    if (guid == "xinput") {
        return true;
    }
    if (guid.size() != 32U) {
        return false;
    }
    for (const unsigned char value : guid) {
        if (std::isxdigit(value) == 0) {
            return false;
        }
    }
    return true;
}

std::string Field(std::string_view line, std::string_view prefix) {
    const std::size_t position = line.find(prefix);
    if (position == std::string_view::npos) {
        return {};
    }
    const std::size_t begin = position + prefix.size();
    const std::size_t end = line.find(',', begin);
    return std::string(line.substr(begin, end - begin));
}

} // namespace

int main(int argc, char** argv) {
    assert(argc == 2);
    std::ifstream input(argv[1]);
    assert(input);

    std::size_t mappings = 0U;
    std::map<std::string, std::size_t> platforms;
    bool has_8bitdo_n64 = false;
    bool has_nso_n64 = false;
    bool has_hyperkin_n64 = false;
    bool has_mayflash_n64 = false;
    bool has_raphnet_n64 = false;
    bool has_retrousb_n64 = false;
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty() || line.front() == '#') {
            continue;
        }
        const std::size_t guid_end = line.find(',');
        assert(guid_end != std::string::npos);
        assert(ValidGuid(std::string_view(line).substr(0U, guid_end)));
        const std::size_t name_end = line.find(',', guid_end + 1U);
        assert(name_end != std::string::npos && name_end > guid_end + 1U);
        assert(line.find(':', name_end + 1U) != std::string::npos);
        const std::string platform = Field(line, ",platform:");
        assert(!platform.empty());
        ++platforms[platform];
        ++mappings;

        has_8bitdo_n64 |= line.find(",8BitDo N64,") != std::string::npos;
        has_nso_n64 |= line.find(",NSO N64 Controller,") != std::string::npos;
        has_hyperkin_n64 |= line.find(",Hyperkin Admiral N64 Controller,") != std::string::npos;
        has_mayflash_n64 |= line.find(",Mayflash N64 Adapter,") != std::string::npos;
        has_raphnet_n64 |= line.find(",Raphnet N64 Adapter,") != std::string::npos;
        has_retrousb_n64 |= line.find(",RetroUSB N64 RetroPort,") != std::string::npos;
    }

    assert(mappings >= 2200U);
    assert(platforms["Windows"] >= 800U);
    assert(platforms["Linux"] >= 700U);
    assert(platforms["Mac OS X"] >= 300U);
    assert(has_8bitdo_n64 && has_nso_n64 && has_hyperkin_n64);
    assert(has_mayflash_n64 && has_raphnet_n64 && has_retrousb_n64);
    return 0;
}
