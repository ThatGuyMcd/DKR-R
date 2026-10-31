#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "online_profile/picture_codec.hpp"
#include <iostream>

namespace {
unsigned checks = 0;
void check(bool condition) {
    ++checks;
    if (!condition)
        throw std::runtime_error("Picture check " + std::to_string(checks) + " failed");
}
template <class F> void rejected(F call) {
    bool failed = false;
    try {
        call();
    } catch (const std::exception&) {
        failed = true;
    }
    check(failed);
}
std::string png(unsigned width, unsigned height, const std::vector<unsigned char>& pixels) {
    std::string bytes;
    const auto write = [](void* p, void* data, int size) {
        static_cast<std::string*>(p)->append(static_cast<char*>(data), size);
    };
    if (!stbi_write_png_to_func(write, &bytes, width, height, 4, pixels.data(), width * 4))
        throw std::runtime_error("fixture encode failed");
    return bytes;
}
} // namespace
int main() try {
    using namespace dkr::profile;
    std::vector<unsigned char> pixels(256 * 128 * 4, 255);
    for (unsigned y = 0; y < 128; ++y)
        for (unsigned x = 0; x < 256; ++x) {
            const auto at = (y * 256 + x) * 4;
            pixels[at] = (x >= 64 && x < 192) ? 17 : 251;
            pixels[at + 1] = 85;
            pixels[at + 2] = 153;
        }
    const auto result = decode_picture(png(256, 128, pixels));
    check(result.rgba.size() == 128 * 128 * 4);
    check(result.png.size() <= max_picture_png);
    check(result.rgba[64 * 128 * 4 + 64 * 4] == 17); // The centre is kept, rather than stretching a wide picture.
    check(result.png.find("Exif") == std::string::npos);
    const auto roundtrip = decode_picture(result.png);
    check(roundtrip.rgba == result.rgba);
    rejected([] { decode_picture({}); });
    rejected([] { decode_picture("not an image"); });
    rejected([] { decode_picture(std::string(max_picture_input + 1, 'x')); });
    rejected([&] { decode_picture(result.png.substr(0, 40)); });
    std::vector<unsigned char> too_wide(4097 * 4, 255);
    rejected([&] { decode_picture(png(4097, 1, too_wide)); });
    rejected([] { read_picture(std::filesystem::path{}); });
    std::cout << checks << " profile picture checks passed\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
