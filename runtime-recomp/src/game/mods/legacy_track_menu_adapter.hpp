#pragma once
#include "legacy_track_menu.hpp"

namespace dkr::mods {
enum class MenuField : unsigned {
    CursorX,
    CursorY,
    X,
    Y,
    TargetX,
    TargetY,
    SelectedX,
    SelectedY,
    Height,
    HalfHeight,
    PreviewCarrier,
    LoadedCarrier,
    Opacity,
    FutureFunLand,
    Details,
    Delay,
    Buttons,
    StickX,
    StickY,
    World,
    StockIDs,
    VoiceDelay,
    Type,
    InTracks,
    LaunchCarrier,
    BackgroundBusy,
    TitleLoaded,
    DisplayList,
    VertexFlip,
    Count
};
using TrackMenuFields = std::array<std::uint32_t, static_cast<unsigned>(MenuField::Count)>;
struct TrackSceneChoice {
    std::string id;
    unsigned carrier = 0;
};
// Menu identity is distinct from the first course's asset identity. Several
// cups may preview the same course without aliasing their selection.
struct TrackMenuCup {
    std::string id, name;
    TrackSceneChoice preview;
    unsigned vehicle = 0;
};
struct TrackMenuEffect {
    bool override_return = false;
    bool navigation_sound = false;
    std::uint32_t return_value = 0;
    std::optional<TrackSceneChoice> scene;
    struct Background {
        std::uint32_t texture, vertices, triangles;
    };
    std::vector<Background> backgrounds;
    std::uint32_t background_display_list = 0;
};
// Append through the background callback's actual Gfx**, never the later
// menu_loop global. Shared by the production bridge and headless regressions.
void append_track_background_quad(std::span<std::uint8_t> guest, std::uint32_t display_list_slot,
                                  const TrackMenuEffect::Background& background);
// Pure guest-data adapter, testable without the renderer or guest scheduler.
// No guest calls/yields, host I/O, asset mutations or retained RDRAM pointers.
// The bridge serializes each event and handles scene requests after unlocking.
class TrackMenuAdapter {
  public:
    explicit TrackMenuAdapter(std::vector<Root> tracks, bool allow_races = false, std::vector<TrackMenuCup> cups = {});
    const std::string& selected_cup() const {
        return selected_cup_;
    }
    const Bytes& name_bytes() const {
        return names_;
    }
    bool needs_names() const {
        return name_address_ == 0;
    }
    void install_names(std::span<std::uint8_t> guest, std::uint32_t address);
    TrackMenuEffect apply(unsigned event, std::span<std::uint8_t> guest, const TrackMenuFields& fields,
                          std::uint32_t argument = 0, std::uint32_t returned = 0);
    Bytes checkpoint() const;
    TrackMenuAdapter stage_checkpoint(View) const;

  private:
    std::vector<Root> tracks_;
    std::vector<TrackMenuCup> cups_;
    std::string selected_cup_;
    struct OptionScope {
        unsigned event;
        TrackCursor cursor;
        std::uint32_t selected_x;
    };
    std::vector<OptionScope> options_;
    Bytes names_;
    std::vector<std::size_t> name_offsets_;
    std::vector<std::size_t> header_offsets_;
    std::size_t background_offset_ = 0;
    std::uint32_t name_address_ = 0;
    bool allow_races_ = false, in_tracks_ = false, masked_ = false;
    std::uint16_t saved_x_ = 0, saved_y_ = 0;
    std::uint32_t saved_buttons_ = 0;
    std::optional<TrackCursor> move_, restore_;
    std::optional<TrackSceneChoice> candidate_, pending_, race_;
    std::string preview_id_;
    std::optional<float> background_y_;
    std::optional<std::uint32_t> background_display_list_;
};
} // namespace dkr::mods
