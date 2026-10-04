#pragma once
#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace dkr::runtime::netplay::experimental {
// Owned 4-kbit EEPROM for a future replay-qualified adapter. It never accesses
// a file, a live save worker, or a single-player profile. Reads see reversible
// writes; only validated confirmed-frame journals update the confirmed image.
// Not yet wired to the retail EEPROM imports or live experimental admission.
class Eeprom final {
public:
    static constexpr std::size_t kBytes=512, kCheckpointBytes=8+4+kBytes;
    bool start(std::uint64_t epoch,std::span<const std::uint8_t> image);
    bool begin_epoch(std::uint64_t epoch);
    bool begin_frame(std::uint32_t frame);
    bool read(std::size_t offset,std::span<std::uint8_t> destination) const;
    bool write(std::size_t offset,std::span<const std::uint8_t> source);
    bool end_frame(std::vector<std::uint8_t>& journal);
    bool capture(std::span<std::uint8_t> checkpoint) const;
    bool restore(std::span<const std::uint8_t> checkpoint);
    bool commit(std::uint64_t epoch,std::uint32_t frame,std::span<const std::uint8_t> journal);
    std::span<const std::uint8_t,kBytes> confirmed_image() const { return confirmed_; }
private:
    std::array<std::uint8_t,kBytes> working_{},confirmed_{};
    std::vector<std::uint8_t> journal_;
    std::uint64_t epoch_=0;
    std::uint32_t next_=0,confirmed_next_=0;
    std::uint16_t records_=0;
    bool open_=false;
};
}
