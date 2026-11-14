// One bounded, context-local transition for the content surface. Painted text,
// images and child tables all fade together; hit boxes and popup geometry stay put.
std::string LauncherPresentationRoute(int page) {
    std::string route = std::to_string(page);
    if (page == kPageModsHacks)
        route += ':' + std::to_string(g_mods_page.section) + ':' + std::to_string(g_mods_page.category);
    else if (page == kPageModLibrary)
        route += ':' + StoreRoute() + ':' + std::to_string(g_mod_store.category) + ':' +
                 std::to_string(g_mod_store.detail_tab);
    else if (page == kPageControls)
        route += ':' + std::to_string(g_controls_section) + ':' + std::to_string(g_selected_player);
    else if (page == kPageOnlineMp)
        route += ':' + std::to_string(int(g_online_page.section));
    return route;
}
struct LauncherContextMotion {
    ImGuiContext* context = nullptr;
    dkr::library_ui::PageMotion motion;
};
std::array<LauncherContextMotion, 2> g_launcher_content_motion{};
class LauncherContentEnter {
    struct Start {
        ImDrawList* draw = nullptr;
        int vertex = 0;
    };
    std::array<Start, 64> starts_{};
    std::size_t count_ = 0;
    ImGuiWindow* root_ = ImGui::GetCurrentWindow();
    LauncherContextMotion* state_ = nullptr;
    std::function<int()> page_;
    bool owns(ImGuiWindow* window) const {
        if (window->Flags & ImGuiWindowFlags_Popup)
            return false;
        for (auto* parent = window; parent; parent = parent->ParentWindow)
            if (parent == root_)
                return true;
        return false;
    }

  public:
    explicit LauncherContentEnter(std::function<int()> page) : page_(std::move(page)) {
        auto* context = ImGui::GetCurrentContext();
        for (auto& item : g_launcher_content_motion)
            if (item.context == context)
                state_ = &item;
        if (!state_) {
            state_ =
                &*std::min_element(g_launcher_content_motion.begin(), g_launcher_content_motion.end(),
                                   [](const auto& a, const auto& b) { return a.motion.started < b.motion.started; });
            *state_ = {};
            state_->context = context;
        }
        state_->motion.observe(LauncherPresentationRoute(page_()), ImGui::GetFrameCount(), PaddockClock());
        for (auto* window : context->Windows)
            if (owns(window) && count_ < starts_.size())
                starts_[count_++] = {window->DrawList, window->DrawList->VtxBuffer.Size};
    }
    ~LauncherContentEnter() {
        const double now = PaddockClock();
        state_->motion.observe(LauncherPresentationRoute(page_()), ImGui::GetFrameCount(), now);
        if (!state_->motion.active(now))
            return;
        std::array<ImDrawList*, 64> visited{};
        std::size_t used = 0;
        for (auto* window : ImGui::GetCurrentContext()->Windows) {
            if (!owns(window) || window->LastFrameActive != ImGui::GetFrameCount() || used == visited.size())
                continue;
            auto* draw = window->DrawList;
            if (std::find(visited.begin(), visited.begin() + used, draw) != visited.begin() + used)
                continue;
            visited[used++] = draw;
            int first = 0;
            for (std::size_t i = 0; i < count_; ++i)
                if (starts_[i].draw == draw) {
                    first = starts_[i].vertex;
                    break;
                }
            for (int i = first; i < draw->VtxBuffer.Size; ++i) {
                auto& vertex = draw->VtxBuffer[i];
                const float t = state_->motion.progress(now, vertex.pos.y - root_->Pos.y);
                const float inverse = 1.F - t;
                const float progress = 1.F - inverse * inverse * inverse;
                const ImU32 alpha = (vertex.col >> IM_COL32_A_SHIFT) & 255U;
                vertex.col =
                    (vertex.col & ~IM_COL32_A_MASK) | (ImU32(std::lround(alpha * progress)) << IM_COL32_A_SHIFT);
            }
        }
    }
};
