#include "imgui_internal.h"
#include "localisation/imgui_ui.hpp"
#include <cassert>
#include <iostream>
#include <vector>
#include "localisation/font_ui.inl"
#include "localisation/tracked_text_ui.inl"

int main() {
    namespace ui = dkr::localisation;
    std::string draft = "Play";
    ImFont* previous_font = nullptr;
    for (int surface = 0; surface != 2; ++surface) {
        auto* context = ImGui::CreateContext();
        auto& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = {1280, 800};
        io.DeltaTime = 1.F / 60;
        auto* font = io.Fonts->AddFontDefault();
        MergeLocalisationFonts(io.Fonts);
        unsigned char* pixels = nullptr;
        int width = 0, height = 0;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        assert(pixels && width > 0 && height > 0);
        const auto glyph_ranges = ui::fallback_ranges();
        for (std::size_t i = 0; glyph_ranges[i]; i += 2)
            for (unsigned code = glyph_ranges[i]; code <= glyph_ranges[i + 1]; ++code)
                assert(font->FindGlyphNoFallback(ImWchar(code)));
        io.Fonts->SetTexID(reinterpret_cast<ImTextureID>(std::uintptr_t(1234)));
        const auto texture = io.Fonts->TexID;
        const auto configs = io.Fonts->ConfigData.Size;
        for (const auto& language : ui::languages()) {
            assert(ui::select(language.code));
            const auto* current_font = font;
            ImGui::NewFrame();
            ImGui::Begin("shared-page");
            const auto expected = ImGui::GetID("###Play##launch");
            ImGui::UiButton("Play##launch", {200, 44});
            assert(ImGui::GetItemID() == expected);
            ImGui::UiButton(draft.c_str());
            assert(ImGui::GetItemID() == ImGui::GetID("Play"));
            char user_buffer[] = "Play";
            ImGui::PushID("mutable-buffer");
            ImGui::UiButton(user_buffer);
            assert(ImGui::GetItemID() == ImGui::GetID("Play"));
            ImGui::PopID();
            char input[] = "Unchanged draft %s {name}";
            ImGui::UiInputText("Name##user-input", input, sizeof(input));
            assert(ImGui::GetItemID() == ImGui::GetID("###Name##user-input"));
            assert(std::string(input) == "Unchanged draft %s {name}");
            const auto popup_id = ImGui::GetID("Settings##dialog");
            ImGui::OpenPopup("Settings##dialog");
            bool popup_open = true;
            if (ImGui::UiBeginPopupModal("Settings##dialog", &popup_open)) {
                assert(ImGui::GetCurrentWindow()->PopupId == popup_id);
                ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
            }
            ImGui::UiTextUnformatted("Language");
            ImGui::UiText("100% %s %n"); // prose is data when there are no format arguments
            const auto text = ui::text("nav.controls");
            float measured = 0;
            for (auto* cursor = text.data(); cursor < text.data() + text.size();) {
                unsigned character = 0;
                const auto size = ImTextCharFromUtf8(&character, cursor, text.data() + text.size());
                assert(size > 0);
                assert(font->FindGlyphNoFallback(ImWchar(character)));
                measured += font->FindGlyph(ImWchar(character))->AdvanceX;
                cursor += size;
            }
            assert(std::abs(TrackedTextWidth(font, font->FontSize, text.data(), 0) - measured) < .01F);
            AddTrackedText(ImGui::GetWindowDrawList(), font, font->FontSize, {10, 100}, 0xFFFFFFFF, text.data(), 0);
            ImGui::End();
            ImGui::Render();
            assert(font == current_font && io.Fonts->TexID == texture);
            assert(configs == io.Fonts->ConfigData.Size && draft == "Play");
        }
        char live_draft[128] = "Keep this draft %s {name}";
        ImGuiID editor_id = 0;
        for (std::size_t step = 0; step < ui::languages().size() + 2; ++step) {
            assert(ui::select(ui::languages()[step % ui::languages().size()].code));
            ImGui::NewFrame();
            ImGui::SetNextWindowSize({600, 300});
            ImGui::Begin("active-editor");
            if (step == 0)
                ImGui::SetKeyboardFocusHere();
            ImGui::UiInputText("Name##live-editor", live_draft, sizeof(live_draft));
            if (step == 0)
                editor_id = ImGui::GetItemID();
            assert(ImGui::GetItemID() == editor_id);
            if (step > 0)
                assert(ImGui::GetActiveID() == editor_id);
            assert(std::string(live_draft) == "Keep this draft %s {name}");
            ImGui::End();
            ImGui::Render();
        }
        previous_font = font;
        (void)previous_font;
        ImGui::DestroyContext(context);
    }
    std::cout
        << "Both contexts: UTF-8 glyphs, stable widget IDs, literal-only translation, retained drafts and immutable font atlas passed\n";
}
