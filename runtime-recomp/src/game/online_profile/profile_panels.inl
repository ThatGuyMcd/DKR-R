struct OlEqualProfileRow;
OlEqualProfileRow* g_equal_profile_row = nullptr;

// Capture only the outer card of each section. Nested friend-code cards keep
// their own backgrounds. Paint the outer backgrounds once the real
// content height is known, without drawing interactive controls twice.
struct OlEqualProfileRow {
    struct Panel {
        ImVec2 a{}, b{};
        OlPanelLook look;
        int section = 0;
    };
    std::array<Panel, 3> panels;
    std::size_t count = 0;
    bool capture_next = false;
    int section = 0;
    ImDrawList* draw = ImGui::GetWindowDrawList();
    ImDrawListSplitter layers;
    OlEqualProfileRow* previous = g_equal_profile_row;

    OlEqualProfileRow() {
        layers.Split(draw, 2);
        layers.SetCurrentChannel(draw, 1);
        g_equal_profile_row = this;
    }
    ~OlEqualProfileRow() {
        g_equal_profile_row = previous;
        float bottom = 0;
        for (std::size_t n = 0; n < count; ++n)
            bottom = std::max(bottom, panels[n].b.y);
        layers.SetCurrentChannel(draw, 0);
        for (std::size_t n = 0; n < count; ++n) {
            const auto& panel = panels[n];
            const OlEnter enter(panel.section);
            PaintOlPanel(draw, panel.a, {panel.b.x, bottom}, panel.look);
        }
        layers.Merge(draw);
    }
};
