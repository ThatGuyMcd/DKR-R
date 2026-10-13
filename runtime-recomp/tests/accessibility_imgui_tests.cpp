#include "runtime_accessibility_imgui.hpp"
#include <cassert>
#include <cstdio>

using namespace dkr::runtime::accessibility;
int main() {
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = {640, 480};
    io.DeltaTime = 1.0F / 60.0F;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    unsigned char* pixels; int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    HostTree tree;
    ImGuiID first_id = 0, second_id = 0;
    bool first_pressed = false, second_pressed = false;
    auto frame = [&](const char* scope = "launcher/0", bool second_only = false) {
        ImGui::NewFrame();
        tree.begin_frame(scope, "Launcher", std::string_view(scope).starts_with("modal/"));
        if (!tree.has_pending_focus()) {
            const auto active = GImGui->ActiveId;
            const auto activate = GImGui->NavActivateId;
            const auto down = GImGui->NavActivateDownId;
            const auto pressed = GImGui->NavActivatePressedId;
            GImGui->ActiveId = 0x1234;
            GImGui->NavActivateId = GImGui->NavActivateDownId = GImGui->NavActivatePressedId = 0x4321;
            prepare_imgui_focus(tree);
            assert(GImGui->ActiveId == 0x1234);
            assert(GImGui->NavActivateId == 0x4321 && GImGui->NavActivateDownId == 0x4321 &&
                GImGui->NavActivatePressedId == 0x4321);
            GImGui->ActiveId = active;
            GImGui->NavActivateId = activate;
            GImGui->NavActivateDownId = down;
            GImGui->NavActivatePressedId = pressed;
        } else prepare_imgui_focus(tree);
        ImGui::SetNextWindowPos({0, 0});
        ImGui::SetNextWindowSize({640, 480});
        ImGui::Begin("Launcher", nullptr, ImGuiWindowFlags_NoSavedSettings);
        first_pressed = false;
        if (!second_only) {
            first_pressed = ImGui::Button("First", {120, 40});
            first_id = ImGui::GetItemID();
            tree.add_button("first", "First", {10, 10, 120, 40}, true, false, ImGui::IsItemFocused());
            apply_imgui_focus(tree, "first");
        }
        second_pressed = ImGui::Button("Second", {120, 40});
        second_id = ImGui::GetItemID();
        tree.add_button("second", "Second", {10, 60, 120, 40}, true, false, ImGui::IsItemFocused());
        apply_imgui_focus(tree, "second");
        ImGui::End();
        tree.end_frame();
        ImGui::Render();
    };
    frame(); frame();
    assert(!tree.has_pending_focus() && !tree.has_accessibility_focus("first"));
    GImGui->NavId = first_id;
    const auto snapshot = tree.snapshot();
    assert(tree.enqueue({snapshot.generation, snapshot.scope, "second", ActionKind::Focus}));
    // Enter and an AX focus request arriving together cannot activate First.
    io.AddKeyEvent(ImGuiKey_Enter, true);
    frame();
    assert(!first_pressed && !second_pressed);
    assert(GImGui->NavId == second_id);
    assert(!GImGui->NavDisableHighlight);
    io.AddKeyEvent(ImGuiKey_Enter, false);
    frame();
    io.AddKeyEvent(ImGuiKey_Enter, true);
    frame();
    assert(!first_pressed && second_pressed);
    io.AddKeyEvent(ImGuiKey_Enter, false);
    frame();
    // A later keyboard focus change is not overwritten by repeated redraws.
    GImGui->NavId = first_id;
    frame();
    assert(GImGui->NavId == first_id);
    // Semantic defaults do not schedule real input focus, including modal entry.
    frame("modal/quit", true);
    assert(!tree.has_pending_focus());
    assert(!tree.has_accessibility_focus("second"));
    auto modal = tree.snapshot();
    assert(tree.enqueue({modal.generation, modal.scope, "second", ActionKind::Focus}));
    frame("modal/quit", true);
    assert(GImGui->NavId == second_id && tree.has_accessibility_focus("second"));
    frame("launcher/0");
    assert(!tree.has_pending_focus()); // original target was keyboard-owned
    // Explicit AX focus is restored after a modal, once.
    auto launcher = tree.snapshot();
    assert(tree.enqueue({launcher.generation, launcher.scope, "first", ActionKind::Focus}));
    frame();
    frame("modal/quit", true);
    frame("launcher/0");
    assert(GImGui->NavId == first_id && tree.has_accessibility_focus("first"));
    frame();
    assert(!tree.has_pending_focus());
    // Offscreen controls remain in the semantic tree and an explicit AX
    // focus request scrolls their real owning ImGui child into view.
    HostTree scroll_tree;
    bool far_visible = false;
    float scroll_y = 0;
    auto scroll_frame = [&] {
        ImGui::NewFrame();
        scroll_tree.begin_frame("settings", "Settings");
        prepare_imgui_focus(scroll_tree);
        ImGui::SetNextWindowPos({0, 0});
        ImGui::SetNextWindowSize({640, 480});
        ImGui::Begin("Scrollable settings", nullptr, ImGuiWindowFlags_NoSavedSettings);
        ImGui::BeginChild("settings-content", {300, 160}, true);
        ImGui::Button("Visible setting", {200, 40});
        scroll_tree.add_button("visible", "Visible setting", {10, 10, 200, 40});
        apply_imgui_focus(scroll_tree, "visible");
        ImGui::Dummy({1, 650});
        ImGui::InvisibleButton("##far-setting", {200, 44});
        far_visible = ImGui::IsItemVisible();
        const auto min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
        Node far;
        far.id = "far"; far.label = "Offscreen setting"; far.role = Role::Checkbox;
        far.bounds = {min.x, min.y, max.x - min.x, max.y - min.y};
        scroll_tree.add_control(far);
        apply_imgui_focus(scroll_tree, "far");
        scroll_y = ImGui::GetScrollY();
        ImGui::EndChild();
        ImGui::End();
        scroll_tree.end_frame();
        ImGui::Render();
    };
    scroll_frame(); scroll_frame();
    assert(!far_visible && scroll_y == 0);
    assert(scroll_tree.snapshot().nodes.size() == 2);
    assert(!scroll_tree.has_pending_focus()); // no default input/scroll change
    const auto offscreen = scroll_tree.snapshot();
    assert(scroll_tree.enqueue({offscreen.generation, offscreen.scope, "far", ActionKind::Focus}));
    scroll_frame(); scroll_frame();
    assert(far_visible && scroll_y > 0);
    assert(scroll_tree.has_accessibility_focus("far"));

    // Native replacement survives an active ImGui editor, then ordinary
    // keyboard text continues editing that replacement without a second focus.
    HostTree edit_tree;
    char text[32] = "Original";
    ImGuiID editor_id = 0;
    bool press_character = false;
    const auto edit_frame = [&] {
        ImGui::NewFrame();
        edit_tree.begin_frame("modal/text", "Enter text", true);
        prepare_imgui_focus(edit_tree);
        ImGui::Begin("Text editor", nullptr, ImGuiWindowFlags_NoSavedSettings);
        ImGui::InputText("Preset name", text, sizeof(text));
        editor_id = ImGui::GetItemID();
        Node editor;
        editor.id = "name"; editor.label = "Preset name"; editor.role = Role::TextField;
        editor.value = text; editor.text_capacity = sizeof(text) - 1;
        const auto request = edit_tree.add_control(editor);
        apply_imgui_focus(edit_tree, "name", true);
        apply_imgui_text_action(request, text, sizeof(text));
        if (press_character) {
            std::strcat(text, "A");
            reload_imgui_text_editor(editor_id);
            press_character = false;
        }
        ImGui::End();
        edit_tree.end_frame();
        ImGui::Render();
    };
    edit_frame(); edit_frame();
    auto edit_snapshot = edit_tree.snapshot();
    assert(edit_tree.enqueue({edit_snapshot.generation, edit_snapshot.scope, "name", ActionKind::Focus}));
    edit_frame(); edit_frame();
    assert(GImGui->ActiveId == editor_id);
    edit_snapshot = edit_tree.snapshot();
    Action replace{edit_snapshot.generation, edit_snapshot.scope, "name", ActionKind::SetText};
    replace.text = "Taj";
    assert(edit_tree.enqueue(replace));
    edit_frame(); edit_frame();
    assert(std::string(text) == "Taj" && GImGui->ActiveId == editor_id);
    io.AddInputCharactersUTF8("!");
    edit_frame();
    assert(std::string(text) == "Taj!");
    edit_frame();
    assert(std::string(text) == "Taj!");
    press_character = true;
    edit_frame(); edit_frame();
    assert(std::string(text) == "Taj!A" && GImGui->ActiveId == editor_id);
    io.AddKeyEvent(ImGuiKey_Escape, true);
    edit_frame();
    io.AddKeyEvent(ImGuiKey_Escape, false);
    edit_frame();
    assert(GImGui->ActiveId != editor_id); // ordinary editor exit still works

    ImGui::DestroyContext();
    std::puts("PASS: AX focus, offscreen scrolling and active text replacement preserve ordinary keyboard input");
}
