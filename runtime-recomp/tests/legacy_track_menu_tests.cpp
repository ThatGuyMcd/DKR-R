#include "legacy_track_menu.hpp"
#include <iostream>
#include <set>
#include <vector>

int main() {
    using namespace dkr::mods;
    unsigned checks = 0;
    const auto check = [&](bool value) {
        ++checks;
        if (!value)
            throw Error("Additive menu test " + std::to_string(checks));
    };
    try {
        for (unsigned stock_rows : {4U, 5U})
            for (std::size_t count : {0U, 1U, 3U, 4U, 5U, 6U, 7U, 8U, 9U, 511U, 512U})
                for (std::size_t cups : {0U, 1U, 6U, 7U, 32U}) {
                    const TrackMenuLayout layout(stock_rows, count, cups);
                    std::set<std::pair<int, int>> reached;
                    std::vector<TrackCursor> pending{{0, 0}};
                    while (!pending.empty()) {
                        const auto at = pending.back();
                        pending.pop_back();
                        if (!reached.emplace(at.row, at.column).second)
                            continue;
                        const auto cell = layout.cell(at);
                        check(bool(cell));
                        if (cell->custom_index) {
                            check(*cell->custom_index < count + cups);
                            check(at.column < 6);
                            check(layout.custom_cursor(*cell->custom_index) == at);
                        } else
                            check(static_cast<unsigned>(at.row) < stock_rows);
                        for (const auto direction : {std::pair{1, 0}, {-1, 0}, {0, 1}, {0, -1}}) {
                            const auto next = layout.move(at, direction.first, direction.second);
                            check(bool(layout.cell(next)));
                            pending.push_back(next);
                        }
                    }
                    const auto stock_cells = stock_rows == 4 ? 24 : 29;
                    check(reached.size() == stock_cells + count + cups);
                    check(!layout.cell({static_cast<int>(layout.rows()), 0}));
                    check(!layout.cell({-1, 0}));
                    check(!layout.cell({0, -1}));
                    if (count) {
                        const auto first = layout.custom_cursor(0);
                        check(layout.move(first, 0, 1).row == static_cast<int>(stock_rows) - 1);
                        check((layout.arrows({static_cast<int>(stock_rows) - 1, 0}) & 4) != 0);
                        check(bool(layout.arrows(layout.custom_cursor(count - 1)) & 4) == bool(cups));
                    }
                }
        std::cout << checks
                  << " additive grid navigation, stock boundaries and full-catalogue reachability checks passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
