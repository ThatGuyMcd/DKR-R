#include "legacy_track_menu_adapter.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <set>

namespace dkr::mods {
namespace {
class Guest {
    std::span<std::uint8_t> memory_;
    const TrackMenuFields& fields_;

  public:
    Guest(std::span<std::uint8_t> memory, const TrackMenuFields& fields) : memory_(memory), fields_(fields) {
        if (memory.size() % 4)
            throw Error("Unaligned custom menu guest memory.");
        for (auto address : fields)
            check(address, 4);
    }
    std::size_t check(std::uint32_t address, std::size_t size) const {
        auto p = std::size_t(address & 0x1fffffffU);
        if ((address & 0xe0000000U) != 0x80000000U || p > memory_.size() || size > memory_.size() - p)
            throw Error("Custom menu accessed an invalid guest range.");
        return p;
    }
    std::uint32_t address(MenuField field, unsigned offset = 0) const {
        const auto base = fields_.at(static_cast<unsigned>(field));
        if (offset > 0xffffffffU - base)
            throw Error("Custom menu guest address overflow.");
        return base + offset;
    }
    std::uint32_t read(MenuField field, unsigned offset = 0, unsigned size = 4) const {
        auto p = check(address(field, offset), size);
        std::uint32_t result = 0;
        for (unsigned i = 0; i < size; ++i)
            result = (result << 8) | memory_[(p + i) ^ 3];
        return result;
    }
    int integer(MenuField field) const {
        return static_cast<std::int32_t>(read(field));
    }
    float real(MenuField field) const {
        const auto value = std::bit_cast<float>(read(field));
        if (!std::isfinite(value) || std::abs(value) > 1000000)
            throw Error("Invalid native track-menu scroll coordinate.");
        return value;
    }
    void write(MenuField field, std::uint32_t value, unsigned offset = 0, unsigned size = 4) {
        auto p = check(address(field, offset), size);
        for (unsigned i = 0; i < size; ++i)
            memory_[(p + i) ^ 3] = static_cast<std::uint8_t>(value >> ((size - i - 1) * 8));
    }
    void real(MenuField field, float value) {
        write(field, std::bit_cast<std::uint32_t>(value));
    }
};
constexpr std::uint32_t Confirm = 0x9000, Back = 0x4000;
} // namespace
TrackMenuAdapter::TrackMenuAdapter(std::vector<Root> tracks, bool allow_races, std::vector<TrackMenuCup> cups)
    : tracks_(std::move(tracks)), cups_(std::move(cups)), allow_races_(allow_races) {
    if ((tracks_.empty() && cups_.empty()) || tracks_.size() > 512 || cups_.size() > 32)
        throw Error("Invalid native custom-track catalogue size.");
    std::set<std::string> identities;
    auto add_name = [&](std::string_view name) {
        if (name.empty() || name.size() > 255)
            throw Error("Custom track name exceeds the native text budget.");
        // Fixed-size retail display-list heaps must also hold the live scene.
        // More visible cells cannot turn a 255-byte mod title into thousands
        // of glyph commands per frame. This limits ONLY the in-game caption;
        // the installed mod/catalogue retains its complete original name.
        const bool abbreviated = name.size() > 32;
        if (abbreviated)
            name = name.substr(0, 29);
        for (unsigned char c : name)
            names_.push_back(c >= 32 && c < 127 ? c : '?');
        if (abbreviated)
            names_.insert(names_.end(), {'.', '.', '.'});
        names_.push_back(0);
    };
    // Retail deduplicates headers by POINTER, not by text. One pointer per
    // logical row keeps every row labelled without changing stock behaviour.
    for (std::size_t i = 0; i < (tracks_.size() + 5) / 6; ++i) {
        header_offsets_.push_back(names_.size());
        add_name("CUSTOM TRACKS");
    }
    for (std::size_t i = 0; i < (cups_.size() + 5) / 6; ++i) {
        header_offsets_.push_back(names_.size());
        add_name("CUSTOM TROPHIES");
    }
    for (const auto& root : tracks_) {
        if (root.content_id.size() != 64 || !identities.insert(root.content_id).second || root.carrier >= 128 ||
            !root.vehicles || (root.vehicles & ~7U))
            throw Error("Invalid custom-track menu identity/capabilities.");
        name_offsets_.push_back(names_.size());
        add_name(root.name);
    }
    for (const auto& cup : cups_) {
        if (cup.id.size() != 64 || !identities.insert(cup.id).second || cup.preview.carrier > 255 || cup.vehicle > 2 ||
            (!cup.preview.id.empty() && cup.preview.id.size() != 64))
            throw Error("Invalid custom trophy menu identity.");
        name_offsets_.push_back(names_.size());
        add_name(cup.name);
    }
    while (names_.size() % 8)
        names_.push_back(0);
    background_offset_ = names_.size();
    // Six tiny, distinct RGBA16 proxy textures. RT64 supplies the original
    // full-quality PNGs; only 4608 bytes of extra guest storage are required.
    constexpr std::size_t stride = 768;
    names_.resize(names_.size() + 6 * stride, 0);
    for (unsigned i = 0; i < 6; ++i) {
        const auto base = background_offset_ + i * stride;
        names_[base] = 16;
        names_[base + 1] = 16;
        names_[base + 2] = 0x11;
        names_[base + 5] = 1;
        names_[base + 7] = 0xC0;
        names_[base + 11] = 7;
        names_[base + 19] = 1;
        std::fill_n(names_.begin() + base + 32, 512, static_cast<std::uint8_t>(49 + i * 16));
    }
}
void TrackMenuAdapter::install_names(std::span<std::uint8_t> guest, std::uint32_t address) {
    if (name_address_)
        throw Error("Custom menu names are already installed for this guest.");
    const auto p = std::size_t(address & 0x1fffffffU);
    if ((address & 0xe0000000U) != 0x80000000U || address % 8 || guest.size() % 4 || p > guest.size() ||
        names_.size() > guest.size() - p)
        throw Error("Native custom-track names have no valid guest allocation.");
    for (std::size_t i = 0; i < names_.size(); ++i)
        guest[(p + i) ^ 3] = names_[i];
    name_address_ = address;
    auto word = [&](std::uint32_t at, std::uint32_t value) {
        const auto offset = std::size_t(at & 0x1fffffffU);
        if (offset > guest.size() - 4)
            throw Error("Custom background allocation overflow.");
        for (unsigned k = 0; k < 4; ++k)
            guest[(offset + k) ^ 3] = static_cast<std::uint8_t>(value >> ((3 - k) * 8));
    };
    for (unsigned i = 0; i < 6; ++i) {
        const auto tex = address + static_cast<std::uint32_t>(background_offset_ + i * 768);
        const auto commands = tex + 544;
        word(tex + 12, commands);
        constexpr std::uint32_t words[]{0xFD10000F, 0,          0xF5100000, 0x07000000, 0xE6000000,
                                        0,          0xF3000000, 0x070FF200, 0xE7000000, 0,
                                        0xF5100800, 0x00090240, 0xF2000000, 0x0003C03C};
        for (unsigned k = 0; k < 14; ++k)
            word(commands + 4 * k, k == 1 ? (tex + 32) & 0x1fffffffU : words[k]);
    }
}
TrackMenuEffect TrackMenuAdapter::apply(unsigned event, std::span<std::uint8_t> memory, const TrackMenuFields& fields,
                                        std::uint32_t arg, std::uint32_t returned) {
    if (event > 25)
        throw Error("Unknown native custom-track event.");
    Guest guest(memory, fields);
    TrackMenuEffect effect;
    const unsigned stock_rows = static_cast<std::int16_t>(guest.read(MenuField::FutureFunLand, 0, 2)) == -1 ? 4 : 5;
    TrackMenuLayout layout(stock_rows, tracks_.size(), cups_.size());
    auto cursor = [&]() {
        return options_.empty() ? TrackCursor{guest.integer(MenuField::CursorY), guest.integer(MenuField::CursorX)}
                                : options_.front().cursor;
    };
    auto index_at = [&](TrackCursor p) -> std::optional<std::size_t> {
        const auto c = layout.cell(p);
        return c ? c->custom_index : std::nullopt;
    };
    auto root_at = [&](TrackCursor p) -> const Root* {
        const auto index = index_at(p);
        return index && *index < tracks_.size() ? &tracks_[*index] : nullptr;
    };
    auto cup_at = [&](TrackCursor p) -> const TrackMenuCup* {
        const auto index = index_at(p);
        return index && *index >= tracks_.size() ? &cups_.at(*index - tracks_.size()) : nullptr;
    };
    auto scene = [&](TrackSceneChoice value) {
        const auto cup = std::find_if(cups_.begin(), cups_.end(), [&](const auto& c) { return c.id == value.id; });
        return cup == cups_.end() ? value : cup->preview;
    };
    auto choice = [&]() -> TrackSceneChoice {
        if (const auto root = root_at(cursor()))
            return {root->content_id, root->carrier};
        if (const auto cup = cup_at(cursor()))
            return {cup->id, cup->preview.carrier};
        return {"", static_cast<unsigned>(guest.integer(MenuField::PreviewCarrier))};
    };
    // Retail assigns special meaning to columns 4 and 5. Borrow a semantic
    // cursor ONLY inside its options/load-decision functions; scrolling,
    // drawing and the immutable stock grid keep the real six-column position.
    if (event >= 18) {
        if ((event & 1) == 0) {
            const auto p = cursor();
            const auto index = index_at(p);
            if (in_tracks_ && index && (event != 22 || guest.integer(MenuField::Type) >= 2) &&
                (event != 24 || guest.integer(MenuField::Delay) > 0)) {
                options_.push_back({event, p, guest.read(MenuField::SelectedX)});
                const auto column = cup_at(p) ? 4 : 0;
                guest.write(MenuField::CursorX, column);
                guest.write(MenuField::SelectedX, column);
                if (event == 22 && cup_at(p))
                    guest.write(MenuField::CursorY, 0);
            }
        } else if (!options_.empty() && options_.back().event + 1 == event) {
            const auto saved = options_.back();
            options_.pop_back();
            guest.write(MenuField::CursorX, saved.cursor.column);
            guest.write(MenuField::CursorY, saved.cursor.row);
            guest.write(MenuField::SelectedX, saved.selected_x);
            // Confirmation changes type DURING the options input callback.
            // Keep the semantic cursor for its parent's final load decision,
            // then restore at that parent's matching exit notification.
            if (event == 21 && in_tracks_ && guest.integer(MenuField::Type) >= 2 && index_at(saved.cursor)) {
                options_.push_back({22, saved.cursor, saved.selected_x});
                guest.write(MenuField::CursorX, cup_at(saved.cursor) ? 4 : 0);
                if (cup_at(saved.cursor))
                    guest.write(MenuField::CursorY, 0);
            }
        }
        return effect;
    }
    auto position = [&](TrackCursor p) {
        if (!layout.cell(p))
            throw Error("Custom selection escaped its logical menu.");
        const int height = guest.integer(MenuField::Height);
        if (height < 120 || height > 1024)
            throw Error("Invalid native menu viewport height.");
        guest.write(MenuField::CursorX, p.column);
        guest.write(MenuField::CursorY, p.row);
        guest.real(MenuField::TargetX, static_cast<float>(p.column * 320));
        guest.real(MenuField::TargetY, static_cast<float>(-p.row * height));
        if (const auto root = root_at(p))
            guest.write(MenuField::PreviewCarrier, root->carrier);
        else if (const auto cup = cup_at(p))
            guest.write(MenuField::PreviewCarrier, cup->preview.carrier);
        else {
            // The fifth stock row is an original retail allocation quirk;
            // never access it unless the original unlock/layout permits it.
            guest.write(MenuField::PreviewCarrier,
                        static_cast<std::int16_t>(guest.read(MenuField::StockIDs, (p.row * 6 + p.column) * 2, 2)));
        }
        guest.write(MenuField::World, std::min(p.row + 1, static_cast<int>(stock_rows)));
    };
    if (event == 0) {
        in_tracks_ = true;
        race_.reset();
        pending_.reset();
        candidate_.reset();
        preview_id_.clear();
        selected_cup_.clear();
        const auto previous = cursor();
        const bool custom = index_at(previous).has_value();
        // Native init resets to Dino Domain after visiting the title screen.
        // Preserve custom selection only on a normal race/menu return, never
        // restore it over the original title-entry reset. Keep retail array
        // indexing safe in either case; the native function owns its flag.
        restore_ = custom && !guest.integer(MenuField::TitleLoaded) ? std::optional(previous) : std::nullopt;
        if (custom) {
            guest.write(MenuField::CursorX, 0);
            guest.write(MenuField::CursorY, 0);
        }
        return effect;
    }
    if (event == 1) {
        if (!name_address_)
            throw Error("Native custom-track names were not installed before menu drawing.");
        if (restore_) {
            position(*restore_);
            guest.real(MenuField::X, guest.real(MenuField::TargetX));
            guest.real(MenuField::Y, guest.real(MenuField::TargetY));
            guest.write(MenuField::Opacity, 32);
            guest.write(MenuField::LoadedCarrier, -1);
            restore_.reset();
        }
        return effect;
    }
    if (event == 15) {
        in_tracks_ = arg == 15;
        pending_.reset();
        candidate_.reset();
        preview_id_.clear();
        if (arg == 15 || arg == 3 || arg == 0 || arg == 1 || arg == 23)
            selected_cup_.clear();
        // Results may offer a retry of the same course. Retain its logical
        // identity there, but never let it follow a return to stock menus.
        if (arg != 17)
            race_.reset();
        return effect;
    }
    if (event == 14) {
        if (race_ && race_->carrier == arg)
            effect.scene = scene(*race_);
        else
            race_.reset();
        in_tracks_ = false;
        return effect;
    }
    if (event == 8) {
        if (pending_) {
            if (pending_->carrier != arg)
                throw Error("Background preview identity no longer matches its accepted load.");
            effect.scene = scene(*pending_);
            pending_.reset();
        }
        return effect;
    }
    if (!in_tracks_)
        return effect;
    if (event == 2) {
        if (masked_)
            throw Error("Nested custom menu input.");
        const auto current = cursor();
        if (!layout.cell(current))
            throw Error("Native cursor is outside the custom menu.");
        const auto buttons = guest.read(MenuField::Buttons, 16);
        const auto sx = static_cast<std::int16_t>(guest.read(MenuField::StickX, 8, 2));
        const auto sy = static_cast<std::int16_t>(guest.read(MenuField::StickY, 8, 2));
        const auto next = layout.move(current, sx, sy);
        const auto index = index_at(current);
        const auto selected = choice();
        const bool custom_nav = index.has_value() || index_at(next).has_value();
        const bool not_ready =
            index && (!allow_races_ || preview_id_ != selected.id || guest.integer(MenuField::BackgroundBusy) ||
                      guest.integer(MenuField::LoadedCarrier) != static_cast<int>(selected.carrier));
        if (custom_nav || not_ready) {
            masked_ = true;
            saved_x_ = sx;
            saved_y_ = sy;
            saved_buttons_ = buttons;
            guest.write(MenuField::StickX, 0, 8, 2);
            guest.write(MenuField::StickY, 0, 8, 2);
            if (not_ready)
                guest.write(MenuField::Buttons, buttons & ~Confirm, 16);
            if (guest.integer(MenuField::Delay) == 0 && !(buttons & (Confirm | Back)) && custom_nav && next != current)
                move_ = next;
        }
    } else if (event == 3) {
        if (masked_) {
            guest.write(MenuField::StickX, saved_x_, 8, 2);
            guest.write(MenuField::StickY, saved_y_, 8, 2);
            guest.write(MenuField::Buttons, saved_buttons_, 16);
            masked_ = false;
            if (move_) {
                position(*move_);
                move_.reset();
                effect.navigation_sound = true;
            }
        }
    } else if (event == 4) {
        if (!name_address_)
            throw Error("Custom track names have not been initialized.");
        const auto height = guest.integer(MenuField::Height);
        if (height < 120 || height > 1024)
            throw Error("Invalid native menu viewport height.");
        const auto x = guest.real(MenuField::X), y = guest.real(MenuField::Y);
        const int cx = static_cast<int>(x / 320), cy = static_cast<int>(y / -height);
        const int radius = returned == 5 ? 2 : 1;
        for (int row = -1, k = 0; row < 2; ++row)
            for (int col = -radius; col <= radius; ++col, ++k) {
                const TrackCursor p{cy + row, cx + col};
                const auto cell = layout.cell(p);
                if (!cell)
                    continue;
                const auto offset = k * 16U;
                if (cell->custom_index) {
                    const auto index = *cell->custom_index;
                    guest.write(MenuField::Details,
                                name_address_ + static_cast<std::uint32_t>(header_offsets_.at(p.row - stock_rows)),
                                offset);
                    guest.write(MenuField::Details, name_address_ + static_cast<std::uint32_t>(name_offsets_[index]),
                                offset + 4);
                    guest.write(MenuField::Details, static_cast<std::uint16_t>(static_cast<int>(p.column * 320 - x)),
                                offset + 8, 2);
                    guest.write(MenuField::Details, static_cast<std::uint16_t>(static_cast<int>(-p.row * height - y)),
                                offset + 10, 2);
                    guest.write(MenuField::Details, 1, offset + 12, 1);
                    const bool selected =
                        p.column == guest.integer(MenuField::SelectedX) && p.row == guest.integer(MenuField::SelectedY);
                    const auto opacity =
                        selected ? std::min(255, std::max(0, guest.integer(MenuField::Opacity)) * 8) : 255;
                    guest.write(MenuField::Details, opacity, offset + 13, 1);
                    guest.write(MenuField::Details, selected ? 128 : 0, offset + 14, 1);
                    guest.write(MenuField::Details, index < tracks_.size() ? 4 : 6, offset + 15, 1);
                }
                // Only add adjacency to custom rows; preserve stock unlock badges,
                // texture/name pointers, frame style and all other native fields.
                if (cell->custom_index || p.row == static_cast<int>(stock_rows) - 1) {
                    const auto vp = guest.read(MenuField::Details, offset + 14, 1) & 128;
                    guest.write(MenuField::Details, vp | (guest.integer(MenuField::Delay) == 0 ? layout.arrows(p) : 0),
                                offset + 14, 1);
                }
            }
    } else if (event == 5) {
        if (choice().id != preview_id_)
            guest.write(MenuField::LoadedCarrier, -1);
    } else if (event == 6) {
        if (candidate_)
            throw Error("Nested custom preview request.");
        auto selected = choice();
        if (selected.carrier == arg)
            candidate_ = std::move(selected);
    } else if (event == 7) {
        if (returned && candidate_) {
            if (pending_)
                throw Error("An accepted preview would replace an outstanding scene request.");
            pending_ = candidate_;
            preview_id_ = candidate_->id;
        }
        candidate_.reset();
    } else if (event == 9 || event == 10 || event == 11) {
        const auto root = root_at(cursor());
        const auto cup = cup_at(cursor());
        if ((root && root->carrier == arg) || (cup && cup->preview.carrier == arg)) {
            effect.override_return = true;
            if (event == 9) {
                if (!name_address_)
                    throw Error("Custom course names are unavailable.");
                const auto index = *layout.cell(cursor())->custom_index;
                effect.return_value = name_address_ + static_cast<std::uint32_t>(name_offsets_[index]);
            } else if (event == 11)
                effect.return_value = root ? root->vehicles : 1U << cup->vehicle;
            else if (cup)
                effect.return_value = cup->vehicle;
            else {
                unsigned vehicle = 0;
                while (!(root->vehicles & (1U << vehicle)))
                    ++vehicle;
                effect.return_value = vehicle;
            }
        }
    } else if (event == 12 && arg == 2) {
        const auto selected = choice();
        selected_cup_ = cup_at(cursor()) ? cup_at(cursor())->id : std::string{};
        if (!selected.id.empty()) {
            if (!allow_races_)
                throw Error("Custom race activation has not passed the save-isolation qualification gate.");
            if (selected_cup_.empty())
                race_ = selected;
            else
                race_.reset();
        } else
            race_.reset();
    } else if (event == 13) {
        if (index_at(cursor()))
            guest.write(MenuField::VoiceDelay, 0);
    } else if (event == 16) {
        if (background_y_ || background_display_list_)
            throw Error("Nested custom track-menu background draw.");
        // thread3_main draws backgrounds through &gCurrDisplayList BEFORE
        // menu_loop adopts that list into sMenuCurrDisplayList. Preserve the
        // real callback argument across its retail register-clobbering body.
        guest.check(arg, 4);
        if (arg % 4)
            throw Error("Unaligned native background display-list slot.");
        background_display_list_ = arg;
        const auto y = guest.real(MenuField::Y);
        const auto height = guest.integer(MenuField::Height);
        if (height < 120 || height > 1024)
            throw Error("Invalid native menu viewport height.");
        if (y < -(static_cast<int>(stock_rows) - 1) * height) {
            background_y_ = y;
            // Clamp to the LAST stock world, not Dino Domain. In particular
            // restoring Future Fun Land must never reuse the first world's art.
            guest.real(MenuField::Y, -(static_cast<int>(stock_rows) - 1) * height);
        }
    } else if (event == 17) {
        if (!background_display_list_)
            throw Error("Native background draw has no matching entry.");
        effect.background_display_list = *background_display_list_;
        background_display_list_.reset();
        if (background_y_) {
            guest.real(MenuField::Y, *background_y_);
            background_y_.reset();
        }
        const auto height = guest.integer(MenuField::Height);
        if (height < 120 || height > 1024)
            throw Error("Invalid native menu viewport height.");
        const auto y = guest.real(MenuField::Y);
        const int row = static_cast<int>(std::floor(-y / height));
        for (int r = std::max(static_cast<int>(stock_rows), row); r <= row + 1 && r < static_cast<int>(layout.rows());
             ++r) {
            const float top = r * height + y;
            if (top >= height || top + height <= 0)
                continue;
            const auto texture =
                name_address_ + static_cast<std::uint32_t>(background_offset_ + ((r - stock_rows) % 6) * 768);
            const auto flip = guest.read(MenuField::VertexFlip);
            if (flip > 1)
                throw Error("Invalid native menu background buffer.");
            const auto vertices = texture + 600 + flip * 72, triangles = vertices + 40;
            auto write_at = [&](std::uint32_t at, std::uint32_t value, unsigned size) {
                const auto offset = guest.check(at, size);
                for (unsigned k = 0; k < size; ++k)
                    memory[(offset + k) ^ 3] = static_cast<std::uint8_t>(value >> ((size - 1 - k) * 8));
            };
            for (unsigned v = 0; v < 4; ++v) {
                write_at(vertices + v * 10, static_cast<std::uint16_t>(v & 1 ? 160 : -160), 2);
                write_at(vertices + v * 10 + 2,
                         static_cast<std::uint16_t>(static_cast<int>(height / 2 - top - (v >= 2 ? height : 0))), 2);
                write_at(vertices + v * 10 + 4, static_cast<std::uint16_t>(-1024), 2);
                write_at(vertices + v * 10 + 6, 0xffffffffU, 4);
            }
            constexpr std::uint32_t tris[]{0x40000201, 0,          0x000001E0, 0x01E00000,
                                           0x40010203, 0x01E00000, 0x000001E0, 0x01E001E0};
            for (unsigned t = 0; t < 8; ++t)
                write_at(triangles + t * 4, tris[t], 4);
            effect.backgrounds.push_back({texture, vertices, triangles});
        }
    }
    return effect;
}

void append_track_background_quad(std::span<std::uint8_t> memory, std::uint32_t slot,
                                  const TrackMenuEffect::Background& background) {
    if (memory.size() % 4)
        throw Error("Unaligned native background memory.");
    const auto range = [&](std::uint32_t address, std::size_t size, unsigned alignment) {
        const auto offset = std::size_t(address & 0x1fffffffU);
        if ((address & 0xe0000000U) != 0x80000000U || address % alignment || offset > memory.size() ||
            size > memory.size() - offset)
            throw Error("Invalid native background display-list range.");
        return offset;
    };
    const auto slot_at = range(slot, 4, 4);
    std::uint32_t at = 0;
    for (unsigned i = 0; i < 4; ++i)
        at = (at << 8) | memory[(slot_at + i) ^ 3];
    const auto commands = range(at, 16, 8);
    range(background.vertices, 40, 8);
    range(background.triangles, 32, 8);
    const auto word = [&](std::size_t offset, std::uint32_t value) {
        for (unsigned i = 0; i < 4; ++i)
            memory[(offset + i) ^ 3] = static_cast<std::uint8_t>(value >> ((3 - i) * 8));
    };
    word(commands, 0x04180050);
    word(commands + 4, background.vertices & 0x1fffffffU);
    word(commands + 8, 0x05110020);
    word(commands + 12, background.triangles & 0x1fffffffU);
    word(slot_at, at + 16);
}
} // namespace dkr::mods
