#include "legacy_track_menu.hpp"
#include <algorithm>

namespace dkr::mods {
TrackMenuLayout::TrackMenuLayout(unsigned stock_rows, std::size_t custom_count, std::size_t cups)
    : stock_rows_(stock_rows), custom_count_(custom_count), cups_(cups) {
    if ((stock_rows != 4 && stock_rows != 5) || custom_count > 512 || cups > 32)
        throw Error("Invalid additive track menu dimensions.");
}
unsigned TrackMenuLayout::columns(int row) const {
    if (row < 0 || static_cast<unsigned>(row) >= rows())
        return 0;
    if (static_cast<unsigned>(row) < stock_rows_)
        return row == 4 ? 5 : 6;
    if (static_cast<unsigned>(row) < cup_row())
        return static_cast<unsigned>(std::min<std::size_t>(6, custom_count_ - (row - stock_rows_) * 6));
    return static_cast<unsigned>(std::min<std::size_t>(6, cups_ - (row - cup_row()) * 6));
}
std::optional<TrackMenuCell> TrackMenuLayout::cell(TrackCursor cursor) const {
    if (cursor.column < 0 || static_cast<unsigned>(cursor.column) >= columns(cursor.row))
        return {};
    TrackMenuCell result{cursor, {}};
    if (static_cast<unsigned>(cursor.row) >= stock_rows_)
        result.custom_index = static_cast<unsigned>(cursor.row) < cup_row()
                                  ? (cursor.row - stock_rows_) * 6 + cursor.column
                                  : custom_count_ + (cursor.row - cup_row()) * 6 + cursor.column;
    return result;
}
TrackCursor TrackMenuLayout::move(TrackCursor cursor, int horizontal, int vertical) const {
    if (!cell(cursor))
        throw Error("Track navigation began outside its logical grid.");
    // Match the original preference: horizontal navigation wins over vertical.
    if (horizontal)
        cursor.column =
            std::clamp(cursor.column + (horizontal > 0 ? 1 : -1), 0, static_cast<int>(columns(cursor.row)) - 1);
    else if (vertical) {
        const int next = std::clamp(cursor.row + (vertical > 0 ? -1 : 1), 0, static_cast<int>(rows()) - 1);
        // Retail challenges have no Future Fun Land counterpart. Preserve
        // that stock rule rather than moving sideways to its trophy cell.
        if (cursor.row == 3 && cursor.column == 5 && next == 4 && stock_rows_ == 5)
            return cursor;
        cursor.row = next;
        cursor.column = std::min(cursor.column, static_cast<int>(columns(next)) - 1);
    }
    return cursor;
}
unsigned TrackMenuLayout::arrows(TrackCursor cursor) const {
    if (!cell(cursor))
        return 0;
    unsigned flags = 0;
    if (move(cursor, 0, 1) != cursor)
        flags |= 1;
    if (move(cursor, 1, 0) != cursor)
        flags |= 2;
    if (move(cursor, 0, -1) != cursor)
        flags |= 4;
    if (move(cursor, -1, 0) != cursor)
        flags |= 8;
    return flags;
}
TrackCursor TrackMenuLayout::custom_cursor(std::size_t index) const {
    if (index >= custom_count_ + cups_)
        throw Error("Custom menu index is outside the catalogue.");
    if (index < custom_count_)
        return {static_cast<int>(stock_rows_ + index / 6), static_cast<int>(index % 6)};
    index -= custom_count_;
    return {static_cast<int>(cup_row() + index / 6), static_cast<int>(index % 6)};
}
} // namespace dkr::mods
