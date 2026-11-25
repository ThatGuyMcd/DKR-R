// Reads one already-selected EEPROM on a worker. It never prepares a launch,
// seeds a mod save, walks directories or changes the save owner's route.
struct PlaySavePreviewData {
    std::optional<dkr::runtime::saves::codec::SaveImage> image;
    std::string_view status;
};
struct PlaySavePreview {
    std::filesystem::path path;
    std::future<std::pair<std::filesystem::path, PlaySavePreviewData>> pending;
    PlaySavePreviewData summary;
    double next = 0;
};
PlaySavePreview g_play_save_preview;
PlaySavePreviewData PlayReadSavePreview(const std::filesystem::path& path) {
    namespace codec = dkr::runtime::saves::codec;
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file)
        return {{}, "No saved progress for this session yet."};
    if (file.tellg() != std::streamoff(codec::kImageSize))
        return {{}, "Save preview unavailable. Check Save Manager."};
    std::array<std::uint8_t, codec::kImageSize> bytes{};
    file.seekg(0);
    file.read(reinterpret_cast<char*>(bytes.data()), bytes.size());
    codec::SaveImage image;
    if (!file || !codec::decode(bytes, image))
        return {{}, "Save preview unavailable. Check Save Manager."};
    return {std::move(image), {}};
}
void DrawPlaySaveSummary(float width, bool live, bool launching) {
    using namespace dkr::library_ui;
    namespace saves = dkr::runtime::saves;
    SaveScope scope = SaveScope::Vanilla;
    std::filesystem::path path;
    const bool lobby = dkr::runtime::netplay::session().active();
    if (live) {
        path = saves::runtime_save_path();
        scope = running_save_scope(g_config_directory, path, saves::runtime_save_selection().online);
    } else if (lobby) {
        scope = SaveScope::Online;
    } else if (dkr::session_content::next_enabled.load()) {
        scope = SaveScope::Modded;
        path = saves::personal_adventure_path(g_config_directory, true);
    }
    const char* heading = scope == SaveScope::Online   ? "ONLINE ADVENTURE"
                          : scope == SaveScope::Modded ? "MODDED ADVENTURE"
                                                       : "VANILLA ADVENTURE";
    const auto heading_type = PaddockReading(16.F, true, 1.45F);
    const auto heading_text = dkr::localisation::source(heading);
    const float heading_width = std::ceil(PaddockMeasure(heading_type, heading_text)) + 8.F;
    const float cloud_width = PlayCloudSwitchWidth() + kPlayCloudGap + kPlayCloudIconWidth;
    const bool inline_cloud = width - heading_width >= 118.F + kPlayCloudGap + kPlayCloudIconWidth;
    const bool cloud_locked = live || lobby || launching;
    if (inline_cloud) {
        ImGui::BeginGroup();
        auto position = ImGui::GetCursorScreenPos();
        position.y += (kPlayCloudControlHeight - heading_type.line) * 0.5F;
        PaddockDrawRun(ImGui::GetWindowDrawList(), heading_type, position, PaddockCol(0xFFF6DA), heading_text.data(),
                       heading_text.data() + heading_text.size());
        ImGui::Dummy({heading_width, kPlayCloudControlHeight});
        ImGui::SameLine(0.F, 0.F);
        DrawPlayCloudSaves(std::min(cloud_width, width - heading_width), cloud_locked);
        ImGui::EndGroup();
    } else {
        PaddockText(heading_type, PaddockRgb(0xFFF6DA), heading_text, width);
        PaddockGap(4.F);
        DrawPlayCloudSaves(width, cloud_locked);
    }
    PaddockGap(6.F);
    const auto at = ImGui::GetCursorScreenPos();
    auto* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(at, {at.x + 62.F, at.y + 22.F}, PaddockCol(0x041B26), 11.F);
    constexpr std::array<unsigned, 3> colours{0x24D493, 0xFFC04D, 0x57CDE8};
    for (int i = 0; i < 3; ++i) {
        const bool active = int(scope) == i + 1;
        draw->AddCircleFilled({at.x + 11.F + 20.F * i, at.y + 11.F}, 6.F, PaddockCol(active ? colours[i] : 0x304752),
                              14);
    }
    const auto save_caption = dkr::localisation::source(save_label(scope));
    const std::string label = dkr::localisation::format(live ? "save.using" : "save.next", {{"save", save_caption}});
    const auto type = PaddockReading(14.F, true, 1.4F);
    const float label_width = std::max(1.F, width - 72.F);
    const float label_height = PaddockTextHeight(type, label, label_width);
    PaddockTextStyle label_style;
    label_style.colour = PaddockCol(0xFFF3C9);
    PaddockTextAt(draw, type, {at.x + 72.F, at.y + 1.F}, label_width, label, label_style);
    ImGui::Dummy({width, std::max(24.F, label_height)});
    PaddockGap(6.F);
    std::string preview;
    if (scope == SaveScope::Vanilla) {
        const auto& save = CachedSaveManagerView().adventures[false];
        preview = save.image
                      ? dkr::localisation::save_preview(*save.image)
                      : std::string(dkr::localisation::source(save.info.exists
                                                                  ? "Save preview unavailable. Check Save Manager."
                                                                  : "No Vanilla Adventure save yet."));
    } else if (!live && scope == SaveScope::Online) {
        preview =
            std::string(dkr::localisation::source("The host supplies a separate online save when the match starts."));
    } else if (path.empty())
        preview = std::string(dkr::localisation::source("Waiting for the session save owner."));
    else {
        auto& cache = g_play_save_preview;
        if (cache.pending.valid() && cache.pending.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            try {
                auto result = cache.pending.get();
                if (result.first == path) {
                    cache.path = result.first;
                    cache.summary = std::move(result.second);
                }
            } catch (...) {
                cache.path = path;
                cache.summary = {{}, "Save preview unavailable. Check Save Manager."};
            }
        }
        if (!cache.pending.valid() && (cache.path != path || PaddockClock() >= cache.next)) {
            cache.next = PaddockClock() + 2;
            cache.pending =
                std::async(std::launch::async, [path] { return std::pair{path, PlayReadSavePreview(path)}; });
        }
        preview = cache.path == path
                      ? (cache.summary.image ? dkr::localisation::save_preview(*cache.summary.image)
                                             : std::string(dkr::localisation::source(cache.summary.status)))
                      : std::string(dkr::localisation::source("Reading this session's saved progress..."));
    }
    PaddockText(PaddockReading(14.F, false, 1.5F), PaddockRgb(0xB0C9CC), preview, width);
    PaddockText(PaddockReading(13.F, false, 1.5F), PaddockRgb(0xB0C9CC),
                dkr::localisation::source(live ? "Preview shows saved progress for this session."
                                               : "Session Mods selects which Adventure save you use offline."),
                width);
    PaddockGap(12.F);
}
