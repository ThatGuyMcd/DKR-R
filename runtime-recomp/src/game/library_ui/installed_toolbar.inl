// Shared layout for all four installed categories. Each caller keeps its own
// search buffer, filters and callbacks; this component owns only presentation.
bool g_library_controls_check = false;
// Read-only presentation fixtures are armed only by the authenticated layout
// harness. They contain no packages and cannot enter launch preparation.
template <class T>
std::shared_ptr<const T> LibraryConsistencyView(std::shared_ptr<const T> current, bool characters = false) {
    if (!g_library_controls_check)
        return current;
    static std::array<std::shared_ptr<const T>, 2> cached;
    auto& result = cached[characters ? 1 : 0];
    if (!result) {
        auto view = std::make_shared<T>();
        for (unsigned i = 0; i < 8; ++i) {
            const auto name = i < 2    ? "Duplicate preview"
                              : i == 2 ? "A long custom mod title for checking consistent wrapping"
                              : i == 3 ? "Brute"
                              : i == 4 ? "Gold Mantis"
                              : i == 5 ? "Kart"
                                       : "Preview " + std::to_string(i);
            if constexpr (requires(T value) { value.tracks; }) {
                typename decltype(view->tracks)::value_type item;
                item.id = "fixture-" + std::to_string(i);
                item.name = name;
                item.revision = "us.v77";
                item.source_name = "Layout check pack";
                item.patch = "fixture-source-" + std::to_string(i);
                item.enabled = i % 2 == 0;
                item.hidden = i == 7;
                item.vehicles = 7;
                item.base_character = i % 8;
                view->tracks.push_back(std::move(item));
            } else {
                typename decltype(view->items)::value_type item;
                item.id = "layout-preview-" + std::to_string(i);
                item.name = name;
                item.digest = std::string(63, '0') + char('1' + i);
                item.enabled = i % 2 == 0;
                if constexpr (requires { item.approved; }) {
                    item.version = "1.0.0";
                    item.selected = true;
                    item.approved = i != 4;
                    item.deterministic = true;
                }
                view->items.push_back(std::move(item));
            }
        }
        result = std::move(view);
    }
    return result;
}
void DrawInstalledToolbar(float width, const std::function<void(float)>& search, int& status,
                          const std::vector<std::string>& states, int& sort, const std::vector<std::string>& sorts) {
    using namespace dkr::library_ui;
    if (g_library_controls_check && ImGui::GetIO().DisplaySize.y <= 800.F)
        ImGui::SetScrollY(std::max(0.F, ImGui::GetCursorPosY() - 16.F));
    const auto at = ImGui::GetCursorScreenPos();
    const float caption = PaddockFieldLabelType().line + 6.F;
    const auto layout = installed_toolbar(width, caption);
    PaddockText(PaddockFieldLabelType(), PaddockRgb(0xABC0CC), "Search", layout.search_width);
    ImGui::SetCursorScreenPos({at.x, at.y + caption});
    search(layout.search_width);
    ImGui::SetCursorScreenPos({at.x + layout.status_x, at.y + layout.status_y});
    PaddockSelect("status", dkr::localisation::source("Status").data(), &status, LocalisedUi::enum_items(states),
                  layout.status_width);
    ImGui::SetCursorScreenPos({at.x + layout.sort_x, at.y + layout.sort_y});
    PaddockSelect("sort", dkr::localisation::source("Sort by").data(), &sort, LocalisedUi::enum_items(sorts),
                  layout.sort_width);
    ImGui::SetCursorScreenPos(at);
    ImGui::Dummy({width, layout.height});
    PaddockGap(ControlGap);
}
struct InstalledResultAction {
    std::string label;
    PaddockButtonKind kind = PaddockButtonKind::Settings;
    std::function<void()> activate;
};
void DrawInstalledResults(float width, const std::string& count,
                          const std::vector<InstalledResultAction>& actions = {}) {
    using namespace dkr::library_ui;
    const auto at = ImGui::GetCursorScreenPos();
    const auto type = PaddockReading(12.F, false, 1.5F);
    std::vector<float> widths;
    float total = 0;
    for (const auto& action : actions) {
        const auto caption = dkr::localisation::label(action.label);
        const float size = std::min(width, PaddockButtonWidth(caption.c_str(), action.kind));
        if (!widths.empty())
            total += 8.F;
        total += size;
        widths.push_back(size);
    }
    const bool side = actions.empty() || total + ControlGap + std::min(240.F, PaddockMeasure(type, count)) <= width;
    PaddockTextAt(ImGui::GetWindowDrawList(), type, {at.x, at.y + (ControlHeight - type.line) * .5F},
                  side ? std::max(1.F, width - total - (actions.empty() ? 0.F : ControlGap)) : width, count,
                  PaddockTextStyle{.colour = PaddockCol(0x9BB0BD)});
    float x = side ? width - total : 0, y = side ? 0 : ControlHeight + 8.F;
    for (std::size_t i = 0; i < actions.size(); ++i) {
        if (x > 0 && x + widths[i] > width + .01F) {
            x = 0;
            y += ControlHeight + 8.F;
        }
        ImGui::SetCursorScreenPos({at.x + x, at.y + y});
        const auto caption = dkr::localisation::label(actions[i].label);
        if (PaddockButton(caption.c_str(), actions[i].kind, widths[i], ControlHeight))
            actions[i].activate();
        x += widths[i] + 8.F;
    }
    ImGui::SetCursorScreenPos(at);
    ImGui::Dummy({width, y + ControlHeight});
    PaddockGap(16.F);
}
