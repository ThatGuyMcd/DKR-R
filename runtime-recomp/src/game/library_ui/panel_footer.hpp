#pragma once

// Included after ImGui's public and internal headers, within the UI owner.
inline ImRect PanelFooterBounds(const ImGuiWindow* window) {
    ImRect panel = window->InnerRect;
    panel.Expand(-window->WindowBorderSize);
    panel.ClipWithFull(window->OuterRectClipped);
    return panel;
}

inline void DrawPanelFooterBackground(ImGuiWindow* window, float top, ImU32 fill, ImU32 border) {
    const auto panel = PanelFooterBounds(window);
    auto* draw = window->DrawList;
    draw->PushClipRect(panel.Min, panel.Max, false);
    draw->AddRectFilled({panel.Min.x, top}, panel.Max, fill,
        std::max(0.F, window->WindowRounding - window->WindowBorderSize), ImDrawFlags_RoundCornersBottom);
    draw->AddLine({panel.Min.x, top}, {panel.Max.x, top}, border);
    draw->PopClipRect();
}
