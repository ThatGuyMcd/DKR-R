#pragma once

#include "netplay/experimental_draw_events.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <span>
#include <string>

namespace dkr::runtime::online_view {
enum class Layout : unsigned { SplitScreen, MyRacer };
enum class Labels : unsigned { NameAndPosition, Position, Off };
// Device preferences only. Never read by a guest adapter, input broker,
// checkpoint, mod identity or lobby rules serializer.
inline std::atomic<Layout> requested_layout{Layout::SplitScreen};
inline std::atomic<Labels> requested_labels{Labels::NameAndPosition};
inline std::atomic<unsigned> requested_label_size{100};
struct Session {
    std::array<std::string, 4> names;
    unsigned owner = 4, players = 0;
    bool valid() const {
        return players >= 2 && players <= 4 && owner < players;
    }
};
struct Plan {
    bool eligible = false, focus = false;
    unsigned owner = 4, layout = 0;
    Labels labels = Labels::Off;
    unsigned label_size = 100;
};
inline Plan plan(const Session& session, std::span<const dkr_owned_draw_event> events) {
    Plan p;
    if (!session.valid())
        return p;
    unsigned mask = 0, seen = 0, layout = 0;
    bool shared = false;
    for (const auto& e : events) {
        if (e.kind == DKR_OWNED_ONLINE_SHARED)
            shared = true;
        if (e.kind != DKR_OWNED_ONLINE_VIEW)
            continue;
        if (!dkr_owned_draw_event_valid(&e) || e.parameters[0] < 1 || e.parameters[0] > 3 || e.parameters[1] != 0 ||
            (e.parameters[2] != 0 && e.parameters[2] != 3) || e.parameters[3] != 0 ||
            e.parameters[4] != session.players)
            return p;
        if (layout && layout != e.parameters[0])
            return p;
        layout = e.parameters[0];
        if (e.token < session.players) {
            if (mask & (1U << e.token))
                return p;
            mask |= 1U << e.token;
            ++seen;
        } else if (session.players != 3 || e.token != 3)
            return p; // Original T.T. camera.
    }
    if (!shared || seen != session.players || mask != ((1U << session.players) - 1) || layout != (session.players - 1))
        return p;
    p.eligible = true;
    p.owner = session.owner;
    p.layout = layout;
    p.focus = requested_layout.load(std::memory_order_relaxed) == Layout::MyRacer;
    p.labels = requested_labels.load(std::memory_order_relaxed);
    p.label_size = std::clamp(requested_label_size.load(std::memory_order_relaxed), 75U, 150U);
    return p;
}
} // namespace dkr::runtime::online_view
