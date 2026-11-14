#pragma once
#include "storage/maintenance.hpp"
#include <cstdint>
#include <filesystem>
#include <functional>
#include <span>
#include <stop_token>
#include <string>
#include <vector>

namespace dkr::crash_reports {
inline constexpr std::size_t MaxUpload = 8 * 1024 * 1024;
inline constexpr std::size_t MaxQueue = 8;
inline constexpr std::int64_t RetentionMs = 30LL * 24 * 3600 * 1000;
struct Consent {
    int choice = -1; // unknown / declined / allowed; no upload before allowed
    bool dumps = false;
    std::int64_t since = 0;
};
struct Delivery {
    unsigned status = 0;
    unsigned retry_seconds = 0;
    bool confirmed = false;
};
struct Pending {
    std::string id;
    std::vector<std::uint8_t> bytes;
};
struct View {
    Consent consent;
    std::size_t queued = 0;
    bool sending = false;
    std::string status;
};
std::int64_t now_ms();
std::string redact(std::string, std::span<const std::string> identities = {});
Consent read_consent(const std::filesystem::path& root);
void write_consent(const std::filesystem::path& root, Consent);
// Single-writer queue. The service holds an OS lock around each cycle,
// including delivery, so two launcher processes cannot send the same entry.
class Queue {
    std::filesystem::path profile_, root_;
    std::string version_;
    std::vector<std::string> identities_;

  public:
    Queue(std::filesystem::path profile, std::string version, std::vector<std::string> identities = {});
    const std::filesystem::path& directory() const {
        return root_;
    }
    void reconcile(Consent, std::int64_t now);
    void discover(Consent, std::int64_t now);
    Pending due(std::int64_t now) const;
    void delivered(const std::string& id, Delivery, std::int64_t now);
    std::size_t count() const;
    bool blocked() const;
};
using Transport = std::function<Delivery(const Pending&, std::stop_token)>;
Delivery upload(const Pending&, std::stop_token);
// Process-owned service. Called after log rotation; joined before TLS teardown.
void start(const std::filesystem::path& profile, std::string version);
void shutdown();
View view();
bool choose(bool enabled, bool include_dumps);
void gameplay(bool active);
struct Gameplay {
    Gameplay() {
        gameplay(true);
    }
    ~Gameplay() {
        gameplay(false);
    }
};
} // namespace dkr::crash_reports
