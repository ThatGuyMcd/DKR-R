#pragma once
#include "profile_policy.hpp"
#include <functional>
#include <map>
#include <mutex>
#include <utility>

namespace dkr::profile::bridge {
struct Card {
    std::string public_key, identity, friend_code;
};
struct Callbacks {
    std::function<Card()> card;
    std::function<std::string(std::string_view)> proof;
    std::function<bool(std::string_view, std::string_view, std::string_view)> verify;
};
struct State {
    std::mutex mutex;
    Callbacks callbacks;
    std::string own_avatar;
    std::map<std::string, std::string> peers;
};
inline State& state() {
    static State value;
    return value;
}
inline void bind(Callbacks callbacks) {
    auto& s = state();
    std::lock_guard lock(s.mutex);
    s.callbacks = std::move(callbacks);
    s.peers.clear();
    s.own_avatar.clear();
}
inline Card card() {
    std::function<Card()> call;
    {
        auto& s = state();
        std::lock_guard lock(s.mutex);
        call = s.callbacks.card;
    }
    return call ? call() : Card{};
}
inline std::string proof(std::string_view public_key) {
    std::function<std::string(std::string_view)> call;
    {
        auto& s = state();
        std::lock_guard lock(s.mutex);
        call = s.callbacks.proof;
    }
    return call ? call(public_key) : std::string{};
}
inline bool verify(std::string_view public_key, std::string_view identity, std::string_view code) {
    decltype(Callbacks::verify) call;
    {
        auto& s = state();
        std::lock_guard lock(s.mutex);
        call = s.callbacks.verify;
    }
    return call && call(public_key, identity, code);
}
inline void own_avatar(std::string value) {
    if (!shared_avatar(value))
        return;
    auto& s = state();
    std::lock_guard lock(s.mutex);
    s.own_avatar = std::move(value);
}
inline std::string own_avatar() {
    auto& s = state();
    std::lock_guard lock(s.mutex);
    return s.own_avatar;
}
inline void peer_avatar(std::string identity, std::string value) {
    if (!shared_avatar(value))
        return;
    auto& s = state();
    std::lock_guard lock(s.mutex);
    if (s.peers.size() >= 512 && !s.peers.contains(identity))
        return;
    s.peers[std::move(identity)] = std::move(value);
}
inline std::string peer_avatar(std::string_view identity) {
    auto& s = state();
    std::lock_guard lock(s.mutex);
    const auto found = s.peers.find(std::string(identity));
    return found == s.peers.end() ? std::string{} : found->second;
}
} // namespace dkr::profile::bridge
