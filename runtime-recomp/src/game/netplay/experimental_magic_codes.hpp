#pragma once
#include "../magic_code_runtime_policy.hpp"
#include <cstdint>
#include <span>
#include <vector>

namespace dkr::runtime::netplay::experimental {
// Separate, reversible launch/action state. No launcher atomics, native stable
// session, queue file or wall-clock retry enters speculative guest execution.
// Confirmation exposes completed actions to a future owned persistence sink.
class MagicCodes final {
public:
    static constexpr std::size_t kCheckpointBytes=64,kJournalBytes=24;
    bool start(std::uint64_t epoch,std::uint32_t canonical_selection,bool online);
    bool begin_epoch(std::uint64_t epoch);
    bool begin_frame(std::uint32_t frame);
    bool apply(std::uint32_t active,std::uint32_t unlocked,
               std::uint32_t& new_active,std::uint32_t& new_unlocked);
    bool complete(std::uint32_t action);
    bool frame_complete();
    bool end_frame(std::vector<std::uint8_t>& journal);
    bool capture(std::span<std::uint8_t> checkpoint) const;
    bool restore(std::span<const std::uint8_t> checkpoint);
    bool commit(std::uint64_t epoch,std::uint32_t frame,std::span<const std::uint8_t> journal);
    std::uint32_t confirmed_actions() const {return confirmed_actions_;}
private:
    bool valid(const magic_codes::MagicCodeSessionState& state) const;
    magic_codes::MagicCodeSessionState working_{},initial_{};
    std::uint64_t epoch_=0;
    std::uint32_t selection_=0,next_=0,confirmed_next_=0,actions_=0,confirmed_actions_=0;
    bool online_=false,open_=false;
};
}
