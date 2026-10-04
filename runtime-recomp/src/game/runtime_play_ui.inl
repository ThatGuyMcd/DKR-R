// Included by runtime_ui.cpp inside its UI namespace, after
// runtime_mod_library_ui.inl. Presentation only: ROM selection, launch and
// Track Lab changes go through the same launcher helpers as before.
//
// PLAY in the look of tools/launcher-html (pages/play.js, styles/pages.css):
// the ROM pass, what changes START, and START itself.

// ------------------------------------------------------------ race buttons

// A launcher race button painted in the paddock's CSS pixels (.race-button),
// so pages can give it depth, a soft hover and their own press feedback.
struct RaceButtonLook {
    unsigned fill = 0x0A6EA1;       // --blue
    unsigned border = 0x296B70;     // --border
    unsigned border_alpha = 255U;
    unsigned text = 0xFFF6DA;
    float font_px = 19.0F;
    float drop = 0.0F;              // box-shadow: 0 <drop> 0 <drop_colour>
    unsigned drop_colour = 0x000000;
    unsigned highlight_alpha = 0U;  // box-shadow: inset 0 1px 0 #fff<alpha>
    unsigned ring_alpha = 0U;       // box-shadow: 0 0 0 1px #fff<alpha>
    float hover_in = 0.0F;          // checker fade-in; 0 is instant
    float ellipsis_spread = 0.0F;
    bool sink = false;              // presses down into its base, no scale
    bool text_shadow = true;        // text-shadow: 0 2px 0 #00000038
};

bool PaddockRaceButton(const char* label, ImVec2 size, const RaceButtonLook& look) {
    const PaddockPress press = PaddockBeginPress(label, size, 0.15F, 0.15F);
    const bool lit = press.hovered || press.focused;
    const float amount = look.hover_in > 0.0F
        ? PaddockEase(PaddockTween(PaddockKey("race-hover", press.id), lit,
                                   look.hover_in, 0.0F))
        : (lit ? 1.0F : 0.0F);
    const float sunk = look.sink
        ? PaddockEase(PaddockTween(PaddockKey("race-sink", press.id),
                                   press.held && press.hovered, 0.08F, 0.2F))
        : 0.0F;
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const float offset = 2.0F * sunk;
    const ImVec2 a{press.min.x, press.min.y + offset};
    const ImVec2 b{press.max.x, press.max.y + offset};
    const PaddockRadii radii = PaddockRound(12.0F);
    if (look.ring_alpha != 0U) {
        PaddockStroke(draw, {press.min.x - 1.0F, press.min.y - 1.0F},
                      {press.max.x + 1.0F, press.max.y + 1.0F}, PaddockRound(13.0F),
                      PaddockCol(0xFFFFFF, look.ring_alpha), 1.0F);
    }
    if (look.drop > 0.0F) {
        const float drop = look.drop - offset;
        PaddockFill(draw, {a.x, a.y + drop}, {b.x, b.y + drop}, radii,
                    PaddockCol(look.drop_colour));
    }
    PaddockFill(draw, a, b, radii, PaddockCol(look.fill));
    if (look.highlight_alpha != 0U) {
        draw->PushClipRect({a.x, a.y + 1.0F}, {b.x, a.y + 2.0F}, true);
        PaddockFill(draw, {a.x + 1.0F, a.y + 1.0F}, {b.x - 1.0F, b.y - 1.0F},
                    PaddockRound(11.0F), PaddockCol(0xFFFFFF, look.highlight_alpha));
        draw->PopClipRect();
    }
    const ImU32 border = PaddockMix(PaddockRgb(look.border, look.border_alpha),
                                    PaddockRgb(0xFFDA63), press.hover);
    PaddockStroke(draw, a, b, radii, PaddockApply(border), 1.0F);

    const PaddockType type = PaddockSign(look.font_px, 1.0F, 0.0F);
    const char* end = PaddockLabelEnd(label);
    if (lit && amount > 0.0F) {
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 12.0F);
        ImGui::DrawRaceButtonHover(label, a, b, amount,
                                   look.hover_in > 0.0F ? 0.9F + 0.1F * amount : 1.0F,
                                   type.font, type.size, look.ellipsis_spread);
        ImGui::PopStyleVar();
    } else {
        const float text_width = ImGui::RaceLabelWidth(type.font, type.size, label, end,
                                                       look.ellipsis_spread);
        const float text_height =
            type.font->CalcTextSizeA(type.size, FLT_MAX, 0.0F, label, end).y;
        const ImVec2 at{std::round(a.x + (b.x - a.x - text_width) * 0.5F),
                        std::round(a.y + (b.y - a.y - text_height) * 0.5F)};
        draw->PushClipRect(a, b, true);
        if (look.text_shadow) {
            ImGui::DrawRaceLabel(draw, type.font, type.size, {at.x, at.y + 2.0F},
                                 PaddockCol(0x000000, 56U), label, end,
                                 look.ellipsis_spread);
        }
        ImGui::DrawRaceLabel(draw, type.font, type.size, at, PaddockCol(look.text),
                             label, end, look.ellipsis_spread);
        draw->PopClipRect();
    }
    PaddockEndPress(press, 12.0F, look.sink ? 1.0F : 0.96F);
    return press.pressed;
}

float PaddockRaceButtonWidth(const char* label, float font_px = 19.0F,
                             float ellipsis_spread = 0.0F) {
    const PaddockType type = PaddockSign(font_px, 1.0F, 0.0F);
    const char* end = PaddockLabelEnd(label);
    return std::ceil(ImGui::RaceLabelWidth(type.font, type.size, label, end,
                                           ellipsis_spread) + 32.0F + 2.0F);
}

// ------------------------------------------------------------ play page

constexpr const char* kRomChoosePrompt =
    "Choose your legally obtained Diddy Kong Racing Game Pak.";

struct PlayPageContext {
    std::filesystem::path& selected_rom;
    std::string& rom_status;
    bool& rom_ready;
    std::vector<RomCatalogEntry>& rom_catalog;
    bool& launch_requested;
};

// The launcher's ROM selection while a launcher frame is being drawn; the
// in-game overlay has none.
const PlayPageContext* g_launcher_rom = nullptr;

enum class RomSelectResult { None, Switched, Add };
// Defined in runtime_settings_ui.inl.
RomSelectResult DrawRomSelect(const char* id, float width, bool compact, bool disabled, const char* reason);

// Something that changes what START does, shown right above it.
struct PlayNotice {
    std::string key;
    std::string title;
    std::string body;
    bool warning = false;
    std::string action;
};

struct PlayPageState {
    ImGuiContext* context = nullptr;
    int last_frame = -100;
    bool was_ready = false;
    double arrived_at = -10.0;             // a ROM was just loaded (PaddockClock)
    // The last ROM removed from the list and where it sat, so Remove can be
    // undone while the page stays open.
    std::filesystem::path removed;
    std::size_t removed_index = 0;
    bool notices_known = false;            // nothing animates on page load
    std::set<std::string> shown;
    std::map<std::string, double> entered;
    std::optional<PlayNotice> leaving;
    float leaving_height = 0.0F;
    double leaving_at = -10.0;
    enum class Focus { None, Start, Browse, Rom } focus = Focus::None;
};
PlayPageState g_play_page;

// Content that fades up by `offset` px after `delay` seconds.
class PaddockFadeUp {
public:
    PaddockFadeUp(double started_at, float delay, float duration, float offset)
        : draw_(ImGui::GetWindowDrawList()),
          first_vertex_(draw_->VtxBuffer.Size),
          started_at_(started_at), delay_(delay), duration_(duration),
          offset_(offset) {}
    PaddockFadeUp(const PaddockFadeUp&) = delete;
    PaddockFadeUp& operator=(const PaddockFadeUp&) = delete;
    ~PaddockFadeUp() {
        const float elapsed = static_cast<float>(PaddockClock() - started_at_) - delay_;
        const float progress = PaddockEase(elapsed / duration_);
        if (progress >= 1.0F) return;
        for (int vertex = first_vertex_; vertex < draw_->VtxBuffer.Size; ++vertex) {
            ImDrawVert& v = draw_->VtxBuffer[vertex];
            v.pos.y += offset_ * (1.0F - progress);
            const ImU32 alpha = (v.col >> IM_COL32_A_SHIFT) & 0xFFU;
            v.col = (v.col & ~IM_COL32_A_MASK) |
                (static_cast<ImU32>(std::lround(alpha * progress)) << IM_COL32_A_SHIFT);
        }
    }

private:
    ImDrawList* draw_;
    int first_vertex_;
    double started_at_;
    float delay_;
    float duration_;
    float offset_;
};

// .play-text-button: an underlined action on the cream pass.
float PlayTextButtonWidth(const char* label) {
    return std::ceil(PaddockMeasure(PaddockReading(14.0F, true, 1.0F), label,
                                    PaddockLabelEnd(label)) + 16.0F);
}

bool PlayTextButton(const char* label, bool destructive) {
    const PaddockType type = PaddockReading(14.0F, true, 1.0F);
    const char* end = PaddockLabelEnd(label);
    const PaddockPress press = PaddockBeginPress(label, {PlayTextButtonWidth(label), 44.0F});
    ImDrawList* draw = ImGui::GetWindowDrawList();
    PaddockFill(draw, press.min, press.max, PaddockRound(6.0F),
                PaddockCol(0x091B24, static_cast<unsigned>(16.0F * press.hover)));
    const ImU32 colour = PaddockCol(destructive ? 0x8A2A17U : 0x0B5963U);
    const float top = std::round(press.min.y + (44.0F - type.line) * 0.5F);
    PaddockDrawRun(draw, type, {press.min.x + 8.0F, top}, colour, label, end);
    const float baseline = top + (type.line - type.content) * 0.5F + type.ascent;
    draw->AddRectFilled({press.min.x + 8.0F, std::round(baseline + 4.0F)},
                        {press.max.x - 8.0F, std::round(baseline + 5.0F)}, colour);
    PaddockEndPress(press, 6.0F);
    return press.pressed;
}

// The action of a launch notice (.play-notice .mods-button).
float PlayNoticeButtonWidth(const char* label) {
    return std::ceil(PaddockMeasure(PaddockReading(13.0F, true, 1.4F), label,
                                    PaddockLabelEnd(label)) + 32.0F);
}

bool PlayNoticeButton(const char* label) {
    const PaddockType type = PaddockReading(13.0F, true, 1.4F);
    const char* end = PaddockLabelEnd(label);
    const PaddockPress press = PaddockBeginPress(label, {PlayNoticeButtonWidth(label), 42.0F});
    ImDrawList* draw = ImGui::GetWindowDrawList();
    PaddockStroke(draw, {press.min.x - 1.0F, press.min.y - 1.0F},
                  {press.max.x + 1.0F, press.max.y + 1.0F}, PaddockRound(7.0F),
                  PaddockCol(0xFFFFFF, static_cast<unsigned>(31.0F + 20.0F * press.hover)), 1.0F);
    PaddockFill(draw, press.min, press.max, PaddockRound(6.0F),
                PaddockCol(0xFFFFFF, static_cast<unsigned>(15.0F + 10.0F * press.hover)));
    PaddockDrawRun(draw, type, {press.min.x + 16.0F, std::round(press.min.y + (42.0F - type.line) * 0.5F)},
                   PaddockCol(0xEAF3F5), label, end);
    PaddockEndPress(press, 6.0F);
    return press.pressed;
}

struct PlayNoticeLayout {
    float height = 0.0F;
    bool one_row = true;
    float text_width = 0.0F;
    float title_height = 0.0F;
};

PlayNoticeLayout MeasurePlayNotice(const PlayNotice& notice, float width) {
    const PaddockType strong = PaddockReading(15.0F, true, 1.4F);
    const PaddockType body = PaddockReading(14.0F, false, 1.5F);
    const float inner = width - 20.0F - 12.0F;
    const float button = PlayNoticeButtonWidth(notice.action.c_str());
    PlayNoticeLayout layout;
    layout.one_row = 320.0F + 20.0F + button <= inner;
    layout.text_width = layout.one_row ? inner - 20.0F - button : inner;
    layout.title_height = PaddockTextHeight(strong, notice.title, layout.text_width, true);
    const float text = layout.title_height + 2.0F +
                       PaddockTextHeight(body, notice.body, layout.text_width);
    layout.height = layout.one_row ? 13.0F + std::max(text, 42.0F) + 13.0F
                                   : 13.0F + text + 12.0F + 42.0F + 13.0F;
    return layout;
}

// Draws a notice at the cursor; returns whether its action was pressed.
bool DrawPlayNotice(const PlayNotice& notice, float width, bool locked) {
    const PlayNoticeLayout layout = MeasurePlayNotice(notice, width);
    const ImVec2 at = ImGui::GetCursorScreenPos();
    const ImVec2 end{at.x + width, at.y + layout.height};
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const unsigned fill = notice.warning ? 0x42341DU : 0x092E42U;
    const unsigned stripe = notice.warning ? 0xFFAB14U : 0xFFC453U;
    // box-shadow: inset 3px 0 follows the rounded corners.
    PaddockFill(draw, at, end, PaddockRound(18.0F), PaddockCol(stripe));
    draw->PushClipRect(at, end, true);
    PaddockFill(draw, {at.x + 3.0F, at.y}, {end.x + 3.0F, end.y}, PaddockRound(18.0F),
                PaddockCol(fill));
    draw->PopClipRect();

    const PaddockType strong = PaddockReading(15.0F, true, 1.4F);
    const PaddockType body = PaddockReading(14.0F, false, 1.5F);
    const float text_height = layout.title_height + 2.0F +
                              PaddockTextHeight(body, notice.body, layout.text_width);
    const float text_top = layout.one_row
        ? at.y + 13.0F + std::round((std::max(text_height, 42.0F) - text_height) * 0.5F)
        : at.y + 13.0F;
    PaddockTextStyle title_style;
    title_style.colour = PaddockCol(notice.warning ? 0xFFECCBU : 0xFFE4A5U);
    PaddockTextAt(draw, strong, {at.x + 20.0F, text_top}, layout.text_width, notice.title,
                  title_style, true);
    PaddockTextStyle body_style;
    body_style.colour = PaddockCol(notice.warning ? 0xFFDC9AU : 0xC7DCE6U);
    PaddockTextAt(draw, body, {at.x + 20.0F, text_top + layout.title_height + 2.0F},
                  layout.text_width, notice.body, body_style);

    const float button = PlayNoticeButtonWidth(notice.action.c_str());
    const ImVec2 button_at = layout.one_row
        ? ImVec2{end.x - 12.0F - button, at.y + std::round((layout.height - 42.0F) * 0.5F)}
        : ImVec2{at.x + 20.0F, end.y - 13.0F - 42.0F};
    ImGui::SetCursorScreenPos(button_at);
    ImGui::BeginDisabled(locked);
    const std::string id = notice.action + "##" + notice.key;
    const bool pressed = PlayNoticeButton(id.c_str());
    ImGui::EndDisabled();
    ImGui::SetCursorScreenPos(at);
    ImGui::Dummy({width, layout.height});
    return pressed;
}

void DrawPlayLights(ImDrawList* draw, ImVec2 at, bool ready, double arrived_at) {
    // A ROM just loaded: the lights run red, amber, green once.
    const float t = static_cast<float>(PaddockClock() - arrived_at);
    const bool arriving = ready && t >= 0.0F && t < 0.56F;
    PaddockFill(draw, at, {at.x + 42.0F, at.y + 112.0F}, PaddockRound(21.0F),
                PaddockCol(0x0C1922));
    const ImU32 off = PaddockRgb(0x4E5B5B);
    const auto ease_in = [](float x) { x = std::clamp(x, 0.0F, 1.0F); return x * x * x; };
    ImU32 red = ready ? off : PaddockRgb(0xEC3627);
    ImU32 amber = ready ? off : PaddockRgb(0xFFAE18);
    ImU32 green = ready ? PaddockRgb(0x20DA81) : off;
    float green_scale = 1.0F;
    if (arriving) {
        red = PaddockMix(PaddockRgb(0xEC3627), off, ease_in((t - 0.12F) / 0.12F));
        amber = PaddockMix(PaddockRgb(0xFFAE18), off, ease_in((t - 0.24F) / 0.12F));
        const float grow = PaddockEase((t - 0.36F) / 0.2F);
        green = PaddockMix(off, PaddockRgb(0x20DA81), grow);
        green_scale = 0.6F + 0.4F * grow;
    }
    draw->AddCircleFilled({at.x + 21.0F, at.y + 24.0F}, 9.0F, PaddockApply(red), 24);
    draw->AddCircleFilled({at.x + 21.0F, at.y + 56.0F}, 9.0F, PaddockApply(amber), 24);
    draw->AddCircleFilled({at.x + 21.0F, at.y + 88.0F}, 9.0F * green_scale,
                          PaddockApply(green), 24);
}

void RemoveSelectedRom(const PlayPageContext& play) {
    const std::string key = RomPathKey(play.selected_rom);
    const auto found = std::find_if(play.rom_catalog.begin(), play.rom_catalog.end(),
                                    [&](const RomCatalogEntry& entry) { return entry.key == key; });
    g_play_page.removed_index = found == play.rom_catalog.end()
        ? play.rom_catalog.size() : static_cast<std::size_t>(found - play.rom_catalog.begin());
    g_play_page.removed = play.selected_rom;
    std::erase_if(play.rom_catalog, [&](const RomCatalogEntry& entry) { return entry.key == key; });
    SaveRomCatalog(play.rom_catalog);
    play.selected_rom.clear();
    play.rom_ready = false;
    play.rom_status = kRomChoosePrompt;
    g_mod_browser_revision = 0U;
    while (!play.rom_catalog.empty() && !play.rom_ready) {
        const RomCatalogEntry next = play.rom_catalog.front();
        play.rom_ready = SelectCatalogRom(next, play.selected_rom, play.rom_catalog, play.rom_status);
    }
    if (!play.rom_ready) SaveLastRom({});
}

void UndoRemoveRom(const PlayPageContext& play) {
    const std::filesystem::path path = std::exchange(g_play_page.removed, {});
    std::string error;
    dkr::runtime::rom::Identity identity{};
    if (!dkr::runtime::ValidateRomForLauncher(path, identity, error)) {
        play.rom_status = error;
        return;
    }
    CommitRomSelection(path, identity, play.selected_rom, play.rom_catalog, play.rom_status);
    // Back where it was in the list.
    const std::string key = RomPathKey(play.selected_rom);
    const auto found = std::find_if(play.rom_catalog.begin(), play.rom_catalog.end(),
                                    [&](const RomCatalogEntry& entry) { return entry.key == key; });
    if (found != play.rom_catalog.end()) {
        const RomCatalogEntry entry = *found;
        play.rom_catalog.erase(found);
        const std::size_t index = std::min(g_play_page.removed_index, play.rom_catalog.size());
        play.rom_catalog.insert(play.rom_catalog.begin() + static_cast<std::ptrdiff_t>(index), entry);
        SaveRomCatalog(play.rom_catalog);
    }
    play.rom_ready = true;
}

std::vector<PlayNotice> BuildPlayNotices(bool rom_ready) {
    namespace tracks_ns = dkr::runtime::custom_tracks;
    std::vector<PlayNotice> notices;
    const std::string armed = tracks_ns::armed_track_id();
    if (!armed.empty()) {
        std::string name = armed;
        for (const auto& track : tracks_ns::tracks()) {
            if (track.id == armed) {
                name = track.name;
                break;
            }
        }
        notices.push_back({"test", "Track Lab test: " + name,
                           tracks_ns::auto_boot_enabled()
                               ? "Starts directly in this track as Diddy, in single player. "
                                 "Uses the Modern graphics profile."
                               : "Native DKR track. Uses the Modern graphics profile.",
                           false, "Stop testing"});
    }
    if (rom_ready && g_mod_browser_revision != 0U) {
        std::vector<std::string> names;
        for (const auto& browser : g_mod_browsers) {
            for (const auto& card : browser.all) {
                if (card.item.enabled && !card.item.hidden &&
                    !dkr::mods::browser::compatible(card, g_mod_browser_revision)) {
                    names.push_back(card.item.name);
                }
            }
        }
        if (!names.empty()) {
            const bool one = names.size() == 1U;
            std::string list;
            for (const auto& name : names) list += (list.empty() ? "" : ", ") + name;
            notices.push_back({"mods",
                               one ? "1 enabled mod needs preparing"
                                   : std::to_string(names.size()) + " enabled mods need preparing",
                               list + (one ? " was" : " were") +
                                   " prepared for a different ROM version. Use Prepare mod on " +
                                   (one ? "its card." : "their cards."),
                               true, "Open Mods"});
        }
    }
    return notices;
}

void DrawPlayPage(float available_width, const PlayPageContext& play) {
    namespace tracks_ns = dkr::runtime::custom_tracks;
    PlayPageState& state = g_play_page;
    const int frame = ImGui::GetFrameCount();
    const bool entered = state.context != ImGui::GetCurrentContext() ||
                         state.last_frame != frame - 1;
    if (entered) {
        state.context = ImGui::GetCurrentContext();
        state.removed.clear();
        state.notices_known = false;
        state.shown.clear();
        state.entered.clear();
        state.leaving.reset();
        state.arrived_at = -10.0;
        state.focus = PlayPageState::Focus::None;
        state.was_ready = play.rom_ready;
    }
    state.last_frame = frame;
    if (play.rom_ready && !state.was_ready) {
        state.arrived_at = PaddockClock();
        state.focus = PlayPageState::Focus::Start;
    }
    state.was_ready = play.rom_ready;


    RefreshModBrowserCards(g_legacy_imports.snapshot());
    const bool imports_busy = g_legacy_imports.snapshot().busy;
    const bool launch_modal = g_mod_launch.snapshot().modal;
    const bool lobby = dkr::runtime::netplay::session().active();
    const bool nav = ImGui::GetIO().NavVisible;

    const float width = std::min(available_width, 1100.0F);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {0.0F, 0.0F});

    DrawPageHeading("PLAY");
    PaddockGap(4.0F);
    PaddockText(PaddockReading(14.0F, false, 1.5F), PaddockRgb(0xB0C9CC),
                "Load your ROM, then jump into the race.", width);
    PaddockGap(16.0F);

    // The ROM pass: status, the ROM list, and the actions for it.
    {
        const bool ready = play.rom_ready;
        const std::size_t count = play.rom_catalog.size();
        const ImVec2 at = ImGui::GetCursorScreenPos();
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const float body_x = at.x + 2.0F + 24.0F + 42.0F + 20.0F;
        const float body_width = std::max(width - 2.0F * 2.0F - 24.0F * 2.0F - 42.0F - 20.0F, 1.0F);
        const PaddockType heading = PaddockSign(19.0F, 1.0F, 0.0F);
        const PaddockType file_type = PaddockReading(15.0F, true, 1.5F);
        const PaddockType meta_type = PaddockReading(14.0F, false, 1.5F);
        PaddockType pick_label = PaddockReading(11.0F, true, 1.0F);
        pick_label.tracking = 11.0F * 0.12F;
        const std::string removed_note = state.removed.empty()
            ? std::string{} : "Removed " + PathUtf8(state.removed.filename()) + " from your list.";
        std::string meta;
        unsigned meta_colour = 0x45595F;
        if (!removed_note.empty()) {
            meta = removed_note;
        } else if (!ready && !play.rom_status.empty() && play.rom_status != kRomChoosePrompt) {
            meta = play.rom_status;
            meta_colour = 0x8A2A17;
        } else if (count == 1U) {
            meta = "Add your other Game Pak revision to switch between them here.";
        } else if (count > 1U) {
            meta = std::to_string(count) + " ROMs in your list. Switch any time before you start.";
        } else {
            meta = "Accepted files: .z64, .n64, .v64";
        }
        const char* primary = count > 0U ? "ADD A ROM...##rom" : "BROWSE FOR ROM...##rom";
        const char* secondary = !state.removed.empty() ? "Undo##rom"
                              : count > 0U && ready ? "Remove from list##rom" : nullptr;
        const float primary_width = std::max(240.0F, PaddockRaceButtonWidth(primary, 19.0F, 0.12F));
        const float secondary_width = secondary != nullptr ? PlayTextButtonWidth(secondary) : 0.0F;
        const bool actions_row = secondary == nullptr ||
                                 primary_width + 16.0F + secondary_width <= body_width;
        const float actions_height = actions_row ? 46.0F : 46.0F + 8.0F + 44.0F;
        const float pick_width = std::min(body_width, 560.0F);
        const float pick_height = 4.0F + pick_label.line + 6.0F + 60.0F + 8.0F;
        const std::string_view no_rom = "Load an original Diddy Kong Racing ROM to continue.";
        const float detail_height = count > 0U ? pick_height : PaddockTextHeight(file_type, no_rom, body_width);
        const float meta_height = PaddockTextHeight(meta_type, meta, body_width);
        const float body_height = 6.0F + heading.line + 8.0F + detail_height + 2.0F +
                                  meta_height + 16.0F + actions_height;
        const float height = 2.0F + 20.0F + std::max(112.0F, body_height) + 20.0F + 2.0F;
        const ImVec2 end{at.x + width, at.y + height};

        // The border turns green with the last light.
        const float since = static_cast<float>(PaddockClock() - state.arrived_at);
        ImU32 border = PaddockRgb(ready ? 0x1AC2A3U : 0xE82E21U);
        if (ready && since >= 0.0F && since < 0.56F) {
            border = PaddockMix(PaddockRgb(0xE82E21), PaddockRgb(0x1AC2A3),
                                PaddockEase((since - 0.36F) / 0.2F));
        }
        PaddockPanelStyle panel;
        panel.radii = PaddockRound(18.0F);
        panel.fill = PaddockRgb(0xFFF0C2);
        panel.border = border;
        panel.border_width = 2.0F;
        PaddockPanel(draw, at, end, panel);
        DrawPlayLights(draw, {at.x + 2.0F + 24.0F, at.y + 2.0F + 20.0F}, ready, state.arrived_at);

        float y = at.y + 2.0F + 20.0F + 6.0F;
        const std::string_view title = ready ? "Ready to race" : "ROM not loaded";
        PaddockDrawRun(draw, heading, {body_x, y}, PaddockCol(ready ? 0x037A47U : 0xC0261BU),
                       title.data(), title.data() + title.size());
        y += heading.line + 8.0F;
        // Focus rings on the cream pass are dark, like the study's outline.
        ImGui::PushStyleColor(ImGuiCol_NavHighlight, PaddockRgb(0x091B24));
        if (count > 0U) {
            // The ROM list: every Game Pak DKR-R has accepted, one pick away.
            y += 4.0F;
            PaddockDrawRun(draw, pick_label, {body_x, y}, PaddockCol(0x3F5359), "GAME ROM", "GAME ROM" + 8);
            y += pick_label.line + 6.0F;
            ImGui::SetCursorScreenPos({body_x, y});
            if (state.focus == PlayPageState::Focus::Rom) {
                if (nav) ImGui::SetKeyboardFocusHere();
                state.focus = PlayPageState::Focus::None;
            }
            const RomSelectResult picked = DrawRomSelect("play-rom", pick_width, false, lobby || launch_modal,
                                                         "Locked while you are in a lobby.");
            if (picked == RomSelectResult::Switched) {
                state.removed.clear();
                if (play.rom_ready) state.arrived_at = PaddockClock();
                state.focus = PlayPageState::Focus::Rom;
            }
            y += 60.0F + 8.0F;
        } else {
            PaddockTextStyle file_style;
            file_style.colour = PaddockCol(0x091B24);
            PaddockTextAt(draw, file_type, {body_x, y}, body_width, no_rom, file_style);
            y += detail_height;
        }
        y += 2.0F;
        PaddockTextStyle meta_style;
        meta_style.colour = PaddockCol(meta_colour);
        PaddockTextAt(draw, meta_type, {body_x, y}, body_width, meta, meta_style);
        y += meta_height + 16.0F;

        ImGui::SetCursorScreenPos({body_x, y});
        if (state.focus == PlayPageState::Focus::Browse && !ready) {
            if (nav) ImGui::SetKeyboardFocusHere();
            state.focus = PlayPageState::Focus::None;
        }
        RaceButtonLook look;
        look.fill = count > 0U ? 0x0A6EA1U : 0x1AC2A3U;
        look.hover_in = 0.12F;
        look.ellipsis_spread = 0.12F;
        if (PaddockRaceButton(primary, {primary_width, 46.0F}, look)) {
            OpenRomBrowser(play.selected_rom);
        }
        if (secondary != nullptr) {
            ImGui::SetCursorScreenPos(actions_row
                ? ImVec2{body_x + primary_width + 16.0F, y + 1.0F}
                : ImVec2{body_x - 8.0F, y + 46.0F + 8.0F});
            if (state.removed.empty()) {
                ImGui::BeginDisabled(lobby || launch_modal);
                if (PlayTextButton(secondary, true)) {
                    RemoveSelectedRom(play);
                    state.was_ready = play.rom_ready;
                    state.focus = play.rom_ready ? PlayPageState::Focus::Rom : PlayPageState::Focus::Browse;
                }
                ImGui::EndDisabled();
            } else if (PlayTextButton(secondary, false)) {
                UndoRemoveRom(play);
                state.arrived_at = PaddockClock();
                state.focus = PlayPageState::Focus::Rom;
            }
        }
        ImGui::PopStyleColor();
        ImGui::SetCursorScreenPos(at);
        ImGui::Dummy({width, height});
    }

    // Anything that changes what START does sits directly above it.
    std::vector<PlayNotice> notices = BuildPlayNotices(play.rom_ready);
    {
        const double now = PaddockClock();
        int entering = 0;
        for (const PlayNotice& notice : notices) {
            if (state.notices_known && !state.shown.contains(notice.key)) {
                state.entered[notice.key] = now + 0.1 * entering++;
            }
        }
        state.shown.clear();
        for (const PlayNotice& notice : notices) state.shown.insert(notice.key);
        state.notices_known = true;
    }
    // A dismissed notice fades up first, then its row closes so START glides up.
    // Only the Track Lab test (always the first notice) can be dismissed here.
    if (state.leaving) {
        const float t = static_cast<float>(PaddockClock() - state.leaving_at);
        if (t >= 0.3F || std::any_of(notices.begin(), notices.end(),
                [&](const PlayNotice& notice) { return notice.key == state.leaving->key; })) {
            state.leaving.reset();
        } else {
            const float row = (state.leaving_height + 16.0F) *
                              (1.0F - PaddockEase((t - 0.1F) / 0.2F));
            const ImVec2 at = ImGui::GetCursorScreenPos();
            ImDrawList* draw = ImGui::GetWindowDrawList();
            const float fade = std::clamp(t / 0.15F, 0.0F, 1.0F);
            if (fade < 1.0F && row > 0.5F) {
                draw->PushClipRect(at, {at.x + width, at.y + row}, true);
                const int first = draw->VtxBuffer.Size;
                ImGui::SetCursorScreenPos({at.x, at.y + 16.0F});
                ImGui::BeginDisabled();
                ImGui::PushID("leaving");
                DrawPlayNotice(*state.leaving, width, true);
                ImGui::PopID();
                ImGui::EndDisabled();
                for (int vertex = first; vertex < draw->VtxBuffer.Size; ++vertex) {
                    ImDrawVert& v = draw->VtxBuffer[vertex];
                    v.pos.y -= 8.0F * fade * fade;
                    const ImU32 alpha = (v.col >> IM_COL32_A_SHIFT) & 0xFFU;
                    v.col = (v.col & ~IM_COL32_A_MASK) |
                        (static_cast<ImU32>(std::lround(alpha * (1.0F - fade * fade)))
                         << IM_COL32_A_SHIFT);
                }
                draw->PopClipRect();
            }
            ImGui::SetCursorScreenPos(at);
            ImGui::Dummy({width, row});
        }
    }

    const bool mods_locked = lobby || launch_modal || imports_busy;
    for (const PlayNotice& notice : notices) {
        PaddockGap(16.0F);
        const auto entered_at = state.entered.find(notice.key);
        std::optional<PaddockFadeUp> fade;
        if (entered_at != state.entered.end()) fade.emplace(entered_at->second, 0.0F, 0.3F, 12.0F);
        const float before = ImGui::GetCursorScreenPos().y;
        if (DrawPlayNotice(notice, width, notice.key == "test" && mods_locked)) {
            if (notice.key == "test") {
                state.leaving = notice;
                state.leaving_height = ImGui::GetCursorScreenPos().y - before;
                state.leaving_at = PaddockClock();
                tracks_ns::arm_track_override(std::string{});
                tracks_ns::set_auto_boot(false);
                state.focus = PlayPageState::Focus::Start;
            } else {
                g_mods_page.entry_section = kModsSectionLibrary;
                g_page_navigation_request = kPageModsHacks;
            }
        }
    }
    // START.
    PaddockGap(16.0F);
    {
        const bool can_start = play.rom_ready && !imports_busy && !launch_modal;
        RaceButtonLook look;
        look.hover_in = 0.12F;
        if (can_start) {
            look.fill = 0x1AC2A3;
            look.drop = 4.0F;
            look.drop_colour = 0x0B6A58;
            look.highlight_alpha = 51U;
            look.sink = true;
        } else {
            // Disabled START reads as unavailable rather than as a faded primary.
            look.fill = 0x1C3440;
            look.border_alpha = 0U;
            look.text = 0x8FA6AD;
            look.ring_alpha = 20U;
        }
        if (state.focus == PlayPageState::Focus::Start && can_start) {
            if (nav) ImGui::SetKeyboardFocusHere();
            state.focus = PlayPageState::Focus::None;
        }
        ImGui::PushStyleVar(ImGuiStyleVar_DisabledAlpha, 1.0F);
        ImGui::BeginDisabled(!can_start);
        if (PaddockRaceButton("START Diddy Kong Racing - Recompiled", {width, 68.0F}, look)) {
            play.launch_requested = true;
        }
        ImGui::EndDisabled();
        ImGui::PopStyleVar();
    }
    if (!play.rom_ready) {
        PaddockGap(8.0F);
        const PaddockType hint = PaddockReading(14.0F, false, 1.5F);
        constexpr std::string_view text = "Load a ROM to start.";
        const ImVec2 at = ImGui::GetCursorScreenPos();
        PaddockTextStyle style;
        style.colour = PaddockCol(0xB0C9CC);
        style.centre = true;
        PaddockTextAt(ImGui::GetWindowDrawList(), hint, at, width, text, style);
        ImGui::Dummy({width, hint.line});
    }
    ImGui::PopStyleVar();
}
