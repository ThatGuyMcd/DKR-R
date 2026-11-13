#pragma once
#include <atomic>
#include <array>
#include <filesystem>
#include <memory>
#include <string_view>

// Existing first-party owner API. The retained UI capsule predates its header
// declaration; the implementation is shared with the running game.
namespace dkr::mods {struct PreparedModLaunch;}
namespace dkr::runtime::legacy {std::shared_ptr<const mods::PreparedModLaunch> prepared_launch();}

namespace dkr::library_ui {
enum class SidebarInput { Shoulders, Dpad };
inline std::atomic<SidebarInput> sidebar_input{SidebarInput::Shoulders};
inline std::atomic<bool> sidebar_toggle_requested{false};
inline std::atomic<bool> sidebar_input_blocked{true};
inline std::atomic<unsigned> navigation_buttons{0};
inline std::atomic<unsigned> navigation_press_edges{0};
// Polled before ImGui can deduplicate physical inputs against routed UI keys.
inline std::array<std::atomic<float>,4> navigation_axes{};
inline std::atomic<int> navigation_connected{-1};
inline void record_navigation_axis(unsigned index,float value) {
    navigation_axes[index].store(value,std::memory_order_relaxed);
}
inline void record_navigation_press(unsigned index) {
    navigation_press_edges.fetch_or(1U<<index,std::memory_order_relaxed);
}
inline void record_navigation_button(unsigned index,bool down) {
    if(down)navigation_buttons.fetch_or(1U<<index,std::memory_order_relaxed);
    else navigation_buttons.fetch_and(~(1U<<index),std::memory_order_relaxed);
}
inline constexpr int SidebarToggle = 13;
inline constexpr float NavigationToggleHeight = 64.F;
// A newly opened surface/mode never acts on a direction already held down.
struct NavigationRepeat {
    bool armed=false;
    int held=0;
    double next=0;
    int poll(bool previous,bool following,bool blocked,double now,bool allow_press=true) {
        if(blocked){armed=false;held=0;return 0;}
        if(!previous&&!following){armed=true;held=0;return 0;}
        if(!armed||previous==following)return 0;
        const int direction=previous?-1:1;
        if(direction!=held){if(!allow_press)return 0;held=direction;next=now+.38;return direction;}
        if(now<next)return 0;
        next=now+.12;return direction;
    }
};
enum class SaveScope { Unresolved, Vanilla, Modded, Online };
inline SaveScope running_save_scope(const std::filesystem::path& root,
        const std::filesystem::path& path,bool online) {
    if(path.empty())return SaveScope::Unresolved;
    if(online)return SaveScope::Online;
    return path.lexically_normal()==(root/"saves/dkr.us.v77.bin").lexically_normal()
        ?SaveScope::Vanilla:SaveScope::Modded;
}
inline constexpr std::string_view save_label(SaveScope scope) {
    switch(scope) {
    case SaveScope::Vanilla:return "Vanilla save";
    case SaveScope::Modded:return "Modded game save";
    case SaveScope::Online:return "Online play save";
    default:return "Save selected at launch";
    }
}
} // namespace dkr::library_ui
