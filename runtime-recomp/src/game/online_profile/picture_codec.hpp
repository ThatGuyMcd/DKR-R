#pragma once
#include "stb/stb_image.h"
#include "stb/stb_image_write.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace dkr::profile {
inline constexpr unsigned picture_size = 128;
inline constexpr std::size_t max_picture_input = 8 * 1024 * 1024;
inline constexpr std::size_t max_picture_png = 96 * 1024;
struct Picture {
    std::vector<unsigned char> rgba;
    std::string png;
};

inline Picture decode_picture(const std::string& bytes) {
    if (bytes.empty() || bytes.size() > max_picture_input)
        throw std::runtime_error("Choose an image no larger than 8 MB.");
    const auto* data = reinterpret_cast<const stbi_uc*>(bytes.data());
    const bool png = bytes.size() >= 8 && bytes.compare(0, 8, "\x89PNG\r\n\x1a\n", 8) == 0;
    const bool jpeg = bytes.size() >= 3 && data[0] == 0xFF && data[1] == 0xD8 && data[2] == 0xFF;
    if (!png && !jpeg)
        throw std::runtime_error("Choose a PNG or JPG profile picture.");
    int width = 0, height = 0, channels = 0;
    if (!stbi_info_from_memory(data, int(bytes.size()), &width, &height, &channels) || width < 1 || height < 1 ||
        width > 4096 || height > 4096)
        throw std::runtime_error("Choose an image no larger than 4096 x 4096 pixels.");
    std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> source(
        stbi_load_from_memory(data, int(bytes.size()), &width, &height, &channels, 4), stbi_image_free);
    if (!source)
        throw std::runtime_error("This picture could not be read. Try another PNG or JPG.");
    Picture result;
    result.rgba.resize(picture_size * picture_size * 4);
    const double side = std::min(width, height);
    const double left = (width - side) * .5, top = (height - side) * .5;
    for (unsigned y = 0; y < picture_size; ++y)
        for (unsigned x = 0; x < picture_size; ++x) {
            const double sx = std::clamp(left + (x + .5) * side / picture_size - .5, 0., double(width - 1));
            const double sy = std::clamp(top + (y + .5) * side / picture_size - .5, 0., double(height - 1));
            const int x0 = int(sx), y0 = int(sy), x1 = std::min(x0 + 1, width - 1), y1 = std::min(y0 + 1, height - 1);
            const double fx = sx - x0, fy = sy - y0;
            for (unsigned c = 0; c < 4; ++c) {
                const auto sample = [&](int px, int py) {
                    return source.get()[(std::size_t(py) * width + px) * 4 + c];
                };
                const double value = (sample(x0, y0) * (1 - fx) + sample(x1, y0) * fx) * (1 - fy) +
                                     (sample(x0, y1) * (1 - fx) + sample(x1, y1) * fx) * fy;
                result.rgba[(std::size_t(y) * picture_size + x) * 4 + c] =
                    static_cast<unsigned char>(std::lround(value));
            }
        }
    const auto write = [](void* context, void* data, int size) {
        if (size > 0)
            static_cast<std::string*>(context)->append(static_cast<const char*>(data), std::size_t(size));
    };
    if (!stbi_write_png_to_func(write, &result.png, picture_size, picture_size, 4, result.rgba.data(),
                                picture_size * 4) ||
        result.png.empty() || result.png.size() > max_picture_png)
        throw std::runtime_error("The profile picture could not be prepared.");
    return result;
}

inline Picture read_picture(const std::filesystem::path& path) {
    std::error_code error;
    const auto status = std::filesystem::symlink_status(path, error);
    if (error || !std::filesystem::is_regular_file(status))
        throw std::runtime_error("Choose a regular image file.");
    const auto size = std::filesystem::file_size(path, error);
    if (error || !size || size > max_picture_input)
        throw std::runtime_error("Choose an image no larger than 8 MB.");
    std::ifstream input(path, std::ios::binary);
    std::string bytes(std::size_t(size), '\0');
    if (!input.read(bytes.data(), std::streamsize(bytes.size())) || input.peek() != std::char_traits<char>::eof())
        throw std::runtime_error("The picture changed while it was being read. Choose it again.");
    return decode_picture(bytes);
}
} // namespace dkr::profile
