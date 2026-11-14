// Shared website catalogue, existing asynchronous image cache and renderer owner.
// No featured-mod download or installation is started from the hero.
struct LauncherFeaturedCache {
    std::shared_ptr<const StoreJson> source;
    std::vector<std::size_t> rows;
    dkr::library_ui::FeaturedRotation rotation;
};
std::array<LauncherFeaturedCache, 2> g_launcher_featured;

void DrawLauncherFeatured(float width, bool live) {
    auto& service = dkr::store::service();
    if (!g_mod_store.opened) {
        g_mod_store.opened = true;
        service.refresh();
    }
    auto snapshot = service.view();
    auto& cache = g_launcher_featured[live ? 1 : 0];
    if (cache.source != snapshot->items) {
        cache.source = snapshot->items;
        cache.rows.clear();
        std::vector<std::string> ids;
        if (cache.source && cache.source->is_array())
            for (std::size_t i = 0; i < cache.source->size(); ++i) {
                const auto& row = (*cache.source)[i];
                if (!row.is_object() || !row.contains("featured") || row["featured"] != true ||
                    (row.contains("archived") && row["archived"] == true))
                    continue;
                auto id = StoreText(row, "id", 160);
                if (id.empty() || ids.size() == 64)
                    continue;
                cache.rows.push_back(i);
                ids.push_back(std::move(id));
            }
        cache.rotation.sync(std::move(ids));
    }
    auto& rotation = cache.rotation;
    const auto at = ImGui::GetCursorScreenPos();
    const float height = width < 700.F ? 290.F : 246.F;
    const ImVec2 end{at.x + width, at.y + height};
    const bool hovered = ImGui::IsMouseHoveringRect(at, end);
    const bool visible = ImGui::IsRectVisible({width, height});
    bool focused = false;
    const StoreJson* item = cache.rows.empty() ? nullptr : &(*cache.source)[cache.rows[rotation.index]];
    auto* draw = ImGui::GetWindowDrawList();
    draw->PushClipRect(at, end, true);
    const auto picture = [&](const StoreJson& row, float opacity) {
        if (opacity <= 0)
            return;
        ImGui::SetCursorScreenPos(at);
        const int first = draw->VtxBuffer.Size;
        StoreCover(row, width, height, false, 12.F);
        for (int i = first; i < draw->VtxBuffer.Size; ++i) {
            auto& v = draw->VtxBuffer[i];
            const auto alpha = (v.col >> IM_COL32_A_SHIFT) & 255U;
            v.col = (v.col & ~IM_COL32_A_MASK) | (ImU32(std::lround(alpha * opacity)) << IM_COL32_A_SHIFT);
        }
    };
    const float blend = rotation.blend(PaddockClock());
    if (item) {
        if (blend < 1 && rotation.previous < cache.rows.size())
            picture((*cache.source)[cache.rows[rotation.previous]], 1.F);
        picture(*item, blend);
    } else
        picture(StoreJson{{"image", "pattern"}}, 1.F);
    // The tint uses the same rounded geometry as the artwork, including its
    // antialias fringe. A rectangular gradient would still bleed at the corners.
    const int tint_begin = draw->VtxBuffer.Size;
    draw->AddRectFilled(at, end, IM_COL32_WHITE, 12.F);
    for (int i = tint_begin; i < draw->VtxBuffer.Size; ++i) {
        auto& vertex = draw->VtxBuffer[i];
        const float x = std::clamp((vertex.pos.x - at.x) / width, 0.F, 1.F);
        const float y = std::clamp((vertex.pos.y - at.y) / height, 0.F, 1.F);
        const float opacity = (250.F * (1.F - x) + (68.F + 48.F * y) * x) / 255.F;
        const auto alpha = (vertex.col >> IM_COL32_A_SHIFT) & 255U;
        vertex.col = IM_COL32(6, 27, 43, ImU32(std::lround(alpha * opacity)));
    }
    const float padding = width < 500.F ? 18.F : 26.F;
    const float copy_width = std::max(1.F, width < 700.F ? width - 2 * padding : std::min(650.F, width * .61F));
    ImGui::SetCursorScreenPos({at.x + padding, at.y + padding});
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {0.F, 0.F});
    ImGui::BeginGroup();
    PaddockText(PaddockReading(12.F, true, 1.2F), PaddockRgb(0xFFCD67), "COMMUNITY SPOTLIGHT", copy_width);
    PaddockGap(8.F);
    const auto title =
        item ? StoreText(*item, "title", 200) : std::string(dkr::localisation::source("Your next race starts here."));
    PaddockHeading(PaddockEllipsize(PaddockSign(30.F), title, copy_width).c_str(), 30.F, 0xFFF0C6, copy_width);
    PaddockGap(8.F);
    StoreNote(item ? dkr::localisation::format("library.by", {{"author", StoreText(*item, "author", 100)}})
                   : std::string(dkr::localisation::source("Find a new track, racer or ride in the Mod Library.")),
              copy_width);
    PaddockGap(12.F);
    ImGui::PushID(live ? "featured-overlay" : "featured-launcher");
    if (StoreButton(dkr::localisation::label(item ? "Explore this mod" : "Browse Mod Library"),
                    std::min(copy_width, 220.F), true)) {
        const auto chosen = item ? *item : StoreJson{};
        StoreNavigate([chosen] {
            OpenModLibrary(0);
            if (!chosen.is_null() && !chosen.empty())
                StoreOpenMod(chosen, true);
        });
    }
    focused |= ImGui::IsItemFocused();
    ImGui::EndGroup();
    const float controls_y = at.y + height - 52.F;
    const auto pause_label =
        std::string(dkr::localisation::text(rotation.paused ? "library.play_rotation" : "library.pause")) +
        "###featured-pause";
    const float pause_width = std::max(76.F, PaddockButtonWidth(pause_label.c_str(), PaddockButtonKind::Plain));
    const float controls_width = 88.F + pause_width;
    ImGui::SetCursorScreenPos({at.x + padding, controls_y});
    const auto status = rotation.ids.empty()
                            ? std::string(dkr::localisation::source("Free community mods"))
                            : std::to_string(rotation.index + 1) + " / " + std::to_string(rotation.ids.size()) +
                                  "  -  " +
                                  std::string(dkr::localisation::source(rotation.paused ? "Paused" : "Every 30s"));
    PaddockText(PaddockReading(12.F), PaddockRgb(0xC3D3DC), status,
                std::max(1.F, width - 2 * padding - controls_width - 10.F));
    if (rotation.ids.size() > 1) {
        ImGui::SetCursorScreenPos({end.x - padding - controls_width, controls_y - 6.F});
        if (StoreButton("<##featured-previous", 38.F))
            rotation.step(-1, PaddockClock());
        focused |= ImGui::IsItemFocused();
        ImGui::SameLine(0, 6.F);
        if (StoreButton(pause_label, pause_width))
            rotation.paused = !rotation.paused;
        focused |= ImGui::IsItemFocused();
        ImGui::SameLine(0, 6.F);
        if (StoreButton(">##featured-next", 38.F))
            rotation.step(1, PaddockClock());
        focused |= ImGui::IsItemFocused();
    }
    rotation.tick(PaddockClock(), ImGui::GetFrameCount(),
                  hovered || focused || ImGui::GetIO().AppFocusLost || !visible);
    ImGui::PopID();
    ImGui::PopStyleVar();
    draw->PopClipRect();
    draw->AddRect(at, end, PaddockCol(0xD9A94F), 12.F, 0, 1.5F);
    ImGui::SetCursorScreenPos(at);
    ImGui::Dummy({width, height});
    PaddockGap(22.F);
}
