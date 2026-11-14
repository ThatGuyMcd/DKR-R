#include "android_imgui_rotation.hpp"
#include <cstdio>
#include <cstring>
#include <stdexcept>
#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        if (!(x))                                                                                                      \
            throw std::runtime_error(#x);                                                                              \
    } while (false)
int main() {
    using namespace dkr::runtime::android_rotation;
    ImGui::CreateContext();
    {
        ImDrawList list(ImGui::GetDrawListSharedData());
        ImDrawVert original{};
        original.pos = ImVec2(111.125f, 222.25f);
        original.uv = ImVec2(0.2f, 0.8f);
        original.col = 0x12345678;
        list.VtxBuffer.push_back(original);
        ImDrawCmd originalCommand{};
        originalCommand.ClipRect = ImVec4(20, 30, 200, 300);
        list.CmdBuffer.push_back(originalCommand);
        ImDrawData data;
        data.DisplayPos = ImVec2(10, 20);
        data.DisplaySize = ImVec2(960, 540);
        data.FramebufferScale = ImVec2(2, 3);
        data.CmdLists.push_back(&list);
        data.CmdListsCount = 1;
        data.TotalVtxCount = 1;
        for (const auto rotation : {identity, rotate90, rotate180, rotate270}) {
            for (unsigned repeat = 0; repeat < 2000; ++repeat) {
                {
                    ImGuiDrawScope scope(&data, rotation);
                    CHECK(std::memcmp(&list.VtxBuffer[0].uv, &original.uv, sizeof(ImVec2)) == 0 &&
                          list.VtxBuffer[0].col == original.col);
                    if (rotation == rotate90) {
                        CHECK(list.VtxBuffer[0].pos.x == 337.75f && list.VtxBuffer[0].pos.y == 101.125f);
                        CHECK(data.DisplaySize.x == 540 && data.DisplaySize.y == 960);
                        CHECK(data.FramebufferScale.x == 3 && data.FramebufferScale.y == 2);
                    }
                }
                CHECK(std::memcmp(&list.VtxBuffer[0], &original, sizeof(original)) == 0);
                CHECK(std::memcmp(&list.CmdBuffer[0].ClipRect, &originalCommand.ClipRect, sizeof(ImVec4)) == 0);
                CHECK(data.DisplayPos.x == 10 && data.DisplayPos.y == 20 && data.DisplaySize.x == 960 &&
                      data.DisplaySize.y == 540);
                CHECK(data.FramebufferScale.x == 2 && data.FramebufferScale.y == 3);
            }
        }
        try {
            ImGuiDrawScope scope(&data, rotate270);
            throw std::runtime_error("unwind");
        } catch (...) {
        }
        CHECK(std::memcmp(&list.VtxBuffer[0], &original, sizeof(original)) == 0);
    }
    ImGui::DestroyContext();
    std::puts(
        "Android overlay rotation: exact restoration, repeated presentations, non-uniform DPI, UV/color preservation and exception unwind passed.");
}
