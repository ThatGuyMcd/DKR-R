#pragma once
#include <atomic>
#include <cstdint>
#include <string>

namespace dkr::runtime::invites {
enum class ReturnPhase { Idle, Stopping, Ready, Failed };
struct ReturnView {
    ReturnPhase phase = ReturnPhase::Idle;
    std::string error;
};
void game_running(bool value);
bool game_running();
bool begin_return();
bool return_pending();
// Main/event owner only, after runtime/renderer/save retirement. Never called
// from an ImGui callback or a network worker.
void finish_return(std::string error);
ReturnView return_view();
void clear_return();
void dialog(bool value);
bool dialog();
bool capture();
bool release_pending();
void fence_inputs();
void release_if_clear(bool clear);
void visible(std::uint64_t invite);
void focus_visible();
std::uint64_t take_focus();
void cancel_dialog();
bool take_cancel();
} // namespace dkr::runtime::invites
