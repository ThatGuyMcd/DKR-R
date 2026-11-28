#pragma once

#include "theatre_file.hpp"

#include <algorithm>
#include <cstdint>
#include <exception>
#include <span>
#include <vector>

namespace dkr::runtime::time_trial {

// Retain seekable audio for multi-hour runs without holding the PCM in memory.
class TheatreAudio {
  public:
    bool append(unsigned update, std::span<const std::int16_t> samples) {
        try {
            if (!file_.open())
                return false;
            auto& stream = file_.stream;
            stream.seekp(end_);
            stream.write(reinterpret_cast<const char*>(samples.data()), samples.size_bytes());
            if (!stream)
                return false;
            blocks_.push_back({update, static_cast<unsigned>(samples.size()), end_});
            end_ += samples.size_bytes();
            return true;
        } catch (const std::exception&) {
            return false;
        }
    }

    bool read(unsigned after, unsigned through, std::vector<std::int16_t>& output) {
        const auto by_update = [](unsigned update, const Block& block) { return update < block.update; };
        const auto first = std::upper_bound(blocks_.begin(), blocks_.end(), after, by_update);
        const auto last = std::upper_bound(first, blocks_.end(), through, by_update);
        for (auto block = first; block != last; ++block) {
            const auto offset = output.size();
            output.resize(offset + block->count);
            file_.stream.seekg(block->offset);
            file_.stream.read(reinterpret_cast<char*>(output.data() + offset), block->count * sizeof(std::int16_t));
            if (!file_.stream) {
                output.resize(offset);
                return false;
            }
        }
        return true;
    }

    void clear() {
        file_.close();
        blocks_.clear();
        end_ = 0;
    }

  private:
    struct Block {
        unsigned update;
        unsigned count;
        std::streamoff offset;
    };

    TheatreFile file_;
    std::vector<Block> blocks_;
    std::streamoff end_ = 0;
};

} // namespace dkr::runtime::time_trial
