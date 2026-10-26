#pragma once
#include "android_rotation_policy.hpp"
#include "imgui.h"
#include <vector>

namespace dkr::runtime::android_rotation {
// Called under Inspector::frameMutex, on the presentation thread only. Restore
// exact originals rather than inverse-transforming rounded floats, so repeated
// interpolated presentations cannot accumulate drift. Scratch capacities persist.
class ImGuiDrawScope final {
    struct Scratch {
        std::vector<ImVec2> positions;
        std::vector<ImVec4> clips;
    };
    inline static thread_local Scratch scratch;
    ImDrawData* data_ = nullptr;
    ImVec2 position_{}, size_{}, scale_{};

  public:
    ImGuiDrawScope(ImDrawData* data, std::uint32_t rotation) {
        if (!data || (rotation != rotate90 && rotation != rotate180 && rotation != rotate270) ||
            data->DisplaySize.x <= 0 || data->DisplaySize.y <= 0)
            return;
        std::size_t vertices = 0, commands = 0;
        for (int n = 0; n < data->CmdListsCount; ++n) {
            vertices += data->CmdLists[n]->VtxBuffer.Size;
            commands += data->CmdLists[n]->CmdBuffer.Size;
        }
        // Complete all possibly throwing allocations before mutating draw data.
        scratch.positions.resize(vertices);
        scratch.clips.resize(commands);
        data_ = data;
        position_ = data->DisplayPos;
        size_ = data->DisplaySize;
        scale_ = data->FramebufferScale;
        std::size_t v = 0, c = 0;
        for (int n = 0; n < data->CmdListsCount; ++n) {
            auto* list = data->CmdLists[n];
            for (auto& vertex : list->VtxBuffer) {
                scratch.positions[v++] = vertex.pos;
                const auto point =
                    to_image(Point{vertex.pos.x - position_.x, vertex.pos.y - position_.y}, size_.x, size_.y, rotation);
                vertex.pos = ImVec2(point.x, point.y);
            }
            for (auto& command : list->CmdBuffer) {
                scratch.clips[c++] = command.ClipRect;
                const auto r = command.ClipRect;
                const auto clip =
                    to_image(Rect{r.x - position_.x, r.y - position_.y, r.z - position_.x, r.w - position_.y}, size_.x,
                             size_.y, rotation);
                command.ClipRect = ImVec4(clip.left, clip.top, clip.right, clip.bottom);
            }
        }
        data->DisplayPos = ImVec2(0, 0);
        if (quarter_turn(rotation)) {
            data->DisplaySize = ImVec2(size_.y, size_.x);
            data->FramebufferScale = ImVec2(scale_.y, scale_.x);
        }
    }
    ~ImGuiDrawScope() {
        if (!data_)
            return;
        std::size_t v = 0, c = 0;
        for (int n = 0; n < data_->CmdListsCount; ++n) {
            for (auto& vertex : data_->CmdLists[n]->VtxBuffer)
                vertex.pos = scratch.positions[v++];
            for (auto& command : data_->CmdLists[n]->CmdBuffer)
                command.ClipRect = scratch.clips[c++];
        }
        data_->DisplayPos = position_;
        data_->DisplaySize = size_;
        data_->FramebufferScale = scale_;
    }
    ImGuiDrawScope(const ImGuiDrawScope&) = delete;
    ImGuiDrawScope& operator=(const ImGuiDrawScope&) = delete;
};
} // namespace dkr::runtime::android_rotation
