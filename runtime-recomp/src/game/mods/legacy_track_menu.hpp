#pragma once
#include "legacy_mod_format.hpp"
#include <optional>

namespace dkr::mods {
struct TrackCursor {
    int row = 0, column = 0;
    bool operator==(const TrackCursor&) const = default;
};
struct TrackMenuCell {
    TrackCursor position;
    // Stock cells have no custom index. They must continue through the
    // original stock unlock/trophy/minigame logic, never a fabricated root.
    std::optional<std::size_t> custom_index;
};
// An additive logical grid. Retail's five-world arrays are NOT extended.
// Only cell().custom_index selects a custom catalogue entry.
class TrackMenuLayout {
  public:
    static constexpr unsigned CustomColumns = 6;
    TrackMenuLayout(unsigned stock_rows, std::size_t custom_count, std::size_t cups = 0);
    unsigned cup_row() const {
        return stock_rows_ + static_cast<unsigned>((custom_count_ + 5) / 6);
    }
    unsigned rows() const {
        return cup_row() + static_cast<unsigned>((cups_ + 5) / 6);
    }
    unsigned stock_rows() const {
        return stock_rows_;
    }
    unsigned columns(int row) const;
    std::optional<TrackMenuCell> cell(TrackCursor cursor) const;
    TrackCursor move(TrackCursor cursor, int horizontal, int vertical) const;
    unsigned arrows(TrackCursor cursor) const;
    TrackCursor custom_cursor(std::size_t index) const;

  private:
    unsigned stock_rows_;
    std::size_t custom_count_;
    std::size_t cups_;
};
} // namespace dkr::mods
