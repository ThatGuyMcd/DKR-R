// Included by runtime_ui.cpp inside its UI namespace, after
// runtime_ui_paddock.inl. Presentation only; all filesystem/catalogue
// mutations are dispatched through ModLibrary workers or the caller.
//
// MODS / HACKS "My mods": one library of legacy patches and native .dkrmap
// tracks, drawn as paddock cards.

// A native .dkrmap track, as the caller resolved it from custom_tracks.
struct DkrLibraryTrack {
    std::string id;
    std::string name;
    std::string author;
    std::string source;          // the .dkrmap folder name
    std::uint64_t bytes = 0;     // payload size
    bool enabled = true;
    bool kept_online = false;
    bool hd_textures = false;    // an installed, enabled HD pack
    std::string hd_pack_id;      // installed HD pack, for its Manage modal
    bool installed = false;     // managed copy, safe to uninstall
};

struct ModsPageState {
    int section = -1;            // -1 closed, then kModsSection*
    int entry_section = -1;      // opened on the next entry (links from other pages)
    int category = 0;            // 0 tracks, 1 characters
    bool filters_open = false;
    std::set<std::string> disclosures;
    std::string dkr_details_id;
    bool request_import_chooser = false;
    bool request_dkr_details = false;
    std::string manage_pack_request;
    bool dkr_remove_confirm = false;
    std::string uninstall_track_request;
    // The page resets on entry, and the launcher and overlay are separate
    // ImGui contexts: a change of context is an entry too.
    ImGuiContext* context = nullptr;
    int last_frame = -100;
    double entered_at = -10.0;   // last section / category switch (PaddockClock)
    double filters_entered_at = -10.0;
};
ModsPageState g_mods_page;

constexpr int kModsSectionLibrary = 0;
constexpr int kModsSectionMagic = 1;
constexpr int kModsSectionTrackLab = 2;
constexpr int kModsSectionHelp = 3;
constexpr float kModsCardGap = 20.0F;

// Content that slides up and fades in after a section switch. Chunks after
// the eighth arrive with the eighth.
class PaddockEnter {
public:
    explicit PaddockEnter(int index, double started_at = g_mods_page.entered_at)
        : draw_(ImGui::GetWindowDrawList()),
          first_vertex_(draw_->VtxBuffer.Size),
          index_(std::min(index, 7)),
          started_at_(started_at) {}
    PaddockEnter(const PaddockEnter&) = delete;
    PaddockEnter& operator=(const PaddockEnter&) = delete;
    ~PaddockEnter() {
        const float elapsed = static_cast<float>(
            PaddockClock() - started_at_) - 0.06F * index_;
        const float progress = PaddockEase(elapsed / 0.32F);
        if (progress >= 1.0F) return;
        const float offset = 8.0F * (1.0F - progress);
        for (int vertex = first_vertex_; vertex < draw_->VtxBuffer.Size; ++vertex) {
            ImDrawVert& v = draw_->VtxBuffer[vertex];
            v.pos.y += offset;
            const ImU32 alpha = (v.col >> IM_COL32_A_SHIFT) & 0xFFU;
            v.col = (v.col & ~IM_COL32_A_MASK) |
                (static_cast<ImU32>(std::lround(alpha * progress)) << IM_COL32_A_SHIFT);
        }
    }

private:
    ImDrawList* draw_;
    int first_vertex_;
    int index_;
    double started_at_;
};

std::string FormatModSize(std::uint64_t bytes) {
    constexpr std::array<const char*, 5> units{{"B", "KB", "MB", "GB", "TB"}};
    double value = static_cast<double>(bytes);
    std::size_t unit = 0U;
    while (value >= 1024.0 && unit + 1U < units.size()) {
        value /= 1024.0;
        ++unit;
    }
    char text[32]{};
    if (unit == 0U) {
        std::snprintf(text, sizeof(text), "%.0f %s", value, units[unit]);
    } else {
        std::snprintf(text, sizeof(text), "%.1f %s", value, units[unit]);
    }
    return text;
}

std::vector<std::filesystem::path> ModReviewRoms() {
    std::vector<std::filesystem::path> roms;
    for (const auto& entry : LoadRomCatalog()) {
        if (roms.size() == 8U) break;
        roms.push_back(entry.path);
    }
    return roms;
}

// ------------------------------------------------------------ library data

// One card: a legacy catalogue entry or a native track.
struct LibraryEntry {
    const dkr::mods::browser::Card* legacy = nullptr;
    const DkrLibraryTrack* dkr = nullptr;
};

struct LibraryCardView {
    bool native = false;
    bool native_toggle = false;
    std::string title;
    std::string byline;
    std::string info;
    std::string size;
    std::string note;
    std::string status;
    PaddockTone tone = PaddockTone::Neutral;
    bool enabled = false;
    bool toggle_allowed = false;
    bool gold = false;          // enabled and prepared for this ROM
    bool prepare = false;
    std::string id;
    std::string review;
};

LibraryCardView DescribeLibraryCard(const LibraryEntry& entry, bool characters,
                                    unsigned active, bool locked) {
    using namespace dkr::mods;
    LibraryCardView view;
    if (entry.dkr != nullptr) {
        const DkrLibraryTrack& track = *entry.dkr;
        view.native = true;
        view.id = track.id;
        view.title = track.name;
        view.byline = track.author.empty() ? track.source : "By " + track.author;
        view.info = track.hd_textures
            ? "HD textures \xC2\xB7 Modern graphics" : "Modern graphics";
        if (track.bytes != 0U) view.size = FormatModSize(track.bytes);
        view.note = "Installed. Available automatically in the in-game track menu.";
        view.status = "Installed";
        view.tone = PaddockTone::On;
        if (track.kept_online) {
            view.native_toggle = true; view.enabled = track.enabled;
            view.toggle_allowed = !locked; view.gold = track.enabled;
            view.note = track.enabled ? "Kept from online. Enabled for offline play."
                                      : "Kept from online. Turn on to use offline.";
            view.status = track.enabled ? "On" : "Off";
            view.tone = track.enabled ? PaddockTone::On : PaddockTone::Neutral;
        }
        return view;
    }
    const browser::Card& card = *entry.legacy;
    const TrackCatalogItem& item = card.item;
    const unsigned revision = g_mod_browser_revision;
    const bool ready = browser::compatible(card, revision);
    view.id = item.id;
    view.review = item.review;
    view.title = item.name;
    for (const auto& source : card.sources) {
        if (!view.byline.empty()) view.byline += ", ";
        view.byline += source;
    }
    view.enabled = item.enabled;
    view.toggle_allowed = !locked &&
        (item.enabled || browser::can_activate(card, characters, active, revision, locked));
    view.gold = item.enabled && ready;
    if (characters) {
        view.info = "Custom racer";
    } else {
        for (const auto& [bit, name] : {std::pair{1U, "Car"}, std::pair{2U, "Hovercraft"},
                                        std::pair{4U, "Plane"}}) {
            if ((item.vehicles & bit) == 0U) continue;
            if (!view.info.empty()) view.info += " \xC2\xB7 ";
            view.info += name;
        }
    }
    view.size = FormatModSize(item.managed_bytes);
    const bool mismatch = revision != 0U && !ready;
    if (item.hidden) {
        view.status = "Hidden";
    } else if (mismatch) {
        view.status = "Different ROM version";
    } else {
        view.status = item.enabled ? "On for next launch" : "Off";
    }
    view.tone = mismatch ? PaddockTone::Warning
        : item.enabled && !item.hidden ? PaddockTone::On : PaddockTone::Neutral;
    if (item.hidden) {
        view.note = "Restore this mod in Details to enable it.";
    } else if (mismatch) {
        view.note = "Prepare this legacy mod for the ROM version you have loaded.";
    } else if (characters && !item.enabled && active >= MaxActiveStageCharacters) {
        view.note = "The native character identity range is full. Installed mods remain available.";
    } else if (!item.details.empty()) {
        view.note = item.details;
    } else {
        view.note = characters ? "Appears in character selection when enabled."
                               : "Appears below the original worlds in Track Select.";
    }
    view.prepare = !item.hidden && mismatch && !item.review.empty();
    return view;
}

// The layout below mirrors .mods-library-card; every number is a CSS pixel.
float MeasureLibraryCard(const LibraryCardView& view, float width) {
    const float inner = std::max(width - 36.0F, 1.0F);
    const PaddockType title = PaddockSign(25.0F, 1.15F);
    const PaddockType info = PaddockReading(12.0F, true, 1.5F);
    const PaddockType size = PaddockReading(11.0F, false, 1.5F);
    const PaddockType note = PaddockReading(13.0F, false, 1.55F);
    float height = 2.0F + 16.0F + 26.0F + 13.0F;
    height += 1.0F + 17.0F + PaddockTextHeight(title, view.title, inner, true) +
              9.0F + 18.0F + 14.0F + 2.0F;
    const bool info_fits = view.size.empty() ||
        PaddockMeasure(info, view.info) + 8.0F + PaddockMeasure(size, view.size) <= inner;
    height += 18.0F + (info_fits ? info.line : info.line + 8.0F + size.line) + 9.0F;
    height += PaddockTextHeight(note, view.note, inner) + 18.0F;
    if (view.prepare) height += 42.0F;
    height += 2.0F + 12.0F + 42.0F + 16.0F + 2.0F;
    return std::ceil(height);
}

// Returns true when Details was pressed.
bool DrawLibraryCard(const LibraryCardView& view, ImVec2 origin, float width,
                     float height, bool characters, bool locked) {
    using namespace dkr::mods;
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const float inner = std::max(width - 36.0F, 1.0F);
    const ImVec2 end{origin.x + width, origin.y + height};
    const PaddockRadii radii{10.0F, 24.0F, 10.0F, 10.0F};
    PaddockPanelStyle panel;
    panel.radii = radii;
    panel.fill = PaddockRgb(0x0B304B);
    panel.drop = PaddockRgb(0x041822);
    panel.drop_offset = 5.0F;
    panel.ring = PaddockRgb(0x061F32);
    panel.ring_width = 2.0F;
    panel.border_width = 2.0F;
    PaddockPanel(draw, origin, end, panel);

    const float left = origin.x + 18.0F;
    float y = origin.y + 18.0F;
    const PaddockFormat format = view.native ? PaddockFormat::Dkr : PaddockFormat::Legacy;
    PaddockFormatTag(draw, {left, y}, format);
    const ImVec2 badge = PaddockBadgeSize(view.status);
    PaddockBadge(draw, {origin.x + width - 18.0F - badge.x, y + (26.0F - badge.y) * 0.5F},
                 view.status, view.tone);
    y += 26.0F + 13.0F;

    // The painted title board, with its checker flag.
    const PaddockType title = PaddockSign(25.0F, 1.15F);
    const PaddockType byline = PaddockReading(12.0F, false, 1.5F);
    const float title_height = PaddockTextHeight(title, view.title, inner, true);
    const float band_bottom = y + 1.0F + 17.0F + title_height + 9.0F + 18.0F + 14.0F + 2.0F;
    const float band_left = origin.x + 4.0F;
    const float band_right = end.x - 4.0F;
    draw->AddRectFilled({band_left, y}, {band_right, band_bottom},
                        PaddockCol(view.native ? 0x086773U : 0x095B82U));
    draw->AddRectFilled({band_left, y}, {band_right, y + 1.0F}, PaddockCol(0x4795AF));
    draw->AddRectFilled({band_left, band_bottom - 2.0F}, {band_right, band_bottom},
                        PaddockCol(0x042338));
    PaddockChecker(draw, {band_right - 64.0F, y + 1.0F}, 8, 8.0F, 0x073E5D, 0xFFCF6D);
    PaddockTextStyle title_style;
    title_style.colour = PaddockCol(0xFFF1BD);
    title_style.shadow = PaddockCol(0x031623);
    PaddockTextAt(draw, title, {left, y + 18.0F}, inner, view.title, title_style, true);
    const std::string shown_byline = PaddockEllipsize(byline, view.byline, inner);
    PaddockDrawRun(draw, byline, {left, y + 18.0F + title_height + 9.0F},
                   PaddockCol(0xD1E3ED), shown_byline.data(),
                   shown_byline.data() + shown_byline.size());
    y = band_bottom + 18.0F;

    const PaddockType info = PaddockReading(12.0F, true, 1.5F);
    const PaddockType size = PaddockReading(11.0F, false, 1.5F);
    PaddockDrawRun(draw, info, {left, y}, PaddockCol(0xE4EFF3), view.info.data(),
                   view.info.data() + view.info.size());
    if (!view.size.empty()) {
        const float size_width = PaddockMeasure(size, view.size);
        const bool fits = PaddockMeasure(info, view.info) + 8.0F + size_width <= inner;
        const ImVec2 at = fits ? ImVec2{left + inner - size_width, y}
                               : ImVec2{left, y + info.line + 8.0F};
        PaddockDrawRun(draw, size, at, PaddockCol(0xB4CEDA), view.size.data(),
                       view.size.data() + view.size.size());
        if (!fits) y += 8.0F + size.line;
    }
    y += info.line + 9.0F;
    const PaddockType note = PaddockReading(13.0F, false, 1.55F);
    PaddockTextStyle note_style;
    note_style.colour = PaddockCol(0xC0D4DF);
    y += PaddockTextAt(draw, note, {left, y}, inner, view.note, note_style) + 18.0F;

    bool details = false;
    if (view.prepare) {
        ImGui::SetCursorScreenPos({left, y});
        ImGui::BeginDisabled(locked);
        if (PaddockButton("Prepare mod##prepare", PaddockButtonKind::WarmLink, inner)) {
            g_legacy_imports.prepare_review(view.review, ModReviewRoms());
        }
        ImGui::EndDisabled();
    }

    const float footer = end.y - 18.0F - 56.0F;
    PaddockDashes(draw, {left, footer}, inner, PaddockCol(0x39718A), 2.0F);
    const float row = footer + 14.0F;
    bool card_focused = false;
    if (!view.native || view.native_toggle) {
        ImGui::SetCursorScreenPos({left, row});
        bool enabled = view.enabled;
        ImGui::BeginDisabled(!view.toggle_allowed);
        if (PaddockSwitch("##enabled", &enabled, "On", "Off")) {
            if (view.native) {
                dkr::runtime::custom_tracks::set_kept_enabled(view.id, enabled, g_legacy_import_status);
            } else {
                const auto kind = characters ? TrackCatalog::Kind::Character : TrackCatalog::Kind::Track;
                g_legacy_imports.set_enabled(kind, view.id, enabled);
            }
        }
        card_focused |= ImGui::IsItemFocused();
        ImGui::EndDisabled();
    }
    const float details_width = PaddockButtonWidth("Details", PaddockButtonKind::WarmLink);
    ImGui::SetCursorScreenPos({left + inner - details_width, row});
    details = PaddockButton("Details##details", PaddockButtonKind::WarmLink);
    card_focused |= ImGui::IsItemFocused();

    const ImGuiID card_id = ImGui::GetID("card-edge");
    const bool lit = ImGui::IsMouseHoveringRect(origin, end) || card_focused;
    const float hover = PaddockEase(PaddockTween(card_id, lit, 0.16F, 0.16F));
    const ImU32 resting = PaddockRgb(view.gold ? 0xF3BD51U : 0x3581A2U);
    const ImU32 border = view.gold ? resting : PaddockMix(resting, PaddockRgb(0x86CEE3), hover);
    PaddockStroke(draw, origin, end, radii, PaddockApply(border), 2.0F);
    return details;
}

// ------------------------------------------------------------ filters

std::string LibraryEntryName(const LibraryEntry& entry) {
    return entry.dkr != nullptr ? entry.dkr->name : entry.legacy->item.name;
}

LibraryEntry ResolveLibraryEntry(const ModBrowserState& state,
                                 const std::vector<DkrLibraryTrack>& tracks, int index) {
    if (index >= 0) return {&state.all[static_cast<std::size_t>(index)], nullptr};
    return {nullptr, &tracks[static_cast<std::size_t>(-index - 1)]};
}

// Indices of the cards to show, in order: >= 0 into state.all, < 0 into
// `tracks` (-1 is the first track).
std::vector<int> SelectLibraryEntries(const ModBrowserState& state,
                                      const std::vector<DkrLibraryTrack>& tracks,
                                      bool characters) {
    using namespace dkr::mods;
    std::vector<int> all;
    all.reserve(state.all.size() + tracks.size());
    for (std::size_t index = 0U; index < state.all.size(); ++index) {
        all.push_back(static_cast<int>(index));
    }
    if (!characters) {
        for (std::size_t index = 0U; index < tracks.size(); ++index) {
            all.push_back(-static_cast<int>(index) - 1);
        }
    }
    const std::string query = browser::lower(state.search);
    const unsigned revision = g_mod_browser_revision;
    struct Keyed {
        int index;
        std::string name;
        std::uint64_t bytes;
        std::uint64_t imported;
        std::string id;
    };
    std::vector<Keyed> keyed;
    for (const int index : all) {
        const LibraryEntry entry = ResolveLibraryEntry(state, tracks, index);
        const bool native = entry.dkr != nullptr;
        std::string searchable = LibraryEntryName(entry);
        std::set<std::string> sources;
        bool enabled = true;
        bool hidden = false;
        bool compatible = true;
        if (native) {
            searchable += ' ' + entry.dkr->author + " DKR " + entry.dkr->source;
            sources.insert(entry.dkr->source);
            enabled = entry.dkr->enabled;
        } else {
            searchable += " Legacy";
            for (const auto& source : entry.legacy->sources) searchable += ' ' + source;
            sources = entry.legacy->sources;
            enabled = entry.legacy->item.enabled;
            hidden = entry.legacy->item.hidden;
            compatible = browser::compatible(*entry.legacy, revision);
        }
        if (!query.empty() && browser::lower(searchable).find(query) == std::string::npos) continue;
        if ((state.format == 1 && native) || (state.format == 2 && !native)) continue;
        if (!state.source.empty() && !sources.contains(state.source)) continue;
        if ((state.state == 1 && !enabled) || (state.state == 2 && enabled)) continue;
        if ((state.visibility == 0 && hidden) || (state.visibility == 2 && !hidden)) continue;
        if ((state.compatibility == 1 && !compatible) ||
            (state.compatibility == 2 && compatible)) continue;
        keyed.push_back({index, browser::lower(LibraryEntryName(entry)),
                         native ? entry.dkr->bytes : entry.legacy->item.managed_bytes,
                         native ? 0U : entry.legacy->item.imported_at,
                         native ? "dkr:" + entry.dkr->id : entry.legacy->item.id});
    }
    const int sort = state.sort;
    std::sort(keyed.begin(), keyed.end(), [sort](const Keyed& a, const Keyed& b) {
        if (sort == 2 && a.bytes != b.bytes) return a.bytes > b.bytes;
        if (sort == 3 && a.bytes != b.bytes) return a.bytes < b.bytes;
        if ((sort == 4 || sort == 5) && a.imported != b.imported) {
            // Unknown dates sort last in either direction.
            if (a.imported == 0U || b.imported == 0U) return a.imported != 0U;
            return sort == 4 ? a.imported > b.imported : a.imported < b.imported;
        }
        if (a.name != b.name) return sort == 1 ? a.name > b.name : a.name < b.name;
        return a.id < b.id;
    });
    std::vector<int> shown;
    shown.reserve(keyed.size());
    for (const Keyed& item : keyed) shown.push_back(item.index);
    return shown;
}

bool LibraryFiltered(const ModBrowserState& state) {
    return state.search[0] != '\0' || !state.source.empty() || state.state != 0 ||
           state.compatibility != 0 || state.visibility != 0 || state.format != 0;
}

void ResetLibraryFilters(ModBrowserState& state) {
    state.search[0] = '\0';
    state.source.clear();
    state.sort = state.state = state.compatibility = state.visibility = state.format = 0;
}

std::string DkrLibrarySignature(const std::vector<DkrLibraryTrack>& tracks) {
    std::string signature;
    for (const auto& track : tracks) {
        signature += track.id + '\x1F' + track.name + '\x1F' + track.author + '\x1F' +
                     track.source + '\x1F' + std::to_string(track.bytes) +
                     (track.enabled ? '1' : '0') + (track.hd_textures ? '1' : '0') + '\x1E';
    }
    return signature;
}

void DrawModManagement(ModBrowserState& browser, bool characters, bool locked);

std::array<std::vector<LibraryCardView>, 2> g_library_card_views;

// The toolbar, filter panel, results line and card grid of one library.
void DrawModCardBrowser(float requested_width, bool characters,
                        const dkr::mods::ModLibraryView& mods, bool mods_locked,
                        const std::vector<DkrLibraryTrack>& dkr_tracks,
                        const std::function<void()>& import_mods) {
    using namespace dkr::mods;
    auto& state = g_mod_browsers[characters ? 1 : 0];
    const auto snapshot = characters ? mods.characters : mods.tracks;
    const bool locked = mods_locked || mods.busy;
    ImGui::PushID(characters ? "character-card-library" : "track-card-library");
    if (state.snapshot != snapshot) {
        state.snapshot = snapshot;
        state.all = snapshot != nullptr ? browser::cards(*snapshot, characters)
                                        : std::vector<browser::Card>{};
        state.filter_key.clear();
    }
    const unsigned active = browser::active_count(state.all);
    const float width = std::max(std::min(requested_width, ImGui::GetContentRegionAvail().x), 1.0F);
    const char* noun = characters ? "characters" : "tracks";

    // Toolbar: search, status, sort and the filter switch.
    const std::string filters_label = [&] {
        const int advanced = (!state.source.empty() ? 1 : 0) + (state.compatibility != 0 ? 1 : 0) +
                             (state.visibility != 0 ? 1 : 0) + (state.format != 0 ? 1 : 0);
        return advanced != 0 ? "Filters (" + std::to_string(advanced) + ")" : std::string("Filters");
    }();
    const std::string filters_id = filters_label + "##filters";
    const float filters_width = PaddockButtonWidth(filters_id.c_str(), PaddockButtonKind::Settings);
    const float field_label = PaddockFieldLabelType().line + 6.0F;
    static const std::vector<std::string> kStates{"All", "Active", "Inactive"};
    static const std::vector<std::string> kSorts{"Name A-Z", "Name Z-A", "Largest first",
                                                 "Smallest first", "Newest first", "Oldest first"};
    const auto search = [&](float x, float y, float w) {
        ImGui::SetCursorScreenPos({x, y});
        const PaddockFieldResult result = PaddockSearch(
            "##search", state.search, sizeof(state.search),
            "Search by name or source pack\xE2\x80\xA6", w);
        if (result == PaddockFieldResult::GamepadActivated) {
            RequestTextEntryKeyboard(characters ? TextEntryTarget::CustomCharacterSearch
                                                : TextEntryTarget::CustomTrackSearch);
        }
    };
    const auto selects = [&](float x, float y, float status_width, float sort_width) {
        ImGui::SetCursorScreenPos({x, y});
        PaddockSelect("status", "Status", &state.state, kStates, status_width);
        ImGui::SetCursorScreenPos({x + status_width + 12.0F, y});
        PaddockSelect("sort", "Sort by", &state.sort, kSorts, sort_width);
    };
    const auto filters_button = [&](float x, float y, float w) {
        ImGui::SetCursorScreenPos({x, y});
        if (PaddockButton(filters_id.c_str(), g_mods_page.filters_open ? PaddockButtonKind::SettingsSelected
                                                                       : PaddockButtonKind::Settings, w)) {
            g_mods_page.filters_open = !g_mods_page.filters_open;
            g_mods_page.filters_entered_at = PaddockClock();
        }
    };
    const ImVec2 top = ImGui::GetCursorScreenPos();
    const float row = field_label + 44.0F;
    {
    PaddockEnter enter(2);
    if (width >= 700.0F) {
        const float fixed = 120.0F + 150.0F + filters_width + 36.0F;
        const float search_width = std::max(width - fixed, 150.0F);
        search(top.x, top.y + field_label, search_width);
        selects(top.x + search_width + 12.0F, top.y, 120.0F, 150.0F);
        filters_button(top.x + width - filters_width, top.y + row - 42.0F, filters_width);
        ImGui::SetCursorScreenPos(top);
        ImGui::Dummy({width, row});
    } else if (width >= 420.0F) {
        search(top.x, top.y, width);
        const float half = (width - filters_width - 24.0F) * 0.5F;
        selects(top.x, top.y + 56.0F, half, half);
        filters_button(top.x + width - filters_width, top.y + 56.0F + row - 42.0F, filters_width);
        ImGui::SetCursorScreenPos(top);
        ImGui::Dummy({width, 56.0F + row});
    } else {
        search(top.x, top.y, width);
        const float half = (width - 10.0F) * 0.5F;
        selects(top.x, top.y + 54.0F, half, half);
        filters_button(top.x, top.y + 54.0F + row + 10.0F, width);
        ImGui::SetCursorScreenPos(top);
        ImGui::Dummy({width, 54.0F + row + 10.0F + 42.0F});
    }
    }

    if (g_mods_page.filters_open) {
        PaddockGap(16.0F);
        PaddockEnter enter(0, g_mods_page.filters_entered_at);
        std::set<std::string> sources;
        for (const auto& card : state.all) sources.insert(card.sources.begin(), card.sources.end());
        if (!characters) {
            for (const auto& track : dkr_tracks) sources.insert(track.source);
        }
        if (!state.source.empty() && !sources.contains(state.source)) state.source.clear();
        std::vector<std::string> source_items{"All packs"};
        int source_index = 0;
        for (const auto& source : sources) {
            if (source == state.source) source_index = static_cast<int>(source_items.size());
            source_items.push_back(source);
        }
        PaddockBox box(width, {16.0F, 16.0F});
        const int fields = characters ? 3 : 4;
        const int columns = std::clamp(static_cast<int>((box.Inner() + 16.0F) / 186.0F), 1, fields);
        const float field_width = (box.Inner() - 16.0F * (columns - 1)) / columns;
        const ImVec2 grid = ImGui::GetCursorScreenPos();
        int slot = 0;
        const auto place = [&]() {
            const int column = slot % columns;
            const int line = slot / columns;
            ImGui::SetCursorScreenPos({grid.x + column * (field_width + 16.0F),
                                       grid.y + line * (row + 16.0F)});
            ++slot;
        };
        static const std::vector<std::string> kFormats{"All tracks", "Legacy", "DKR"};
        static const std::vector<std::string> kCompatibility{"All", "Compatible", "Needs attention"};
        static const std::vector<std::string> kVisibility{"Visible", "All", "Hidden"};
        if (!characters) {
            place();
            PaddockSelect("format", "Track format", &state.format, kFormats, field_width);
        }
        place();
        PaddockSelect("compatibility", "Compatibility", &state.compatibility, kCompatibility, field_width);
        place();
        if (PaddockSelect("source", "Source pack", &source_index, source_items, field_width)) {
            state.source = source_index > 0 ? source_items[source_index] : std::string{};
        }
        place();
        PaddockSelect("visibility", "Visibility", &state.visibility, kVisibility, field_width);
        const int lines = (slot + columns - 1) / columns;
        ImGui::SetCursorScreenPos(grid);
        ImGui::Dummy({box.Inner(), lines * row + (lines - 1) * 16.0F});
        box.End([](ImDrawList* draw, ImVec2 a, ImVec2 b) {
            PaddockPanelStyle panel;
            panel.radii = PaddockRound(12.0F);
            panel.fill = PaddockRgb(0xFFFFFF, 5U);
            panel.ring = PaddockRgb(0xFFFFFF, 10U);
            panel.ring_width = 1.0F;
            PaddockPanel(draw, a, b, panel);
        });
    }

    const std::string key = std::string(state.search) + '|' + state.source + '|' +
        std::to_string(state.sort) + ':' + std::to_string(state.state) + ':' +
        std::to_string(state.compatibility) + ':' + std::to_string(state.visibility) + ':' +
        std::to_string(state.format) + ':' + std::to_string(g_mod_browser_revision) + '|' +
        (characters ? std::string{} : DkrLibrarySignature(dkr_tracks));
    if (state.filter_key != key) {
        state.shown = SelectLibraryEntries(state, dkr_tracks, characters);
        state.filter_key = key;
        state.layout_key.clear();
    }
    const std::size_t total = state.all.size() + (characters ? 0U : dkr_tracks.size());

    // Results line.
    {
        PaddockEnter enter(3);
        const ImVec2 at = ImGui::GetCursorScreenPos();
        const PaddockType type = PaddockReading(12.0F, false, 1.5F);
        const std::string count = std::to_string(state.shown.size()) + " of " +
                                  std::to_string(total) + ' ' + noun;
        PaddockDrawRun(ImGui::GetWindowDrawList(), type, {at.x, at.y + (52.0F - type.line) * 0.5F},
                       PaddockCol(0x9BB0BD), count.data(), count.data() + count.size());
        if (LibraryFiltered(state)) {
            const float clear = PaddockButtonWidth("Clear filters", PaddockButtonKind::Link);
            ImGui::SetCursorScreenPos({at.x + width - clear, at.y + 5.0F});
            if (PaddockButton("Clear filters##results", PaddockButtonKind::Link)) {
                ResetLibraryFilters(state);
            }
        }
        ImGui::SetCursorScreenPos(at);
        ImGui::Dummy({width, 52.0F});
    }
    if (characters) {
        const std::string slots = std::to_string(active) + " custom characters enabled";
        PaddockInlineNote(slots,
                          active >= MaxActiveStageCharacters
                              ? "The native identity range is full. Installed mods remain available."
                              : "Enable your collection; memory and asset limits are checked before launch. Four custom racers appear on the original stage. Highlight one and press L / R to browse extras. Mods without a custom portrait retain their original face.",
                          width);
        PaddockGap(18.0F);
    }

    bool manage = false;
    if (state.shown.empty()) {
        PaddockEnter enter(4);
        PaddockBox box(width, {24.0F, 48.0F});
        const bool any = total != 0U;
        const PaddockType heading = PaddockSign(23.0F, 1.25F);
        const std::string_view title = any ? "No matching mods" : "Your library starts here";
        const ImVec2 at = ImGui::GetCursorScreenPos();
        PaddockTextStyle heading_style;
        heading_style.colour = PaddockCol(0xFFF0C2);
        heading_style.shadow = PaddockCol(0x031623);
        heading_style.centre = true;
        float y = PaddockTextAt(ImGui::GetWindowDrawList(), heading, at, box.Inner(), title,
                                heading_style, true);
        y += 13.0F;
        const PaddockType body = PaddockReading(14.0F, false, 1.55F);
        PaddockTextStyle body_style;
        body_style.colour = PaddockCol(0xABC0CC);
        body_style.centre = true;
        y += PaddockTextAt(ImGui::GetWindowDrawList(), body, {at.x, at.y + y}, box.Inner(),
                           any ? "Try a different search or clear your filters to see more mods."
                               : "Import a legacy patch or a DKR track to add it here.",
                           body_style) + 13.0F;
        const char* action = any ? "Clear filters##empty" : "Import mods##empty";
        const auto kind = any ? PaddockButtonKind::Plain : PaddockButtonKind::Primary;
        const float action_width = PaddockButtonWidth(action, kind);
        ImGui::SetCursorScreenPos({at.x + (box.Inner() - action_width) * 0.5F, at.y + y});
        ImGui::BeginDisabled(!any && locked);
        if (PaddockButton(action, kind)) {
            if (any) {
                ResetLibraryFilters(state);
            } else if (import_mods) {
                import_mods();
            }
        }
        ImGui::EndDisabled();
        box.End([](ImDrawList* draw, ImVec2 a, ImVec2 b) {
            PaddockPanelStyle panel;
            panel.radii = PaddockRound(14.0F);
            panel.fill = PaddockRgb(0xFFFFFF, 4U);
            panel.ring = PaddockRgb(0xFFFFFF, 13U);
            panel.ring_width = 1.0F;
            PaddockPanel(draw, a, b, panel);
        });
    } else {
        const int columns = std::max(static_cast<int>((width + kModsCardGap) / (265.0F + kModsCardGap)), 1);
        const float card_width = std::floor((width - kModsCardGap * (columns - 1)) / columns);
        const std::string layout_key = key + '#' + std::to_string(card_width) + '#' +
            std::to_string(g_font_generation) + '#' + std::to_string(active) +
            (locked ? 'L' : 'U');
        auto& views = g_library_card_views[characters ? 1 : 0];
        if (state.layout_key != layout_key) {
            views.clear();
            views.reserve(state.shown.size());
            for (const int index : state.shown) {
                views.push_back(DescribeLibraryCard(
                    ResolveLibraryEntry(state, dkr_tracks, index), characters, active, locked));
            }
            state.row_heights.clear();
            for (std::size_t first = 0U; first < views.size(); first += columns) {
                float tallest = 0.0F;
                for (std::size_t index = first; index < std::min(first + columns, views.size()); ++index) {
                    tallest = std::max(tallest, MeasureLibraryCard(views[index], card_width));
                }
                state.row_heights.push_back(tallest);
            }
            state.layout_key = layout_key;
        }
        const ImVec2 grid = ImGui::GetCursorScreenPos();
        float y = grid.y;
        for (std::size_t line = 0U; line < state.row_heights.size(); ++line) {
            const float height = state.row_heights[line];
            const ImVec2 row_min{grid.x, y};
            const ImVec2 row_max{grid.x + width, y + height + 5.0F};
            if (ImGui::IsRectVisible(row_min, row_max)) {
                for (int column = 0; column < columns; ++column) {
                    const std::size_t index = line * columns + column;
                    if (index >= views.size()) break;
                    const LibraryCardView& view = views[index];
                    ImGui::PushID(view.native ? ("dkr:" + view.id).c_str() : view.id.c_str());
                    PaddockEnter enter(static_cast<int>(index) + 4);
                    const ImVec2 at{grid.x + column * (card_width + kModsCardGap), y};
                    if (DrawLibraryCard(view, at, card_width, height, characters, locked)) {
                        if (view.native) {
                            g_mods_page.dkr_details_id = view.id;
                            g_mods_page.request_dkr_details = true;
                        } else {
                            state.manage_id = view.id;
                            manage = true;
                        }
                    }
                    ImGui::PopID();
                }
            }
            y += height + kModsCardGap;
        }
        ImGui::SetCursorScreenPos(grid);
        ImGui::Dummy({width, std::max(y - kModsCardGap - grid.y + 5.0F, 0.0F)});
    }
    if (manage) ImGui::OpenPopup("Manage custom mod");
    DrawModManagement(state, characters, locked);
    ImGui::PopID();
}

// ------------------------------------------------------------ modals

void PaddockModalText(const ImVec4& colour, std::string_view text) {
    ImGui::PushStyleColor(ImGuiCol_Text, colour);
    ImGui::TextWrapped("%.*s", static_cast<int>(text.size()), text.data());
    ImGui::PopStyleColor();
}

void PaddockModalText(std::string_view text) {
    ImGui::TextWrapped("%.*s", static_cast<int>(text.size()), text.data());
}

bool PaddockModalButton(const char* label, float height = 42.0F) {
    return PaddockButton(label, PaddockButtonKind::Flat,
                         std::max(ImGui::GetContentRegionAvail().x, 1.0F), height);
}

void DrawModManagement(ModBrowserState& browser, bool characters, bool locked) {
    using namespace dkr::mods;
    const auto kind = characters ? TrackCatalog::Kind::Character : TrackCatalog::Kind::Track;
    const auto find_card = [&](const std::string& id) -> const browser::Card* {
        const auto found = std::find_if(browser.all.begin(), browser.all.end(),
                                        [&](const auto& card) { return card.item.id == id; });
        return found == browser.all.end() ? nullptr : &*found;
    };
    const auto modal_size = [] {
        const auto display = ImGui::GetIO().DisplaySize;
        ImGui::SetNextWindowSizeConstraints(
            {std::max(240.0F, std::min(540.0F, display.x - 48)), 0},
            {std::max(240.0F, std::min(700.0F, display.x - 32)), std::max(200.0F, display.y - 48)});
    };
    const PaddockFlatScope paddock;
    bool request_hide = false;
    bool request_remove = false;
    modal_size();
    if (BeginPaddockModalWindow("Manage custom mod", ImGuiWindowFlags_AlwaysAutoResize)) {
        const auto* card = find_card(browser.manage_id);
        if (!card) {
            PaddockModalText("This mod is no longer in the installed library.");
        } else {
            const auto& item = card->item;
            PaddockModalText(characters ? "MANAGE CUSTOM CHARACTER" : "MANAGE CUSTOM TRACK");
            ImGui::Separator();
            PaddockModalText(item.name);
            PaddockModalText(item.enabled ? kAccent : kWarm,
                             item.hidden ? "HIDDEN" : item.enabled ? "ACTIVE" : "INACTIVE");
            for (const auto& source : card->sources) PaddockModalText("SOURCE PACK: " + source);
            PaddockModalText(std::string("PREPARED FOR: ") +
                             (card->revisions == 3 ? "ROM v 1.0 / v 1.1"
                              : card->revisions == 1 ? "ROM v 1.0" : "ROM v 1.1"));
            PaddockModalText("MANAGED SIZE: " + FormatManagedTexturePackSize(item.managed_bytes) +
                             " (prepared variants; retained import material excluded)");
            if (item.imported_at) {
                const std::chrono::year_month_day date{std::chrono::floor<std::chrono::days>(
                    std::chrono::sys_seconds{std::chrono::seconds{item.imported_at}})};
                ImGui::Text("IMPORTED: %04d-%02u-%02u", static_cast<int>(date.year()),
                            static_cast<unsigned>(date.month()), static_cast<unsigned>(date.day()));
            } else {
                PaddockModalText("IMPORTED: Unknown (older import)");
            }
            if (!item.details.empty()) PaddockModalText(kWarm, item.details);
            if (!characters) {
                ImGui::TextWrapped("VEHICLES: %s%s%s", item.vehicles & 1 ? "Car " : "",
                                   item.vehicles & 2 ? "Hovercraft " : "", item.vehicles & 4 ? "Plane" : "");
            }
            ImGui::Dummy({0, 8});
            if (locked) {
                PaddockModalText(kWarm, "Return to the launcher and leave the lobby to change mods. "
                                        "Busy operations must finish first.");
            }
            const unsigned active = browser::active_count(browser.all);
            if (characters && !item.enabled && active >= MaxActiveStageCharacters) {
                PaddockModalText("The native character identity range is full. Installed mods remain available.");
            }
            const bool can_enable = browser::can_activate(*card, characters, active,
                                                          g_mod_browser_revision, locked);
            ImGui::BeginDisabled(locked || (!item.enabled && !can_enable));
            if (PaddockModalButton(item.enabled ? "DEACTIVATE MOD" : "ACTIVATE MOD")) {
                g_legacy_imports.set_enabled(kind, item.id, !item.enabled);
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndDisabled();
            ImGui::BeginDisabled(locked);
            if (PaddockModalButton(item.hidden ? "RESTORE TO LIBRARY" : "HIDE FROM LIBRARY")) {
                if (item.hidden) {
                    g_legacy_imports.set_hidden(kind, item.id, false);
                } else {
                    browser.hide_id = item.id;
                    request_hide = true;
                }
                ImGui::CloseCurrentPopup();
            }
            if (PaddockModalButton("REMOVE MOD...")) {
                browser.remove_id = item.id;
                request_remove = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndDisabled();
            if (PaddockModalButton("OPEN MANAGED LOCATION")) {
                const auto path = g_config_directory / "mods" / "legacy" /
                    (characters ? "prepared-characters" : "prepared") / item.group / item.storage;
                dkr::runtime::support::open_directory(path, g_legacy_import_status);
            }
            if (ImGui::CollapsingHeader("IMPORT DETAILS")) {
                ImGui::TextWrapped("CONTENT ID: %s", item.id.c_str());
                PaddockModalText("Source imports are retained for re-preparation. This mod uses isolated "
                                 "saves; hiding or removing it never deletes saves.");
                ImGui::BeginDisabled(locked || item.review.empty());
                if (PaddockModalButton("PREPARE SOURCE IMPORT AGAIN")) {
                    g_legacy_imports.prepare_review(item.review, ModReviewRoms());
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndDisabled();
                PaddockModalText("This explicitly restores removed entries from this source import. "
                                 "Existing hidden entries remain hidden.");
            }
        }
        if (PaddockModalButton("CLOSE")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if (request_hide) ImGui::OpenPopup("Hide custom mod?");
    if (request_remove) ImGui::OpenPopup("Remove custom mod?");
    for (const bool remove : {false, true}) {
        modal_size();
        if (!BeginPaddockModalWindow(remove ? "Remove custom mod?" : "Hide custom mod?",
                                     ImGuiWindowFlags_AlwaysAutoResize)) {
            continue;
        }
        const auto* card = find_card(remove ? browser.remove_id : browser.hide_id);
        if (card) {
            PaddockModalText(card->item.name);
            ImGui::Separator();
            PaddockModalText(remove
                ? "Remove this mod and all its prepared ROM variants? Other tracks and characters from "
                  "the same pack, source files, retained import material and all saves are kept."
                : "Deactivate and hide this mod? Its files and saves are kept. Use Visibility: Hidden "
                  "to restore the card. Restoring does not activate it.");
            if (remove) {
                PaddockModalText(kWarm, "Prepared content is deleted. Reimport its source patch to install it again.");
            }
            ImGui::BeginDisabled(locked);
            if (PaddockModalButton(remove ? "REMOVE THIS MOD" : "DEACTIVATE AND HIDE", 44)) {
                if (remove) {
                    g_legacy_imports.remove(kind, card->item.id);
                } else {
                    g_legacy_imports.set_hidden(kind, card->item.id, true);
                }
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndDisabled();
        } else {
            PaddockModalText("This mod is no longer installed.");
        }
        if (PaddockModalButton("CANCEL", 44)) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

// What the import chooser can start. The caller owns the pickers.
struct ModImportActions {
    bool rom_ready = false;
    bool locked = false;
    std::function<void()> choose_patch;
    std::function<void()> choose_dkrmap_folder;
    std::function<void()> choose_track_zip;
    std::function<void()> choose_rom;
};

void DrawModImportOption(const char* id, PaddockFormat format, const char* title,
                         const char* body, float width, const std::function<void()>& content) {
    ImGui::PushID(id);
    PaddockBox box(width, {18.0F, 18.0F});
    const ImVec2 at = ImGui::GetCursorScreenPos();
    PaddockFormatTag(ImGui::GetWindowDrawList(), at, format);
    ImGui::Dummy({box.Inner(), PaddockFormatTagSize(format).y});
    PaddockGap(12.0F);
    PaddockHeading(title, 24.0F, 0xFFF0C2, box.Inner(), 1.2F);
    PaddockGap(12.0F);
    PaddockText(PaddockReading(14.0F, false, 1.55F), PaddockRgb(0xC7DCE6), body, box.Inner());
    PaddockGap(14.0F);
    content();
    box.End([](ImDrawList* draw, ImVec2 a, ImVec2 b) {
        PaddockPanelStyle panel;
        panel.radii = {10.0F, 20.0F, 10.0F, 10.0F};
        panel.fill = PaddockRgb(0x0B304B);
        panel.border = PaddockRgb(0x39657B);
        panel.border_width = 1.0F;
        PaddockPanel(draw, a, b, panel);
    });
    ImGui::PopID();
}

void DrawModImportChooser(const ModImportActions& actions) {
    constexpr const char* kName = "Import mods";
    if (g_mods_page.request_import_chooser) {
        ImGui::OpenPopup(kName);
        g_mods_page.request_import_chooser = false;
    }
    if (!ImGui::IsPopupOpen(kName)) return;
    const PaddockFlatScope paddock;
    const auto display = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowSize({std::min(680.0F, display.x - 48.0F), 0.0F}, ImGuiCond_Appearing);
    if (!BeginPaddockModalWindow(kName, ImGuiWindowFlags_AlwaysAutoResize |
                                            ImGuiWindowFlags_NoScrollbar)) {
        return;
    }
    const float width = std::max(ImGui::GetContentRegionAvail().x, 1.0F);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {0.0F, 0.0F});
    PaddockText(PaddockReading(14.0F, false, 1.55F), PaddockRgb(0xFFF6DA),
                "Choose the format you downloaded. Both types appear in My mods.", width);
    PaddockGap(18.0F);
    DrawModImportOption("legacy", PaddockFormat::Legacy, "ROM patches",
                        ".xdelta files or ZIP packs containing legacy tracks and characters.", width, [&] {
        if (!actions.rom_ready) {
            const PaddockType body = PaddockReading(14.0F, false, 1.55F);
            constexpr std::string_view sentence =
                "Load the original ROM first so the patch can be prepared.";
            const float inner = width - 36.0F;
            const float sentence_width = PaddockMeasure(body, sentence) + 4.0F;
            const float link_width = PaddockMeasure(PaddockReading(14.0F, true), "Choose ROM");
            const ImVec2 at = ImGui::GetCursorScreenPos();
            if (sentence_width + link_width <= inner) {
                PaddockDrawRun(ImGui::GetWindowDrawList(), body, {at.x, at.y + (40.0F - body.line) * 0.5F},
                               PaddockCol(0xC7DCE6), sentence.data(), sentence.data() + sentence.size());
                ImGui::SetCursorScreenPos({at.x + sentence_width, at.y});
            } else {
                PaddockText(body, PaddockRgb(0xC7DCE6), sentence, inner);
            }
            if (PaddockTextLink("Choose ROM##import")) {
                ImGui::CloseCurrentPopup();
                if (actions.choose_rom) actions.choose_rom();
            }
            PaddockGap(10.0F);
        }
        ImGui::BeginDisabled(actions.locked || !actions.rom_ready);
        if (PaddockButton("Choose patch file")) {
            ImGui::CloseCurrentPopup();
            if (actions.choose_patch) actions.choose_patch();
        }
        ImGui::EndDisabled();
    });
    PaddockGap(16.0F);
    DrawModImportOption("dkr", PaddockFormat::Dkr, "DKR-R tracks",
                        "A .dkrmap folder or a ZIP of a native track. You can import it before loading a ROM.",
                        width, [&] {
        ImGui::BeginDisabled(actions.locked);
        if (PaddockButton("Choose .dkrmap folder")) {
            ImGui::CloseCurrentPopup();
            if (actions.choose_dkrmap_folder) actions.choose_dkrmap_folder();
        }
        ImGui::SameLine(0.0F, 10.0F);
        if (PaddockButton("Choose track ZIP")) {
            ImGui::CloseCurrentPopup();
            if (actions.choose_track_zip) actions.choose_track_zip();
        }
        ImGui::EndDisabled();
    });
    PaddockGap(18.0F);
    if (PaddockButton("Cancel##import-chooser", PaddockButtonKind::Link)) ImGui::CloseCurrentPopup();
    ImGui::PopStyleVar();
    ImGui::EndPopup();
}

void DrawDkrTrackDetails(const std::vector<DkrLibraryTrack>& tracks, bool rom_ready,
                         bool locked = false) {
    constexpr const char* kName = "Track details";
    if (g_mods_page.request_dkr_details) {
        ImGui::OpenPopup(kName);
        g_mods_page.request_dkr_details = false;
        g_mods_page.dkr_remove_confirm = false;
    }
    if (!ImGui::IsPopupOpen(kName)) return;
    const PaddockFlatScope paddock;
    const auto display = ImGui::GetIO().DisplaySize;
    const float modal_width = std::max(240.0F, std::min(570.0F, display.x - 48.0F));
    ImGui::SetNextWindowSizeConstraints({modal_width, 0.0F},
                                       {modal_width, std::max(200.0F, display.y - 48.0F)});
    if (!BeginPaddockModalWindow(kName, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }
    const float width = std::max(ImGui::GetContentRegionAvail().x, 1.0F);
    const auto found = std::find_if(tracks.begin(), tracks.end(), [](const DkrLibraryTrack& track) {
        return track.id == g_mods_page.dkr_details_id;
    });
    if (found == tracks.end()) {
        PaddockModalText("This track is no longer in the library.");
    } else if (g_mods_page.dkr_remove_confirm) {
        PaddockModalText(found->name);
        PaddockModalText("Uninstall this track from DKR-R? Its installed .dkrmap copy will be deleted. "
                        "Original source files, saves and HD texture packs will be kept. "
                        "You can import the track again later.");
        ImGui::BeginDisabled(locked || !found->installed);
        if (PaddockModalButton("UNINSTALL TRACK", 44)) {
            g_mods_page.uninstall_track_request = found->id;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();
        if (PaddockModalButton("CANCEL", 44)) g_mods_page.dkr_remove_confirm = false;
    } else {
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {0.0F, 0.0F});
        const ImVec2 at = ImGui::GetCursorScreenPos();
        PaddockFormatTag(ImGui::GetWindowDrawList(), at, PaddockFormat::Dkr);
        ImGui::Dummy({width, PaddockFormatTagSize(PaddockFormat::Dkr).y});
        PaddockGap(12.0F);
        PaddockHeading(found->name, 24.0F, 0xFFF0C2, width, 1.2F);
        PaddockGap(12.0F);
        const PaddockType body = PaddockReading(14.0F, false, 1.55F);
        const auto paragraph = [&](std::string_view text) {
            PaddockGap(12.0F);
            PaddockText(body, PaddockRgb(0xFFF6DA), text, width);
        };
        paragraph(found->author.empty() ? std::string("Native .dkrmap track") : "By " + found->author);
        paragraph(found->kept_online ? (found->enabled ? "Kept from an online session. Enabled for offline play."
                                                     : "Kept from an online session. Inactive until you turn it on in My Mods.") : found->installed
            ? "Installed. This course appears automatically in the in-game track menu."
            : "Read from your working folder. Source files are managed in Track Lab.");
        paragraph("Use Track Lab to test work in progress from your working folder. "
                  "Native tracks use the Modern graphics profile.");
        if (!rom_ready) paragraph("Load your original Diddy Kong Racing ROM in Play before racing.");
        if (!found->hd_pack_id.empty()) {
            PaddockGap(13.0F);
            if (PaddockButton("Manage HD textures")) {
                g_mods_page.manage_pack_request = found->hd_pack_id;
                ImGui::CloseCurrentPopup();
            }
        }
        if (found->installed) {
            PaddockGap(13.0F);
            ImGui::BeginDisabled(locked);
            if (PaddockButton("Uninstall track")) g_mods_page.dkr_remove_confirm = true;
            ImGui::EndDisabled();
        } else {
            paragraph("To remove this track, move its .dkrmap out of your working folder and rescan, "
                      "or stop watching that folder in Track Lab.");
        }
        if (locked) paragraph("Return to the launcher and leave the lobby to change tracks. "
                              "Wait for any current import to finish.");
        PaddockGap(13.0F);
        ImGui::PopStyleVar();
    }
    if (PaddockButton("Close##dkr-details", PaddockButtonKind::Link)) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

// ------------------------------------------------------------ page controls

float PaddockImportButtonWidth(const char* label) {
    const PaddockType type = PaddockSign(22.0F, 1.1F);
    return std::ceil(2.0F + 12.0F + 30.0F + 12.0F +
                     PaddockMeasure(type, label, PaddockLabelEnd(label)) + 20.0F + 2.0F);
}

// The painted "+ Import mods" sign (.mods-import-button).
bool PaddockImportButton(const char* label, float forced_width = 0.0F) {
    const PaddockType type = PaddockSign(22.0F, 1.1F);
    const char* end = PaddockLabelEnd(label);
    const float natural = PaddockImportButtonWidth(label);
    const float width = forced_width > 0.0F ? forced_width : natural;
    const PaddockPress press = PaddockBeginPress(label, {width, 56.0F});
    ImDrawList* draw = ImGui::GetWindowDrawList();
    PaddockPanelStyle panel;
    panel.radii = {8.0F, 18.0F, 8.0F, 8.0F};
    panel.fill = PaddockMix(PaddockRgb(0xB94B1A), PaddockRgb(0xD65B1E), press.hover);
    panel.border = PaddockMix(PaddockRgb(0xFFD06B), PaddockRgb(0xFFE39C), press.hover);
    panel.border_width = 2.0F;
    panel.drop = PaddockRgb(0x5E240F);
    panel.drop_offset = 4.0F;
    panel.highlight = PaddockRgb(0xFFFFFF, 38U);
    panel.highlight_width = 2.0F;
    PaddockPanel(draw, press.min, press.max, panel);
    const float left = press.min.x + (width - natural) * 0.5F + 14.0F;
    const ImVec2 symbol{left, press.min.y + 13.0F};
    PaddockFill(draw, symbol, {symbol.x + 30.0F, symbol.y + 30.0F}, PaddockRound(5.0F),
                PaddockCol(0x6F2B11));
    // Two centred strokes, which a font glyph's baseline would not give
    // (.import-button-symbol).
    const ImU32 ink = PaddockCol(0xFFF3C9);
    PaddockFill(draw, {symbol.x + 8.0F, symbol.y + 14.0F}, {symbol.x + 22.0F, symbol.y + 16.0F},
                PaddockRound(1.0F), ink);
    PaddockFill(draw, {symbol.x + 14.0F, symbol.y + 8.0F}, {symbol.x + 16.0F, symbol.y + 22.0F},
                PaddockRound(1.0F), ink);
    const float text_top = press.min.y + (56.0F - type.line) * 0.5F;
    PaddockDrawRun(draw, type, {symbol.x + 42.0F, text_top + 2.0F}, PaddockCol(0x632706),
                   label, end);
    PaddockDrawRun(draw, type, {symbol.x + 42.0F, text_top}, PaddockCol(0xFFF3C9), label, end);
    PaddockEndPress(press, 18.0F);
    return press.pressed;
}

// Tracks / Characters (.mods-categories). Returns the selected index.
int DrawLibraryCategories(int current, std::size_t tracks, std::size_t characters) {
    const PaddockType type = PaddockSign(19.0F, 1.4F);
    const PaddockType count_type = PaddockReading(12.0F, true, 1.5F);
    const std::array<const char*, 2> labels{{"Tracks", "Characters"}};
    const std::array<std::string, 2> counts{{std::to_string(tracks), std::to_string(characters)}};
    std::array<float, 2> widths{};
    for (std::size_t index = 0U; index < 2U; ++index) {
        const float pill = std::max(23.0F, PaddockMeasure(count_type, counts[index]) + 12.0F);
        widths[index] = std::ceil(2.0F + 26.0F + PaddockMeasure(type, labels[index]) + 9.0F + pill);
    }
    const float button_height = std::ceil(type.line + 14.0F + 2.0F);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float total = 8.0F + widths[0] + widths[1] + 4.0F;
    const ImVec2 end{origin.x + total, origin.y + button_height + 8.0F};
    ImDrawList* draw = ImGui::GetWindowDrawList();
    PaddockPanelStyle frame;
    frame.radii = PaddockRound(12.0F);
    frame.fill = PaddockRgb(0x031C2C);
    frame.ring = PaddockRgb(0x316782);
    frame.ring_width = 1.0F;
    PaddockPanel(draw, origin, end, frame);
    int selected = current;
    float x = origin.x + 4.0F;
    for (int index = 0; index < 2; ++index) {
        ImGui::SetCursorScreenPos({x, origin.y + 4.0F});
        const bool pressed_now = current == index;
        const PaddockPress press = PaddockBeginPress(labels[index], {widths[index], button_height});
        if (press.pressed) selected = index;
        const float on = PaddockEase(PaddockTween(PaddockKey("category", press.id),
                                                  pressed_now, 0.15F, 0.15F));
        const ImU32 fill = PaddockMix(PaddockMix(PaddockRgb(0x244353, 0U), PaddockRgb(0x244353),
                                                 press.hover),
                                      PaddockRgb(0x087C91), on);
        if (on > 0.0F) {
            PaddockFill(draw, {press.min.x, press.min.y + 2.0F}, {press.max.x, press.max.y + 2.0F},
                        PaddockRound(8.0F), PaddockCol(0x03131E, static_cast<unsigned>(255.0F * on)));
        }
        PaddockFill(draw, press.min, press.max, PaddockRound(8.0F), PaddockApply(fill));
        if (on > 0.0F) {
            draw->PushClipRect({press.min.x, press.min.y}, {press.max.x, press.min.y + 2.0F}, true);
            PaddockFill(draw, press.min, press.max, PaddockRound(8.0F),
                        PaddockCol(0xFFFFFF, static_cast<unsigned>(36.0F * on)));
            draw->PopClipRect();
        }
        if (press.hover > 0.0F) {
            PaddockStroke(draw, press.min, press.max, PaddockRound(8.0F),
                          PaddockCol(0x7A9BA7, static_cast<unsigned>(255.0F * press.hover)), 1.0F);
        }
        const ImU32 text = PaddockApply(PaddockMix(PaddockRgb(0xBED9E5), PaddockRgb(0xFFF4CE), on));
        const float top = press.min.y + 8.0F;
        const float label_width = PaddockMeasure(type, labels[index]);
        const char* label_end = labels[index] + std::strlen(labels[index]);
        PaddockDrawRun(draw, type, {press.min.x + 14.0F, top + 2.0F}, PaddockCol(0x031623),
                       labels[index], label_end);
        PaddockDrawRun(draw, type, {press.min.x + 14.0F, top}, text, labels[index], label_end);
        const float pill_x = press.min.x + 14.0F + label_width + 9.0F;
        const float pill_width = std::max(23.0F, PaddockMeasure(count_type, counts[index]) + 12.0F);
        const float pill_top = press.min.y + (button_height - count_type.line) * 0.5F;
        PaddockFill(draw, {pill_x, pill_top}, {pill_x + pill_width, pill_top + count_type.line},
                    PaddockRound(5.0F), PaddockCol(0xFFFFFF, 13U));
        const float digits = PaddockMeasure(count_type, counts[index]);
        PaddockDrawRun(draw, count_type, {pill_x + (pill_width - digits) * 0.5F, pill_top}, text,
                       counts[index].data(), counts[index].data() + counts[index].size());
        PaddockEndPress(press, 8.0F);
        x += widths[index] + 4.0F;
    }
    ImGui::SetCursorScreenPos(origin);
    ImGui::Dummy({total, end.y - origin.y});
    return selected;
}
