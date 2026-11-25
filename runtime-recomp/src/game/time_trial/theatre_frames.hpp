#pragma once

#include "session.hpp"
#include "ultramodern/renderer_context.hpp"
#include <miniz/miniz.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <deque>
#include <exception>
#include <fstream>
#include <random>
#include <span>
#include <vector>

namespace dkr::runtime::time_trial {

// One renderer owns this temporary archive. Only frames actually submitted by
// the local verifier can enter it. Decoding never writes to live guest memory.
class TheatreFrames final {
  public:
    static constexpr std::size_t ImageBytes = 8U * 1024U * 1024U;

    struct Frame {
        OSTask task{};
        FrameStamp stamp;
    };

    explicit TheatreFrames(bool sequential = false) : sequential_(sequential) {}

    ~TheatreFrames() {
        file_.close();
        std::error_code error;
        if (directory_.empty())
            return;
        std::filesystem::remove(directory_ / "frames.tmp", error);
        std::filesystem::remove(directory_, error);
    }

    bool append(const OSTask& task, FrameStamp stamp, std::span<const std::uint8_t, ImageBytes> image) {
        try {
            if (sequential_) {
                if (stamp.progress.updates <= last())
                    return true;
                // The guest waits for the consumer before producing more.
                // Refuse unexpected growth instead of falling back to disk.
                if (streamed_.size() >= 12)
                    return false;
                streamed_.push_back({{task, stamp}, {image.begin(), image.end()}});
                return true;
            }
            if (!entries_.empty() && stamp.progress.updates <= last())
                return true;
            const bool key = entries_.size() % Group == 0;
            if (entries_.empty() && !open())
                return false;
            if (key) {
                raw_.assign(image.begin(), image.end());
            } else {
                raw_.assign(MapBytes, 0);
                for (std::size_t page = 0; page < Pages; ++page) {
                    const auto offset = page * PageBytes;
                    if (std::memcmp(last_image_.data() + offset, image.data() + offset, PageBytes) == 0)
                        continue;
                    raw_[page / 8] |= 1U << (page % 8);
                    const auto start = raw_.size();
                    raw_.resize(start + PageBytes);
                    for (std::size_t i = 0; i < PageBytes; ++i)
                        raw_[start + i] = last_image_[offset + i] ^ image[offset + i];
                }
            }
            auto bytes = static_cast<mz_ulong>(mz_compressBound(static_cast<mz_ulong>(raw_.size())));
            compressed_.resize(bytes);
            if (mz_compress2(compressed_.data(), &bytes, raw_.data(), static_cast<mz_ulong>(raw_.size()), 1) != MZ_OK)
                return false;
            file_.seekp(write_offset_);
            file_.write(reinterpret_cast<const char*>(compressed_.data()), bytes);
            if (!file_)
                return false;
            entries_.push_back({{task, stamp}, write_offset_, bytes, static_cast<mz_ulong>(raw_.size())});
            write_offset_ += bytes;
            last_image_.assign(image.begin(), image.end());
            return true;
        } catch (const std::exception&) {
            return false;
        }
    }

    unsigned last() const {
        if (sequential_)
            return streamed_.empty() ? 0 : streamed_.back().frame.stamp.progress.updates;
        return entries_.empty() ? 0 : entries_.back().frame.stamp.progress.updates;
    }

    const Frame* load(unsigned requested) {
        if (sequential_) {
            if (streamed_.empty())
                return nullptr;
            while (streamed_.size() > 1 && streamed_[1].frame.stamp.progress.updates <= requested)
                streamed_.pop_front();
            return &streamed_.front().frame;
        }
        if (entries_.empty())
            return nullptr;
        const auto after =
            std::upper_bound(entries_.begin(), entries_.end(), requested, [](unsigned frame, const Entry& entry) {
                return frame < entry.frame.stamp.progress.updates;
            });
        const auto index = static_cast<std::size_t>(std::max(entries_.begin() + 1, after) - entries_.begin() - 1);
        if (decoded_index_ == index)
            return &entries_[index].frame;
        const auto key = index - index % Group;
        const auto start =
            decoded_index_ && *decoded_index_ >= key && *decoded_index_ < index ? *decoded_index_ + 1 : key;
        decoded_.resize(ImageBytes);
        for (auto i = start; i <= index; ++i) {
            if (!decode(entries_[i], i % Group == 0)) {
                decoded_index_.reset();
                return nullptr;
            }
        }
        decoded_index_ = index;
        return &entries_[index].frame;
    }

    std::uint8_t* image() {
        return sequential_ ? streamed_.front().image.data() : decoded_.data();
    }

  private:
    static constexpr std::size_t PageBytes = 4096, Pages = ImageBytes / PageBytes;
    static constexpr std::size_t MapBytes = Pages / 8, Group = 45;

    struct Entry {
        Frame frame;
        std::streamoff offset;
        mz_ulong bytes, raw_bytes;
    };

    struct StreamFrame {
        Frame frame;
        std::vector<std::uint8_t> image;
    };
    bool sequential_;
    std::deque<StreamFrame> streamed_;

    bool open() {
        const auto root = std::filesystem::temp_directory_path();
        std::random_device random;
        for (int i = 0; i < 8; ++i) {
            const auto path = root / ("dkr-r-theatre-" + std::to_string(random()) + "-" + std::to_string(random()));
            if (std::filesystem::create_directory(path)) {
                directory_ = path;
                file_.open(path / "frames.tmp", std::ios::in | std::ios::out | std::ios::binary | std::ios::trunc);
                return bool(file_);
            }
        }
        return false;
    }

    bool decode(const Entry& entry, bool key) {
        compressed_.resize(entry.bytes);
        file_.seekg(entry.offset);
        file_.read(reinterpret_cast<char*>(compressed_.data()), entry.bytes);
        if (!file_)
            return false;
        auto size = entry.raw_bytes;
        if (key)
            return mz_uncompress(decoded_.data(), &size, compressed_.data(), entry.bytes) == MZ_OK &&
                   size == ImageBytes;
        raw_.resize(size);
        if (mz_uncompress(raw_.data(), &size, compressed_.data(), entry.bytes) != MZ_OK || size != entry.raw_bytes ||
            size < MapBytes)
            return false;
        std::size_t cursor = MapBytes;
        for (std::size_t page = 0; page < Pages; ++page) {
            if (!(raw_[page / 8] & (1U << (page % 8))))
                continue;
            if (size - cursor < PageBytes)
                return false;
            const auto offset = page * PageBytes;
            for (std::size_t i = 0; i < PageBytes; ++i)
                decoded_[offset + i] ^= raw_[cursor + i];
            cursor += PageBytes;
        }
        return cursor == size;
    }

    std::filesystem::path directory_;
    std::fstream file_;
    std::streamoff write_offset_ = 0;
    std::vector<Entry> entries_;
    std::vector<std::uint8_t> last_image_, decoded_, raw_, compressed_;
    std::optional<std::size_t> decoded_index_;
};

} // namespace dkr::runtime::time_trial
