#pragma once
// Bookkeeping for the live state cross-check of lobbies that span CPU
// architectures (docs/CROSS-ARCHITECTURE.md). Every racer reports the hash of
// its confirmed state every kStateCheckEvery frames; the host compares each
// report with its own hash of the same frame. Nothing here touches the
// network or the simulation, so the rules can be tested on their own.
#include "netplay_types.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <unordered_set>
#include <vector>

namespace dkr::runtime::netplay {

inline constexpr std::uint32_t kStateCheckEvery = 30U;

// True for a launch the host sent in its architecture-free form, which it only
// does when a peer from another architecture is in the lobby.
inline bool lobby_spans_architectures(std::uint64_t launch_hash, const CompatibilityManifest& own) {
    return launch_hash == manifest_class_hash(own) && launch_hash != manifest_hash(own);
}

// The host remembers which admitted senders are on another architecture so it can
// pick the launch hash. Keep only the ones still in the lobby (members, peers or
// pending joins), so the set cannot grow with joins and leaves and never loses a
// peer that is present.
inline void prune_other_architecture_senders(std::unordered_set<std::uint64_t>& senders,
                                             const std::set<std::uint64_t>& present) {
    std::erase_if(senders, [&](std::uint64_t id) { return !present.contains(id); });
}

struct StateMismatch {
    std::uint64_t epoch = 0U;
    std::uint32_t frame = 0U;
    std::uint64_t peer = 0U;
    std::uint64_t local = 0U;
    std::uint64_t remote = 0U;
};

class StateCrossCheck {
  public:
    enum class Verdict { Accepted, Duplicate, Rejected, Mismatch };
    // At most `capacity` sampled frames are kept; the oldest is dropped first.
    explicit StateCrossCheck(std::size_t capacity = 16U) : capacity_(capacity < 2U ? 2U : capacity) {}

    // The racers whose reports are expected (not including this peer).
    void set_peers(std::set<std::uint64_t> peers) {
        peers_ = std::move(peers);
    }
    // Own hash for a sampled frame. A report that arrived first is compared now.
    Verdict local(std::uint64_t epoch, std::uint32_t frame, std::uint64_t hash, StateMismatch* mismatch = nullptr) {
        if (!valid(epoch, frame))
            return Verdict::Rejected;
        Entry* entry = find_or_make(epoch, frame);
        if (!entry)
            return Verdict::Rejected;
        if (entry->local)
            return *entry->local == hash ? Verdict::Duplicate : Verdict::Rejected;
        entry->local = hash;
        for (const auto& [peer, remote] : entry->reports)
            if (remote != hash)
                return fill(mismatch, epoch, frame, peer, hash, remote);
        finish(epoch, frame);
        return Verdict::Accepted;
    }
    // A peer's report. Reports from anyone not in the peer set, malformed
    // frames, a second differing report from one peer, and reports older than
    // everything retained are rejected and change nothing.
    Verdict report(std::uint64_t peer, std::uint64_t epoch, std::uint32_t frame, std::uint64_t hash,
                   StateMismatch* mismatch = nullptr) {
        if (!peers_.contains(peer) || !valid(epoch, frame))
            return Verdict::Rejected;
        Entry* entry = find_or_make(epoch, frame);
        if (!entry)
            return Verdict::Rejected;
        if (const auto at = entry->reports.find(peer); at != entry->reports.end())
            return at->second == hash ? Verdict::Duplicate : Verdict::Rejected;
        entry->reports.emplace(peer, hash);
        if (entry->local && *entry->local != hash)
            return fill(mismatch, epoch, frame, peer, *entry->local, hash);
        finish(epoch, frame);
        return Verdict::Accepted;
    }
    // Peers that let a sampled frame fall out of the window without reporting
    // it, each returned once, so the caller can say so once and not stall.
    std::vector<std::uint64_t> take_silent() {
        std::vector<std::uint64_t> result;
        result.swap(silent_new_);
        return result;
    }
    std::size_t pending() const {
        return entries_.size();
    }
    std::uint64_t verified_frames() const {
        return verified_;
    }

  private:
    struct Entry {
        std::optional<std::uint64_t> local;
        std::map<std::uint64_t, std::uint64_t> reports;
    };
    using Key = std::pair<std::uint64_t, std::uint32_t>;

    static bool valid(std::uint64_t epoch, std::uint32_t frame) {
        return epoch != 0U && frame != 0U && frame % kStateCheckEvery == 0U;
    }
    Verdict fill(StateMismatch* out, std::uint64_t epoch, std::uint32_t frame, std::uint64_t peer, std::uint64_t local,
                 std::uint64_t remote) {
        if (out)
            *out = {epoch, frame, peer, local, remote};
        return Verdict::Mismatch;
    }
    Entry* find_or_make(std::uint64_t epoch, std::uint32_t frame) {
        const Key key{epoch, frame};
        if (const auto at = entries_.find(key); at != entries_.end())
            return &at->second;
        if (entries_.size() >= capacity_) {
            // Nothing older than what is retained is worth keeping.
            if (key < entries_.begin()->first)
                return nullptr;
            drop_oldest();
        }
        return &entries_[key];
    }
    void drop_oldest() {
        const auto oldest = entries_.begin();
        // Only a frame this peer sampled counts: a stray late report must not
        // make an honest peer look silent.
        if (oldest->second.local)
            for (const auto peer : peers_)
                if (!oldest->second.reports.contains(peer) && silent_.insert(peer).second)
                    silent_new_.push_back(peer);
        entries_.erase(oldest);
    }
    void finish(std::uint64_t epoch, std::uint32_t frame) {
        const auto at = entries_.find({epoch, frame});
        if (at == entries_.end() || !at->second.local || at->second.reports.size() < peers_.size())
            return;
        ++verified_;
        entries_.erase(at);
    }

    std::size_t capacity_;
    std::set<std::uint64_t> peers_;
    std::map<Key, Entry> entries_;
    std::set<std::uint64_t> silent_;
    std::vector<std::uint64_t> silent_new_;
    std::uint64_t verified_ = 0U;
};

} // namespace dkr::runtime::netplay
