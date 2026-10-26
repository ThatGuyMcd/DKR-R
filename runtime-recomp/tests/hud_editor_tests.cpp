// Headless execution of the real ImGui workshop; no game/window/save is opened.
#include "../src/game/hud_layout_editor.cpp"
#include <cassert>
#include <cstdio>
#include <map>
#include "generated/racing_banana_font.h"
#include "generated/jumpman_font.h"
#include "backends/imgui_impl_sdlrenderer2.h"
#define SDL_MAIN_HANDLED
#include <SDL.h>

namespace dkr::runtime::hud {
namespace {
groups::Layout saved;
std::optional<groups::Layout> live;
std::map<std::string, groups::Layout> presets;
bool fail_save = false;
} // namespace
groups::Layout layout() {
    return saved;
}
bool apply_layout(const groups::Layout& value) {
    if (fail_save || !groups::valid(value))
        return false;
    saved = value;
    live.reset();
    return true;
}
void preview_layout(const std::optional<groups::Layout>& value) {
    live = value;
}
std::vector<std::string> preset_names() {
    std::vector<std::string> names;
    for (const auto& p : presets)
        names.push_back(p.first);
    return names;
}
bool save_preset(const std::string& name, const groups::Layout& value) {
    presets[name] = value;
    return true;
}
std::optional<groups::Layout> load_preset(const std::string& name) {
    return presets.contains(name) ? std::optional(presets[name]) : std::nullopt;
}
bool delete_preset(const std::string& name) {
    return presets.erase(name) != 0;
}
} // namespace dkr::runtime::hud
int main(int argc, char** argv) {
    namespace ed = dkr::runtime::hud::editor;
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    static const ImWchar letters[]{0x20, 0x2F, 0x3A, 0xFF, 0}, digits[]{0x30, 0x39, 0};
    ImFontConfig font{};
    font.FontDataOwnedByAtlas = false;
    io.Fonts->AddFontFromMemoryTTF(const_cast<char*>(dkr_racing_banana_font), int(dkr_racing_banana_font_size), 24,
                                   &font, letters);
    font.MergeMode = true;
    io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(dkr_jumpman_font), int(dkr_jumpman_font_size), 24 * 1.42F,
                                   &font, digits);
    ImFontConfig fallback{};
    fallback.MergeMode = true;
    fallback.SizePixels = 24;
    fallback.GlyphRanges = letters;
    io.Fonts->AddFontDefault(&fallback);
    unsigned char* pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    SDL_Surface* surface = nullptr;
    SDL_Renderer* renderer = nullptr;
    if (argc > 1) {
        surface = SDL_CreateRGBSurfaceWithFormat(0, 1280, 800, 32, SDL_PIXELFORMAT_RGBA32);
        assert(surface);
        renderer = SDL_CreateSoftwareRenderer(surface);
        assert(renderer);
        ImGui_ImplSDLRenderer2_Init(renderer);
        ImGui_ImplSDLRenderer2_NewFrame();
    }
    for (const auto size : {ImVec2{1280, 800}, ImVec2{1920, 1080}, ImVec2{1024, 768}}) {
        io.DisplaySize = size;
        for (int frame = 0; frame < 8; ++frame) {
            ImGui::NewFrame();
            ImGui::SetNextWindowPos({0, 0});
            ImGui::SetNextWindowSize(size);
            ImGui::Begin("test", nullptr, ImGuiWindowFlags_NoSavedSettings);
            if (frame == 0) {
                ed::e = ed::Editor{};
                ed::e.opened = true;
                ed::e.draft.mode = dkr::runtime::hud::LayoutMode::Custom;
                ImGui::OpenPopup("HUD WORKSHOP");
            }
            ed::e.details = frame >= 4;
            ed::e.target = frame % 4;
            ed::modal(nullptr, nullptr);
            ImGui::End();
            ImGui::Render();
            const auto* data = ImGui::GetDrawData();
            assert(data && data->Valid && data->TotalVtxCount > 0);
            for (const auto* list : data->CmdLists)
                for (const auto& v : list->VtxBuffer) {
                    assert(std::isfinite(v.pos.x));
                    assert(std::isfinite(v.pos.y));
                }
            if (renderer && size.x == 1280 && frame == 6) {
                SDL_SetRenderDrawColor(renderer, 8, 25, 38, 255);
                SDL_RenderClear(renderer);
                ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData());
                SDL_RenderPresent(renderer);
                assert(SDL_SaveBMP(surface, argv[1]) == 0);
            }
        }
    }
    ed::e = ed::Editor{};
    ed::e.draft.mode = dkr::runtime::hud::LayoutMode::Custom;
    const auto original = ed::e.draft;
    ed::change([] { ed::e.draft.scale = 1.25F; });
    assert(ed::e.undo.size() == 1);
    ed::undo(false);
    assert(ed::e.draft == original);
    ed::undo(true);
    assert(ed::e.draft.scale == 1.25F);
    ed::start_move();
    ed::nudge(10, 5);
    ed::finish_move(false);
    assert(ed::e.draft.scale == 1.25F);
    assert(ed::selected().x == 0);
    ed::e.opened = true;
    ed::accept_preset_name("Deck");
    assert(dkr::runtime::hud::preset_names().size() == 1);
    ed::cancel();
    assert(!dkr::runtime::hud::live);
    const auto applied = dkr::runtime::hud::saved;
    dkr::runtime::hud::preview_layout(ed::e.draft);
    dkr::runtime::hud::fail_save = true;
    assert(!ed::apply_settings(ed::e.draft));
    assert(!dkr::runtime::hud::live && dkr::runtime::hud::saved == applied);
    assert(!ed::g_settings_message.empty());
    dkr::runtime::hud::fail_save = false;
    assert(ed::apply_settings(ed::e.draft));
    assert(ed::g_settings_message.empty());
    if (renderer) {
        ImGui_ImplSDLRenderer2_Shutdown();
        SDL_DestroyRenderer(renderer);
        SDL_FreeSurface(surface);
    }
    ImGui::DestroyContext();
    std::puts("HUD workshop headless layout/state tests passed");
}
