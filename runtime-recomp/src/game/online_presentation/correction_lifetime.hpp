#pragma once
#include <array>
#include <cstdint>
#include <memory>

namespace dkr::runtime::netplay::experimental {
// Accessed under SubmissionDomain::mutex. Weak records do not retain image RAM
// or acquire a final strong reference whose destructor takes that same mutex.
template <class Image> class CorrectionLifetime final {
    struct Entry {
        std::weak_ptr<const Image> image;
        const Image* address = nullptr;
        std::uint32_t frame = 0;
        bool allowed = false;
    };
    std::array<Entry, 4> entries_{};

  public:
    void reset() {
        entries_ = {};
    }
    bool publish(const std::shared_ptr<const Image>& image) {
        for (auto& entry : entries_)
            if (entry.image.expired()) {
                entry = {image, image.get(), image->descriptor().frame, true};
                return true;
            }
        return false;
    }
    bool eligible(const Image* image) const {
        for (const auto& entry : entries_)
            if (entry.address == image && !entry.image.expired())
                return entry.allowed;
        return false;
    }
    void retire(std::uint32_t first, bool correction) {
        for (auto& entry : entries_)
            if (!entry.image.expired() && (!correction || entry.frame >= first))
                entry.allowed = false;
    }
};
} // namespace dkr::runtime::netplay::experimental
