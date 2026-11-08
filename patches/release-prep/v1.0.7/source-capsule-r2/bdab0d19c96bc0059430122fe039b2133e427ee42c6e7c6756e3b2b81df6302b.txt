// Portal palette and bounded, controller-navigable layouts. No child windows:
// the launcher owns the single page scroll surface.
struct StorePanel {
    ImVec2 origin;
    float width, padding = 16.F, minimum;
    ImDrawList* draw;
    ImDrawListSplitter layers;
    StorePanel(float requested, bool selected = false, float min_height = 0)
        : origin(ImGui::GetCursorScreenPos()), width(std::max(1.F, requested)), minimum(min_height),
          draw(ImGui::GetWindowDrawList()), selected_(selected) {
        layers.Split(draw, 2);
        layers.SetCurrentChannel(draw, 1);
        ImGui::SetCursorScreenPos({origin.x + padding, origin.y + padding});
        ImGui::BeginGroup();
    }
    float inner() const {
        return std::max(1.F, width - 2 * padding);
    }
    float bottom() const {
        return origin.y + minimum - padding;
    }
    ~StorePanel() {
        ImGui::EndGroup();
        const float height = std::max(minimum, ImGui::GetCursorScreenPos().y - origin.y + padding);
        layers.SetCurrentChannel(draw, 0);
        PaddockPanelStyle style;
        style.radii = {10.F, 23.F, 10.F, 10.F};
        style.fill = PaddockRgb(selected_ ? 0x123F56 : 0x0B304B);
        style.border = PaddockRgb(selected_ ? 0xFFD28A : 0x8E794C);
        style.border_width = selected_ ? 2.F : 1.F;
        style.drop = PaddockRgb(0x041822);
        style.drop_offset = 4.F;
        PaddockPanel(draw, origin, {origin.x + width, origin.y + height}, style);
        layers.Merge(draw);
        ImGui::SetCursorScreenPos(origin);
        ImGui::Dummy({width, height});
        PaddockGap(16.F);
    }

  private:
    bool selected_;
};
struct StoreFormLayout {
    bool columns = false;
    float width;
    StoreFormLayout(const char* id, float available, float maximum = 680.F) {
        columns = available >= maximum + 280.F &&
                  ImGui::BeginTable(id, 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings,
                                    {std::min(available, maximum + 360.F), 0});
        if (columns) {
            ImGui::TableSetupColumn("Form", ImGuiTableColumnFlags_WidthFixed, maximum);
            ImGui::TableSetupColumn("Summary", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
        }
        width = std::min(maximum, ImGui::GetContentRegionAvail().x);
    }
    void sidebar(const std::function<void(float)>& content) {
        if (columns) {
            ImGui::TableSetColumnIndex(1);
            content(ImGui::GetContentRegionAvail().x);
            ImGui::TableSetColumnIndex(0);
        }
    }
    ~StoreFormLayout() {
        if (columns)
            ImGui::EndTable();
    }
};
void StoreBadge(const std::string& label, float width, PaddockTone tone = PaddockTone::Neutral) {
    const auto size = PaddockBadgeSize(label);
    if (size.x <= width) {
        PaddockBadge(ImGui::GetWindowDrawList(), ImGui::GetCursorScreenPos(), label, tone);
        ImGui::Dummy(size);
    } else
        StoreNote(label, width);
    PaddockGap(8.F);
}
std::string StoreCategory(const StoreJson& item) {
    for (unsigned i = 1; i < kStoreCategoryIds.size(); ++i)
        if (StoreText(item, "category", 32) == kStoreCategoryIds[i])
            return kStoreCategoryNames[i];
    return "Mod";
}
std::string StoreRating(const StoreJson& item) {
    if (item.contains("rating") && item["rating"].is_object()) {
        const auto& rating = item["rating"];
        const int count = StoreInt(rating, "count");
        if (count && rating.contains("average") && rating["average"].is_number()) {
            char value[100];
            std::snprintf(value, sizeof(value), "%.1f / 5  |  %d review%s",
                          std::clamp(rating["average"].get<double>(), 0., 5.), count, count == 1 ? "" : "s");
            return value;
        }
    }
    return "No reviews yet";
}
std::string StoreSummary(const StoreJson& item) {
    auto value = StoreText(item, "summary", 4000);
    if (value.size() > 135) {
        auto end = value.rfind(' ', 132);
        if (end == value.npos || end < 60)
            end = 132;
        while (end && (static_cast<unsigned char>(value[end]) & 0xC0) == 0x80)
            --end;
        value = value.substr(0, end) + "...";
    }
    return value;
}
int StoreTabLine(const std::vector<std::string>& labels, int selected, float width, const char* id) {
    if (labels.empty())
        return 0;
    selected = std::clamp(selected, 0, int(labels.size()) - 1);
    ImGui::PushID(id);
    const auto start = ImGui::GetCursorScreenPos();
    float x = 0, y = 0, row_height = 0;
    for (int i = 0; i < int(labels.size()); ++i) {
        const auto kind = i == selected ? PaddockButtonKind::Primary : PaddockButtonKind::Plain;
        const float size = std::min(width, PaddockButtonWidth(labels[i].c_str(), kind));
        if (x > 0 && x + size > width) {
            x = 0;
            y += row_height + 8;
            row_height = 0;
        }
        ImGui::SetCursorScreenPos({start.x + x, start.y + y});
        ImGui::PushID(i);
        if (PaddockButton((labels[i] + "###tab").c_str(), kind, size, 44.F))
            selected = i;
        StoreControlFocus();
        row_height = std::max(row_height, ImGui::GetItemRectSize().y);
        ImGui::PopID();
        x += size + 8;
    }
    ImGui::SetCursorScreenPos({start.x, start.y + y + row_height + 16});
    ImGui::PopID();
    return selected;
}
void StoreFieldPair(const char* key_a, const char* label_a, const char* key_b, const char* label_b, float width,
                    std::size_t maximum_a = 100, std::size_t maximum_b = 100) {
    if (width >= 620.F &&
        ImGui::BeginTable(key_a, 2, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings, {width, 0})) {
        ImGui::TableNextColumn();
        StoreField(key_a, label_a, ImGui::GetContentRegionAvail().x, maximum_a);
        ImGui::TableNextColumn();
        StoreField(key_b, label_b, ImGui::GetContentRegionAvail().x, maximum_b);
        ImGui::EndTable();
    } else {
        StoreField(key_a, label_a, width, maximum_a);
        StoreField(key_b, label_b, width, maximum_b);
    }
}
std::string StoreDate(const StoreJson& row, const char* key = "updatedAt") {
    auto date = StoreText(row, key, 80);
    if (date.size() >= 10)
        return date.substr(0, 10);
    return date.empty() ? "Not supplied" : date;
}
void StoreNotice(const std::string& title, const std::string& message, float width, bool error = false) {
    StorePanel panel(std::min(width, 980.F), error);
    if (!title.empty())
        StoreHeading(title, panel.inner(), 18.F);
    StoreNote(message, panel.inner(), !error);
}
void StoreTags(const StoreJson& row, float width) {
    if (!row.contains("tags") || !row["tags"].is_array())
        return;
    const auto start = ImGui::GetCursorScreenPos();
    float x = 0, height = 0;
    unsigned shown = 0;
    for (const auto& tag : row["tags"]) {
        if (!tag.is_string() || shown == 2)
            break;
        const auto label = tag.get<std::string>();
        const auto size = PaddockBadgeSize(label);
        if (x + size.x > width)
            continue;
        PaddockBadge(ImGui::GetWindowDrawList(), {start.x + x, start.y}, label, PaddockTone::Neutral);
        x += size.x + 6.F;
        height = std::max(height, size.y);
        ++shown;
    }
    if (height) {
        ImGui::Dummy({width, height});
        PaddockGap(8.F);
    }
}
