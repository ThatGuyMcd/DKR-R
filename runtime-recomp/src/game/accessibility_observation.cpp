#include "accessibility_observation.hpp"

#include "revision_addresses.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <mutex>

namespace dkr::runtime::accessibility {
namespace {
constexpr std::size_t kMemorySize = 0x800000;
constexpr auto kReceiptTimeout = std::chrono::milliseconds(1500);

class Reader {
public:
    explicit Reader(std::span<const std::uint8_t> memory) : memory_(memory) {}
    bool range(std::uint32_t address, std::size_t size, unsigned alignment = 1) const {
        // Accept only canonical KSEG0/KSEG1 RDRAM, never aliases made by masking
        // an arbitrary guest value. Check before any word-swap or pointer read.
        const bool cached = address >= 0x80000000U && address < 0x80800000U;
        const bool uncached = address >= 0xA0000000U && address < 0xA0800000U;
        if ((!cached && !uncached) || address % alignment != 0) return false;
        const std::size_t offset = address - (cached ? 0x80000000U : 0xA0000000U);
        return size <= memory_.size() && offset <= memory_.size() - size;
    }
    std::uint32_t value(std::uint32_t address, unsigned size = 4) const {
        const std::size_t offset = address >= 0xA0000000U ? address - 0xA0000000U : address - 0x80000000U;
        std::uint32_t result = 0;
        for (unsigned i = 0; i < size; ++i) result = (result << 8U) | memory_[(offset + i) ^ 3U];
        return result;
    }
    std::optional<std::string> text(std::uint32_t address, bool allow_empty = false) const {
        std::string result;
        for (unsigned i = 0; i < 96; ++i) {
            if (!range(address, i + 1U)) return std::nullopt;
            const auto character = value(address + i, 1);
            if (character == 0) return result.empty() && !allow_empty ? std::nullopt : std::optional{result};
            // Initial observer accepts bounded printable ASCII; an unfamiliar
            // encoding is unavailable rather than guessed or passed to speech.
            if (character < 32 || character > 126) return std::nullopt;
            result.push_back(static_cast<char>(character));
        }
        return std::nullopt;
    }
private:
    std::span<const std::uint8_t> memory_;
};

// Shared stock centered text geometry for track names and trophy intros.
// Bounds qualify copied font ink, never a source string's semantic importance.
struct FontRect { int left, top, right, bottom; };
struct FontGlyph { char ch; FontRect ink; };
std::optional<std::vector<FontGlyph>> font_glyphs(const Reader& reader,
    std::uint32_t fonts, unsigned font, const std::string& text, int cx, int cy, bool compact,
    bool middle = true, bool centered = true, std::optional<bool> width_compact = std::nullopt) {
    if (!reader.range(fonts, (font + 1U) * 0x400U, 4)) return std::nullopt;
    const auto data = fonts + font * 0x400U;
    const auto advance = reader.value(data + 0x20, 2), height = reader.value(data + 0x22, 2), blank = reader.value(data + 0x24, 2);
    if (advance > 255 || !height || height > 255 || blank > 255) return std::nullopt;
    const bool width_kerning = width_compact.value_or(compact);
    int width = 0;
    for (unsigned char ch : text) {
        if (ch == ' ') { width += blank - (width_kerning && blank ? 1 : 0); continue; }
        const auto g = data + 0x100 + (ch - 32) * 8;
        if (reader.value(g, 1) == 255) continue;
        const auto step = advance ? advance : reader.value(g + 1, 1);
        if (reader.value(g, 1) >= 32 || step <= static_cast<unsigned>(compact)) return std::nullopt;
        width += step - width_kerning;
    }
    int px = cx - (centered ? width >> 1 : 0), py = cy - (middle ? height >> 1 : 0);
    std::vector<FontGlyph> out;
    for (unsigned char ch : text) {
        if (ch == ' ') { out.push_back({' ', {px, py, px, py}}); px += blank; continue; }
        const auto g = data + 0x100 + (ch - 32) * 8;
        if (reader.value(g, 1) == 255) { out.push_back({static_cast<char>(ch), {-1, -1, -1, -1}}); continue; }
        const int left = px + reader.value(g + 2, 1), top = py + reader.value(g + 3, 1);
        const auto w = reader.value(g + 6, 1), h = reader.value(g + 7, 1);
        if (!w || !h) return std::nullopt;
        out.push_back({static_cast<char>(ch), {left, top, left + static_cast<int>(w), top + static_cast<int>(h)}});
        px += (advance ? advance : reader.value(g + 1, 1)) - compact;
    }
    return out;
}

struct VisibleFontText { std::string text; bool complete; };
VisibleFontText visible_font_text(const std::string& text, std::span<const FontGlyph> glyphs,
                                  std::span<const FontRect> later, FontRect clip = {0, 0, 320, 240}) {
    bool complete = true; std::string run, label;
    const auto finish = [&] {
        while (!run.empty() && run.back() == ' ') run.pop_back();
        if (!run.empty()) { if (!label.empty()) label += ", "; label += "visible text fragment \"" + run + "\""; }
        run.clear();
    };
    for (const auto& g : glyphs) {
        const auto r = g.ink;
        if (g.ch == ' ') { if (!run.empty()) run += ' '; continue; }
        if (r.left >= clip.left && r.top >= clip.top && r.right > r.left && r.right <= clip.right && r.bottom <= clip.bottom &&
            std::none_of(later.begin(), later.end(), [&](FontRect c) { return r.left < c.right && r.right > c.left && r.top < c.bottom && r.bottom > c.top; })) run += g.ch;
        else { complete = false; finish(); }
    }
    if (complete) label = text; else finish();
    return {std::move(label), complete};
}

std::mutex publication_mutex;
// Odd epochs mean enabled. Epoch changes reject a decode that straddles a
// disable/re-enable cycle, even when the renderer session is unchanged.
std::atomic<std::uint64_t> demand_epoch{0};
SessionToken generation = 0;
std::uint64_t sequence = 0;
std::optional<Observation> latest;
Clock::time_point received;
}

Observation decode_submission(std::span<const std::uint8_t> memory,
                              rom::Revision revision, std::uint32_t scene) {
    Observation result;
    result.revision = revision;
    result.scene = scene;
    if (memory.size() != kMemorySize ||
        (revision != rom::Revision::UsV77 && revision != rom::Revision::UsV80)) return result;
    const Observation unavailable = result;
    const auto& addresses = revision_addresses::table_for(revision);
    const Reader reader(memory);
    const auto mode = reader.value(addresses.GameMode);
    // Immutable symbols from the two retail ELFs. Do not infer a universal
    // revision delta: .data and .bss move by different amounts.
    const auto address = [revision](std::uint32_t v77, std::uint32_t v80) {
        return revision == rom::Revision::UsV77 ? v77 : v80;
    };
    const auto cursor = reader.value(address(0x800DF460, 0x800DF9E0));
    const auto add_text = [&](std::uint32_t pointer, int index, bool selected, bool enabled = true) {
        auto label = reader.text(pointer);
        if (!label) return false;
        result.menu_entries.push_back({index, *label, enabled, selected});
        if (selected) { result.selection = index; result.selection_label = *label; }
        return true;
    };
    const auto pointer_entries = [&](std::uint32_t strings, unsigned count, unsigned selected) {
        if (selected >= count || !reader.range(strings, count * 4U, 4)) return false;
        for (unsigned i = 0; i < count; ++i)
            if (!add_text(reader.value(strings + i * 4U), static_cast<int>(i), i == selected)) return false;
        return true;
    };
    const auto menu_text = [&](unsigned id) -> std::optional<std::string> {
        const auto table = reader.value(address(0x800DF4A0, 0x800DFA20));
        if (!reader.range(table, (id + 1U) * 4U, 4)) return std::nullopt;
        return reader.text(reader.value(table + id * 4U));
    };
    const auto level_name = [&](unsigned id) -> std::optional<std::string> {
        const auto count = reader.value(address(0x80121170, 0x801216F0));
        const auto table = reader.value(address(0x8012116C, 0x801216EC));
        if (!count || count > 256 || id >= count || !reader.range(table, (id + 1U) * 4U, 4)) return std::nullopt;
        auto pointer = reader.value(table + id * 4U);
        const unsigned language = (reader.value(address(0x80126448, 0x801269E8) + 4U) & 12U) >> 2U;
        // level_name skips NUL-separated localized labels. Bound the traversal
        // independently of the selected label's bounded ASCII decoder.
        unsigned skipped = 0;
        for (unsigned i = 0; skipped < language; ++i) {
            if (i >= 384 || !reader.range(pointer, 1)) return std::nullopt;
            if (reader.value(pointer++, 1) == 0) ++skipped;
        }
        return reader.text(pointer);
    };
    const auto initials_entry = [&]() {
                const auto target = reader.value(address(0x80126C6C, 0x8012722C));
                const auto string = reader.value(address(0x80126C74, 0x80127238));
                const auto length = reader.value(address(0x800E0FA0, 0x800E1520));
                if (!reader.range(target, 4, 4) || length > 3 ||
                    reader.value(address(0x80126C78, 0x8012723C)) != 3 || !reader.range(string, 4)) return false;
                const auto selected = reader.value(target);
                if (selected > 30) return false;
                std::string entered;
                for (unsigned i = 0; i < length; ++i) {
                    const auto character = reader.value(string + i, 1);
                    if (character < 32 || character > 126) return false;
                    entered.push_back(static_cast<char>(character));
                }
                auto title = menu_text(8);
                if (!title) return false;
                result.menu_name = *title; result.menu_detail = "Initials: " + entered;
                std::string label;
                if (selected < 28) {
                    const auto character = reader.value(address(0x800E0F6C, 0x800E14EC) + selected, 1);
                    if (character < 32 || character > 126) return false;
                    label.assign(1, static_cast<char>(character));
                } else label = selected == 28 ? "Space" : selected == 29 ? "Delete" : "OK";
                result.selection = selected; result.selection_label = label;
                result.menu_entries.push_back({static_cast<int>(selected), label, true, true});
                return true;
    };
    // Font windows contain only text queued for the current rendered page,
    // unlike game-text scripts which also contain unrevealed future pages.
    struct LiveText { std::string text; int x; int y; bool choice; bool selected; bool enabled; std::uint32_t node; };
    const auto live_window = [&](unsigned id) -> std::optional<std::vector<LiveText>> {
        std::vector<LiveText> lines;
        const auto windows = reader.value(address(0x8012A7E8, 0x8012ADA8));
        if (!windows) return lines;
        if (!reader.range(windows, (id + 1U) * 40U, 4)) return std::nullopt;
        const auto window = windows + id * 40U;
        if (!(reader.value(window + 0x1E, 2) & 0x8000U)) return lines;
        const auto signed_half = [&](std::uint32_t field) { return static_cast<std::int16_t>(reader.value(field, 2)); };
        if (signed_half(window + 8) < 0 || signed_half(window + 4) >= 320 ||
            signed_half(window + 10) < 0 || signed_half(window + 6) >= 240 ||
            signed_half(window + 12) <= 0 || signed_half(window + 14) <= 0) return lines;
        const auto pool = reader.value(address(0x8012A7EC, 0x8012ADAC));
        if (!reader.range(pool, 64U * 32U, 4)) return std::nullopt;
        auto node = reader.value(window + 36);
        std::vector<std::uint32_t> visited;
        while (node) {
            if (node < pool || node - pool >= 64U * 32U || (node - pool) % 32U ||
                std::find(visited.begin(), visited.end(), node) != visited.end()) return std::nullopt;
            visited.push_back(node);
            if (reader.value(node + 1, 1) == 255) return std::nullopt;
            const int local_x = signed_half(node + 8) + signed_half(node + 12);
            const int local_y = signed_half(node + 10) + signed_half(node + 14);
            const int screen_y = signed_half(window + 6) + local_y;
            // render_text_string clips to this window. Do not inspect strings
            // queued wholly beyond its bottom/right or the screen's bottom.
            const bool within_text_view = local_x < signed_half(window + 12) &&
                local_y >= 0 && local_y < signed_half(window + 14) && screen_y >= 0 && screen_y < 240;
            if (within_text_view && reader.value(node + 24, 1) && reader.value(node + 25, 1) < 3) {
                auto text = reader.text(reader.value(node + 4), true);
                if (!text) return std::nullopt;
                // parse_string_with_number substitutes the visible window number.
                const auto number = std::to_string(reader.value(node + 1, 1));
                for (std::size_t at = 0; (at = text->find('~', at)) != std::string::npos; at += number.size()) text->replace(at, 1, number);
                const auto red = reader.value(node + 16, 1), green = reader.value(node + 17, 1), blue = reader.value(node + 18, 1);
                // NPC headings occupy y=6; the defeat page's second heading
                // is y=20. Their inherited colour is not an option marker.
                const bool option_row = id == 1 && reader.value(address(0x8012A789, 0x8012AD49), 1) == 0 && signed_half(node + 10) >= 24;
                const bool selected = option_row && red == 207 && green == 207 && blue == 207;
                const bool choice = option_row && (selected || (red == 0 && green == 0 && blue == 127));
                if (!text->empty()) lines.push_back({*text, signed_half(node + 8), signed_half(node + 10), choice, selected,
                                                    reader.value(node + 24, 1) == 255, node});
            }
            node = reader.value(node + 28);
        }
        if (std::count_if(lines.begin(), lines.end(), [](const LiveText& line) { return line.selected; }) > 1) return std::nullopt;
        std::stable_sort(lines.begin(), lines.end(), [](const LiveText& a, const LiveText& b) {
            return a.y == b.y ? a.x < b.x : a.y < b.y;
        });
        return lines;
    };
    const auto dialogue = [&](const std::vector<LiveText>& lines, const std::string& name) {
        result.kind = StateKind::Dialogue; result.menu_name = name;
        for (const auto& line : lines) {
            if (line.choice) {
                const int index = static_cast<int>(result.menu_entries.size());
                result.menu_entries.push_back({index, line.text, line.enabled, line.selected});
                if (line.selected) { result.selection = index; result.selection_label = line.text; }
            } else if (result.menu_detail.find(line.text) == std::string::npos) {
                if (!result.menu_detail.empty()) result.menu_detail += " ";
                result.menu_detail += line.text;
            }
        }
        if (result.selection_label.empty()) {
            result.selection = 0; result.selection_label = result.menu_detail;
            result.menu_detail.clear();
            result.menu_entries.push_back({0, result.selection_label, false, true});
        }
    };
    if ((mode == 0 || mode == 1) && reader.value(addresses.LevelLoadTimer, 2) == 0 &&
        !(mode == 1 && reader.value(addresses.CurrentMenuId) == 14)) {
        for (unsigned window : {6U, 1U}) {
            auto lines = live_window(window);
            if (!lines) return unavailable;
            if (!lines->empty()) {
                dialogue(*lines, window == 6 ? "Message" : "Dialogue");
                if (window == 1 && reader.value(address(0x8012A789, 0x8012AD49), 1) == 1 &&
                    reader.value(address(0x8012A787, 0x8012AD47), 1) != 0 &&
                    reader.value(address(0x8012A78A, 0x8012AD4A), 1) == 0)
                    result.menu_detail += " Press A to continue.";
                return result;
            }
        }
    }
    if (mode == 1 && reader.value(addresses.MenuDelay) == 0) {
        const auto menu = reader.value(addresses.CurrentMenuId);
        if (menu == 28) {
            // The mandatory controller-pak caution page is drawn directly from
            // 13 MenuElements; element zero is only the title's drop shadow.
            const auto elements = address(0x800DFFD8, 0x800E0558);
            auto title = reader.text(reader.value(elements + 32U + 0x14));
            if (!title) return unavailable;
            result.kind = StateKind::Dialogue; result.menu_name = *title;
            for (unsigned i = 2; i < 13; ++i) {
                auto line = reader.text(reader.value(elements + i * 32U + 0x14), true);
                if (!line) return unavailable;
                if (!line->empty()) { if (!result.menu_detail.empty()) result.menu_detail += " "; result.menu_detail += *line; }
            }
            // menu_caution_loop's accepted controls and one-second lockout.
            result.menu_detail += " Press A, B, or Start to continue.";
            result.selection = 0; result.selection_label = "Continue";
            result.menu_entries.push_back({0, "Continue", reader.value(address(0x801263C4, 0x80126964)) == 0, true});
        } else if (menu == 26) {
            // Boot phase2 renders this manager directly, repeatedly recycling
            // window6. Its final linked list is not the complete seven-row page.
            // Error/delete/wait views are not reconstructed from file metadata.
            const auto scroll = reader.value(address(0x801263D8, 0x80126978));
            const auto selected = static_cast<std::int32_t>(cursor);
            const auto pak = reader.value(address(0x80126A68, 0x80127028));
            const auto blink = reader.value(address(0x801263BC, 0x8012695C));
            const auto tv = reader.value(0x80000300);
            if (reader.value(address(0x80126C20, 0x801271E0)) != 2 ||
                reader.value(address(0x800DFD94, 0x800E0314)) != 1 ||
                reader.value(address(0x80126BC8, 0x80127188)) != 0 || reader.value(addresses.MenuStage) != 0 ||
                reader.value(address(0x80126C10, 0x801271D0)) != 0 ||
                reader.value(address(0x80126BB4, 0x80127174)) != 7 || scroll > 9 || pak != 0 ||
                selected < -1 || selected > 16 || blink > 63 || tv == 0 || tv > 2 ||
                reader.value(addresses.LevelLoadTimer, 2) != 0 || reader.value(address(0x800E31AC, 0x800E373C)) != 0 ||
                reader.value(address(0x8012A7F0, 0x8012ADB0)) != 0) return unavailable;
            if ((selected >= 0 && selected < 16 && (selected < static_cast<int>(scroll) || selected >= static_cast<int>(scroll + 7))) ||
                (selected == 16 && scroll != 9)) return unavailable;
            // Render precedes input: suppress the tick that changes cursor/page
            // or refreshes pak data, rather than describing an unseen new page.
            for (unsigned i = 0; i < 4; ++i) {
                if (reader.value(address(0x8012645C, 0x801269FC) + i, 1) ||
                    reader.value(address(0x80126464, 0x80126A04) + i, 1) ||
                    (reader.value(address(0x80121140, 0x801216C0) + i * 2U, 2) & 0xD000U)) return unavailable;
            }
            const auto framebuffer = reader.value(address(0x801262C8, 0x80126868));
            const auto fonts = reader.value(address(0x8012A7E4, 0x8012ADA4));
            const auto windows = reader.value(address(0x8012A7E8, 0x8012ADA8));
            if (framebuffer > 1 || reader.value(address(0x801262B0, 0x80126850) + framebuffer * 4U) != 320 ||
                reader.value(address(0x801262B8, 0x80126858) + framebuffer * 4U) != 240 ||
                !reader.range(windows, 40, 4) || !font_glyphs(reader, fonts, 2, "", 0, 0, false)) return unavailable;
            const auto font = reader.value(windows + 29, 1);
            const auto last_y = static_cast<std::int16_t>(reader.value(windows + 2, 2));
            const int title_top = 30 - (reader.value(fonts + 2 * 0x400 + 0x22, 2) >> 1);
            // draw_text retains the final glyph-line y in font window0. On a
            // completed delete tick, fields have reset to CHOOSE but the rendered
            // frame still ends in PLEASE WAIT at y128: do not read refreshed notes.
            if (reader.value(windows + 32, 2) || reader.value(windows + 34, 2) ||
                (scroll < 9 ? font != 2 || last_y != title_top : font != 0 || last_y != 210)) return unavailable;
            auto title = menu_text(35), pak_name = menu_text(86 + pak), free_label = menu_text(114);
            auto note_header = menu_text(115), file_header = menu_text(116), pages_header = menu_text(117);
            auto exit = scroll == 9 ? menu_text(51) : std::optional<std::string>{std::string{}};
            const auto free_pages = reader.value(address(0x80126BB0, 0x80127170));
            if (!title || !pak_name || !free_label || !note_header || !file_header || !pages_header || !exit || free_pages > 123) return unavailable;
            const auto substitute = [](std::string text, unsigned value) {
                const auto number = std::to_string(value);
                for (std::size_t at = 0; (at = text.find('~', at)) != std::string::npos; at += number.size()) text.replace(at, 1, number);
                return text;
            };
            struct Field { std::string text; std::vector<FontGlyph> ink; unsigned number, order; };
            const auto fields = [&](const std::vector<std::string>& labels, const std::vector<unsigned>& numbers,
                                    const std::vector<int>& x, const std::vector<bool>& centered,
                                    unsigned font_id, int y, FontRect clip, bool compact) -> std::optional<std::vector<std::string>> {
                std::vector<Field> drawn;
                for (unsigned i = 0; i < labels.size(); ++i) {
                    auto glyphs = font_glyphs(reader, fonts, font_id, labels[i], x[i], y, compact, false, centered[i], false);
                    if (!glyphs) return std::nullopt;
                    drawn.push_back({labels[i], std::move(*glyphs), numbers[i], i});
                }
                // render_dialogue_text sorts its linked list by descending
                // substitution number; equal-number newer nodes paint first.
                std::stable_sort(drawn.begin(), drawn.end(), [](const Field& a, const Field& b) {
                    return a.number == b.number ? a.order > b.order : a.number > b.number;
                });
                std::vector<std::string> out(labels.size());
                for (unsigned i = 0; i < drawn.size(); ++i) {
                    std::vector<FontRect> later;
                    for (unsigned j = i + 1; j < drawn.size(); ++j) for (const auto& glyph : drawn[j].ink)
                        if (glyph.ch != ' ') later.push_back(glyph.ink);
                    out[drawn[i].order] = visible_font_text(drawn[i].text, drawn[i].ink, later, clip).text;
                }
                return out;
            };
            auto title_ink = font_glyphs(reader, fonts, 2, *title, 160, 30, false);
            if (!title_ink) return unavailable;
            const std::array<FontRect, 2> later_panels = {{{58,48,262,78}, {28,82,292,208}}};
            const auto visible_title = visible_font_text(*title, *title_ink, later_panels).text;
            if (visible_title.empty()) return unavailable;
            result.kind = StateKind::OptionsMenu; result.menu_name = visible_title;
            // Pak header contains two separately positioned lines and numbers.
            *pak_name = substitute(*pak_name, 1);
            auto header_ink = font_glyphs(reader, fonts, 0, *pak_name, 160, 50, false, false);
            const auto free_text = substitute(*free_label, free_pages);
            auto free_ink = font_glyphs(reader, fonts, 0, free_text, 160, 64, false, false);
            if (!header_ink || !free_ink) return unavailable;
            std::vector<FontRect> pak_cover;
            // The free-page number determines this pair's actual draw order.
            if (free_pages < 1) for (const auto& g : *free_ink) if (g.ch != ' ') pak_cover.push_back(g.ink);
            const auto visible_pak = visible_font_text(*pak_name, *header_ink, pak_cover, {58,48,262,78}).text;
            std::vector<FontRect> free_cover;
            if (free_pages >= 1) for (const auto& g : *header_ink) if (g.ch != ' ') free_cover.push_back(g.ink);
            const auto visible_free = visible_font_text(free_text, *free_ink, free_cover, {58,48,262,78}).text;
            if (!visible_pak.empty()) result.menu_entries.push_back({100, visible_pak, true, selected == -1});
            result.menu_detail = visible_free;
            auto headings = fields({*note_header, *file_header, *pages_header}, {scroll,1,1}, {54,84,268}, {true,false,true},
                1, 84, {28,82,292,96}, revision == rom::Revision::UsV80);
            if (!headings) return unavailable;
            const auto labels = address(0x80126AA0, 0x80127060);
            const auto base = reader.value(labels);
            if (!reader.range(base, 512, 4)) return unavailable;
            for (unsigned row = 0; row < 7; ++row) {
                const unsigned index = scroll + row;
                const auto pointer = reader.value(labels + index * 4U);
                if (pointer != base + index * 32U) return unavailable;
                std::string name;
                bool terminated = false;
                for (unsigned j = 0; j < 32; ++j) {
                    const auto ch = reader.value(pointer + j, 1);
                    if (!ch) { terminated = true; break; }
                    if (ch < 32 || ch > 126) return unavailable;
                    name += static_cast<char>(ch);
                }
                if (!terminated) return unavailable;
                const auto pages = reader.value(address(0x80126B70, 0x80127130) + index * 4U);
                if (pages > 123) return unavailable;
                const int top = 98 + row * 16;
                auto values = fields({std::to_string(index + 1), substitute(name, 1), std::to_string(pages)},
                    {index + 1,1,pages}, {54,84,268}, {true,false,true}, 1, top + 2, {28,top,292,top+14}, revision == rom::Revision::UsV80);
                if (!values) return unavailable;
                std::string line;
                for (unsigned column = 0; column < 3; ++column) if (!(*values)[column].empty()) {
                    if (!line.empty()) line += ", ";
                    if (!(*headings)[column].empty()) line += (*headings)[column] + " ";
                    line += (*values)[column];
                }
                if (!line.empty()) result.menu_entries.push_back({static_cast<int>(index), line, true, selected == static_cast<int>(index)});
            }
            if (scroll == 9) {
                auto ink = font_glyphs(reader, fonts, 0, *exit, 160, 210, false, false);
                if (!ink) return unavailable;
                auto label = visible_font_text(*exit, *ink, {}).text;
                if (!label.empty()) result.menu_entries.push_back({16, label, true, selected == 16});
            } else if (blink & 8U) {
                // This later arrow starts at y210, below all admitted row ink.
                const auto arrow = address(0x800E043C, 0x800E09BC);
                if (reader.value(arrow + 4, 2) != 0xFFF4U || reader.value(arrow + 6, 2) != 0xFFF8U ||
                    reader.value(arrow + 8) != 0) return unavailable;
            }
            for (const auto& entry : result.menu_entries) if (entry.selected) {
                result.selection = entry.index; result.selection_label = entry.label;
            }
            if (!result.menu_detail.empty()) result.menu_detail += ". ";
            result.menu_detail += "Press B to leave the Controller Pak manager";
        } else if (menu == 0 && reader.value(addresses.TitleRevealTimer) == 32) {
            // titlescreen_render hides its labels when the controller is
            // missing and highlights gTitleScreenCurrentOption, not the
            // generic menu cursor used by the other menus.
            if (reader.value(address(0x800DD384, 0x800DD8F4)) == 0xFFFFFFFFU) return unavailable;
            const auto title_selection = reader.value(address(0x800DF45C, 0x800DF9DC));
            if (!pointer_entries(address(0x800DF7A4, 0x800DFD24), 2, title_selection)) return unavailable;
            result.kind = StateKind::TitleMenu; result.menu_name = "Title";
        } else if (menu == 12) {
            if (!pointer_entries(address(0x800DFA10, 0x800DFF90), 6, cursor)) return unavailable;
            result.kind = StateKind::OptionsMenu; result.menu_name = "Options";
        } else if (menu == 13) {
            // menu_audio_options_loop draws this table only while -20 < delay
            // < 20. The surrounding delay == 0 gate deliberately excludes fades.
            const auto stage = reader.value(addresses.MenuStage);
            const auto selected = reader.value(address(0x80126C46, 0x80127206), 2);
            const auto output = reader.value(address(0x800DFAC8, 0x800E0048));
            const auto sfx = reader.value(address(0x800DFAC0, 0x800E0040));
            const auto music = reader.value(address(0x800DFAC4, 0x800E0044));
            if ((stage != 4 && stage != 5) || selected >= stage || output > 2 || sfx > 256 || music > 256)
                return unavailable;
            const auto strings = address(0x800DFA3C, 0x800DFFBC);
            const auto row = [&](unsigned index) { return reader.value(strings + index * 16U + 12U); };
            const auto output_string = reader.value(address(0x801269D0, 0x80126F90) + output * 4U);
            // The render mutates row zero to the current output text. Require a
            // coherent rendered table, including its stock terminating row.
            if (row(0) != output_string || row(4) != row(5) || row(stage == 4 ? 6 : 7) != 0)
                return unavailable;
            auto title = reader.text(row(5));
            if (!title) return unavailable;
            result.kind = StateKind::OptionsMenu; result.menu_name = *title;
            for (unsigned index = 0; index < stage; ++index) {
                const unsigned visible_row = index < 3 ? index : stage == 5 && index == 3 ? 6 : 3;
                auto label = reader.text(row(visible_row));
                if (!label) return unavailable;
                if (index == 1 || index == 2) {
                    // The arrow is drawn at (volume >> 1) + 96. Describe that
                    // quantized visible position, never a printed/raw volume.
                    const auto position = (index == 1 ? sfx : music) >> 1U;
                    *label += ": slider " + std::to_string((position * 100U + 64U) / 128U) + "%";
                }
                result.menu_entries.push_back({static_cast<int>(index), *label, true, index == selected});
                if (index == selected) { result.selection = index; result.selection_label = *label; }
            }
        } else if (menu == 14) {
            // savemenu_render: stock US 320x240 layout, stable (non-fade)
            // submissions only. A modal is drawn last and covers the cards.
            const auto stage = reader.value(addresses.MenuStage);
            const auto framebuffer = reader.value(address(0x801262C8, 0x80126868));
            const auto tv_type = reader.value(0x80000300);
            if (stage > 15 || reader.value(addresses.LevelLoadTimer, 2) != 0 || framebuffer > 1 ||
                (tv_type != 1 && tv_type != 2) ||
                reader.value(address(0x801262B0, 0x80126850) + framebuffer * 4U) != 320 ||
                reader.value(address(0x801262B8, 0x80126858) + framebuffer * 4U) != 240) return unavailable;
            auto title = menu_text(165);
            if (!title) return unavailable;
            if (stage & 8U) {
                auto lines = live_window(7);
                const auto count = reader.value(address(0x80126A74, 0x80127034));
                const auto selected = reader.value(address(0x80126A78, 0x80127038));
                const auto choices = address(0x80126A80, 0x80127040);
                if (!lines || lines->empty() || count == 0 || count > 4 || selected >= count) return unavailable;
                for (auto& line : *lines) { line.choice = false; line.selected = false; }
                std::vector<std::uint32_t> nodes;
                for (unsigned i = 0; i < count; ++i) {
                    const auto node = reader.value(choices + i * 4U);
                    if (std::find(nodes.begin(), nodes.end(), node) != nodes.end()) return unavailable;
                    nodes.push_back(node);
                    auto line = std::find_if(lines->begin(), lines->end(),
                        [node](const LiveText& text) { return text.node == node; });
                    if (line == lines->end()) return unavailable;
                    line->choice = true; line->selected = i == selected;
                }
                dialogue(*lines, *title);
                return result;
            }
            result.kind = StateKind::OptionsMenu; result.menu_name = *title;
            const auto phase = stage & 7U;
            if (phase == 1 || phase == 2 || phase == 7) {
                auto wait = menu_text(124); if (!wait) return unavailable;
                result.menu_detail = *wait;
            }
            // Each field has independent clipping; card backgrounds are not
            // evidence that the centered text or its count is fully visible.
            const auto fonts = reader.value(address(0x8012A7E4, 0x8012ADA4));
            const auto compact = reader.value(address(0x8012A7F0, 0x8012ADB0));
            bool unqualified_sprite_field = false;
            struct InkRect { int left; int top; int right; int bottom; };
            std::vector<InkRect> foreground;
            const auto overlaps_foreground = [&](InkRect rectangle) {
                return std::any_of(foreground.begin(), foreground.end(), [&](InkRect ink) {
                    return rectangle.left < ink.right && rectangle.right > ink.left &&
                        rectangle.top < ink.bottom && rectangle.bottom > ink.top;
                });
            };
            const auto visible_text = [&](const std::string& text, unsigned font, int center, int y,
                                          bool middle, bool collect_foreground = false,
                                          std::vector<InkRect>* ink_out = nullptr,
                                          const std::vector<InkRect>* covered_by = nullptr) -> std::optional<std::string> {
                if (compact > 1 || !reader.range(fonts, (font + 1U) * 0x400U, 4)) return std::nullopt;
                const auto data = fonts + font * 0x400U;
                const auto spacing = reader.value(data + 0x20, 2);
                const auto height = reader.value(data + 0x22, 2);
                const auto blank = reader.value(data + 0x24, 2);
                if (spacing > 255 || height == 0 || height > 255 || blank > 255) return std::nullopt;
                int width = 0;
                for (unsigned char ch : text) {
                    if (ch == ' ') { width += blank - (compact && blank ? 1U : 0U); continue; }
                    const auto glyph = data + 0x100U + (ch - 32U) * 8U;
                    const auto texture = reader.value(glyph, 1);
                    if (texture == 255) continue;
                    if (texture >= 32) return std::nullopt;
                    const auto advance = spacing ? spacing : reader.value(glyph + 1, 1);
                    if (advance == 0 || (compact && advance == 1)) return std::nullopt;
                    width += advance - compact;
                }
                int x = center - (width >> 1);
                const int top = y - (middle ? height >> 1 : 0);
                bool complete = true;
                std::string run;
                std::vector<std::string> fragments;
                const auto finish_run = [&] {
                    while (!run.empty() && run.back() == ' ') run.pop_back();
                    if (!run.empty()) fragments.push_back(std::move(run));
                    run.clear();
                };
                for (unsigned char ch : text) {
                    if (ch == ' ') { if (!run.empty()) run += ' '; x += blank; continue; }
                    const auto glyph = data + 0x100U + (ch - 32U) * 8U;
                    if (reader.value(glyph, 1) == 255) { complete = false; finish_run(); continue; }
                    const int left = x + reader.value(glyph + 2, 1);
                    const int upper = top + reader.value(glyph + 3, 1);
                    const auto ink_width = reader.value(glyph + 6, 1), ink_height = reader.value(glyph + 7, 1);
                    if (ink_width == 0 || ink_height == 0) return std::nullopt;
                    const InkRect ink{left, upper, left + static_cast<int>(ink_width), upper + static_cast<int>(ink_height)};
                    if (collect_foreground) foreground.push_back(ink);
                    if (ink_out) ink_out->push_back(ink);
                    if (left >= 0 && ink.right <= 320 && upper >= 0 && ink.bottom <= 240 &&
                        (collect_foreground || !overlaps_foreground(ink)) &&
                        (!covered_by || std::none_of(covered_by->begin(), covered_by->end(), [&](InkRect later) {
                            return ink.left < later.right && ink.right > later.left && ink.top < later.bottom && ink.bottom > later.top;
                        })))
                        run += static_cast<char>(ch);
                    else { complete = false; finish_run(); }
                    x += (spacing ? spacing : reader.value(glyph + 1, 1)) - compact;
                }
                if (complete) return text;
                finish_run();
                std::string visible;
                for (const auto& fragment : fragments) {
                    if (!visible.empty()) visible += ", ";
                    visible += "visible text fragment \"" + fragment + "\"";
                }
                return visible;
            };
            // Confirmation/wait text is painted last without an opaque box.
            // Withhold background glyphs/sprites whose ink bounds it overlaps.
            if (phase == 6 || phase == 7) {
                const auto overlay = phase == 6 ? std::optional<std::string>{"OK?"} : menu_text(124);
                if (!overlay || !visible_text(*overlay, 2, 160, 128, true, true)) return unavailable;
            }
            const auto sprite_visible = [&](unsigned image, unsigned frame, int anchor_x, int anchor_y,
                                            const std::vector<InkRect>& later_ink) -> std::optional<bool> {
                // SpriteAsset anchors are not retained in Sprite. The actual
                // sprite_init_frame vertex uploads preserve those anchors and
                // tile sizes. Recognize only its bounded stock command shape.
                const auto images = reader.value(address(0x800DF75C, 0x800DFCDC));
                if (!reader.range(images, 18U * 32U, 4)) return std::nullopt;
                const auto transform = images + image * 32U;
                const unsigned asset = image == 2 ? 2 : image == 11 ? 0x40 : 0x41;
                if (reader.value(transform + 6, 2) != asset || reader.value(transform, 2) ||
                    reader.value(transform + 2, 2) || reader.value(transform + 4, 2) ||
                    reader.value(transform + 8) != 0x3F800000U ||
                    reader.value(transform + 0x14) != (image == 2 ? 0xC2000000U : 0U)) return std::nullopt;
                const auto opacity = reader.value(address(0x800DF764, 0x800DFCE4));
                if (opacity > 255) return std::nullopt;
                if (opacity == 0) return false;
                const auto ortho = address(0x800DD2B8, 0x800DD828);
                for (unsigned i = 0; i < 16; ++i) {
                    const auto expected = i == 0 || i == 5 ? 0x3F800000U : i == 15 ? 0x43200000U : 0U;
                    if (reader.value(ortho + i * 4U) != expected) return std::nullopt;
                }
                const auto camera = reader.value(address(0x80120CE4, 0x80121264));
                const auto adjust = reader.value(address(0x80120D15, 0x80121295), 1);
                if (camera > 3 || reader.value(addresses.Cameras + camera * 0x44U + 4U, 2) != 0 || adjust > 1 ||
                    (adjust && reader.value(address(0x80126174, 0x80126714)) != 0x3F800000U)) return std::nullopt;
                const auto ids = reader.value(address(0x800DF750, 0x800DFCD0));
                const auto count = reader.value(address(0x800DF754, 0x800DFCD4), 2);
                if (count <= asset || count > 128 || !reader.range(ids, count * 2U, 2) ||
                    (reader.value(ids + asset * 2U, 2) & 0xC000U) != 0x8000U) return std::nullopt;
                const auto sprite = reader.value(address(0x80126550, 0x80126AF0) + asset * 4U);
                if (!sprite) return false; // menu_element_render draws nothing.
                if (!reader.range(sprite, 12, 4)) return std::nullopt;
                const auto frames = reader.value(sprite, 2);
                if (frames < (image == 2 ? 10U : 1U) || frames > 256 ||
                    !reader.range(sprite, 12U + (frame + 1U) * 4U, 4) ||
                    frame >= frames || (reader.value(sprite + 6, 2) & 10U) ||
                    reader.value(address(0x800DF4B4, 0x800DFA34)) != 0 ||
                    (image != 2 && reader.value(transform + 0x18, 2) != 0)) return std::nullopt;
                auto list = reader.value(sprite + 12U + frame * 4U);
                bool all_visible = true, saw_vertices = false;
                unsigned pending_quads = 0, batch_quads = 0;
                bool tile_ready = false;
                for (unsigned command = 0; command < 64; ++command, list += 8U) {
                    if (!reader.range(list, 8, 4)) return std::nullopt;
                    const auto w0 = reader.value(list), w1 = reader.value(list + 4);
                    const auto opcode = w0 >> 24U;
                    if (opcode == 0xB8) return w0 == 0xB8000000U && w1 == 0 && saw_vertices && pending_quads == 0 && !tile_ready ?
                        std::optional<bool>{all_visible} : std::nullopt;
                    if (opcode == 0xE7) { if (w0 != 0xE7000000U || w1 || pending_quads) return std::nullopt; continue; }
                    // DMA addresses in generated sprite lists are physical;
                    // qualify them explicitly instead of masking guest pointers.
                    if (w1 >= 0x800000U) return std::nullopt;
                    const auto pointer = 0x80000000U + w1;
                    if (opcode == 7) {
                        const auto commands = (w0 >> 16U) & 255U;
                        if (tile_ready || !commands || (w0 & 0xFFFFU) != commands * 8U || !reader.range(pointer, commands * 8U, 4))
                            return std::nullopt;
                        tile_ready = true;
                    } else if (opcode == 4) {
                        const auto vertices = (((w0 >> 16U) & 255U) >> 3U) + 1U;
                        if (!tile_ready || pending_quads || vertices > 20 || vertices % 4 || !(w0 & 0x10000U) ||
                            (w0 & 0xFFFFU) != vertices * 18U + 8U || !reader.range(pointer, vertices * 10U, 2)) return std::nullopt;
                        pending_quads = batch_quads = vertices / 4U; saw_vertices = true;
                        InkRect quad{320, 240, 0, 0};
                        int quad_x[4], quad_y[4];
                        for (unsigned i = 0; i < vertices; ++i) {
                            const auto vertex = pointer + i * 10U;
                            const int x = anchor_x + static_cast<std::int16_t>(reader.value(vertex, 2));
                            const int y = anchor_y - static_cast<std::int16_t>(reader.value(vertex + 2, 2));
                            if (reader.value(vertex + 4, 2) != 0) return std::nullopt;
                            quad_x[i % 4] = x; quad_y[i % 4] = y;
                            all_visible &= x >= 0 && x < 320 && y >= 0 && y < 240;
                            quad.left = std::min(quad.left, x); quad.right = std::max(quad.right, x + 1);
                            quad.top = std::min(quad.top, y); quad.bottom = std::max(quad.bottom, y + 1);
                            if (i % 4 == 3) {
                                if (quad_x[0] != quad_x[3] || quad_x[1] != quad_x[2] ||
                                    quad_y[0] != quad_y[1] || quad_y[2] != quad_y[3] ||
                                    quad_x[1] < quad_x[0] || quad_y[2] < quad_y[0]) return std::nullopt;
                                const bool covered = std::any_of(later_ink.begin(), later_ink.end(), [&](InkRect ink) {
                                    return quad.left < ink.right && quad.right > ink.left && quad.top < ink.bottom && quad.bottom > ink.top;
                                });
                                all_visible &= quad_x[1] > quad_x[0] && quad_y[2] > quad_y[0] && !overlaps_foreground(quad) && !covered;
                                quad = {320, 240, 0, 0};
                            }
                        }
                    } else if (opcode == 5) {
                        if (!tile_ready || w0 != 0x05110020U || !pending_quads || !reader.range(pointer, 32, 2)) return std::nullopt;
                        const unsigned base = (batch_quads - pending_quads) * 4U;
                        const unsigned indices[] = {base + 3, base + 2, base + 1, base + 4, base + 3, base + 1};
                        for (unsigned i = 0; i < 6; ++i)
                            if (reader.value(pointer + (i < 3 ? 1U + i : 16U + i - 2U), 1) != indices[i]) return std::nullopt;
                        tile_ready = false;
                        --pending_quads;
                    } else return std::nullopt;
                }
                return std::nullopt;
            };
            const auto filename = [&](unsigned compressed, bool trim) -> std::optional<std::string> {
                const auto alphabet = address(0x800E0F6C, 0x800E14EC);
                std::string text(3, ' ');
                for (int i = 2; i >= 0; --i, compressed >>= 5U) {
                    const auto ch = reader.value(alphabet + (compressed & 31U), 1);
                    if (ch < 32 || ch > 126) return std::nullopt;
                    text[i] = static_cast<char>(ch);
                }
                if (trim) while (!text.empty() && text.back() == ' ') text.pop_back();
                return text;
            };
            struct DrawnCard {
                int index, x, y;
                bool active, highlighted, symbols, adventure_two;
                std::optional<unsigned> balloons;
                std::string printed;
                std::vector<InkRect> later_ink;
                std::optional<std::string> heading;
                std::string label;
                std::vector<InkRect> name_ink;
            };
            std::vector<DrawnCard> drawn_cards;
            const auto cards = [&](bool destination) {
                const auto count = reader.value(destination ? address(0x80126A00, 0x80126FC0) : address(0x80126A08, 0x80126FC8));
                const auto array = reader.value(destination ? address(0x80126A04, 0x80126FC4) : address(0x80126A0C, 0x80126FCC));
                const auto selected = reader.value(destination ? address(0x80126BE4, 0x801271A4) : address(0x80126BD4, 0x80127194));
                const auto scroll_bits = reader.value(destination ? address(0x80126BEC, 0x801271AC) : address(0x80126BDC, 0x8012719C));
                const float scroll = std::bit_cast<float>(scroll_bits);
                if (count == 0 || count > 80 || selected >= count || !reader.range(array, count * 16U, 4) ||
                    !std::isfinite(scroll) || scroll < 0 || scroll > count - 1U) return false;
                const int integer = static_cast<int>(scroll);
                const int origin = 80 - static_cast<int>((scroll - integer) * 164.0f);
                const bool active = destination ? phase == 5 : phase == 3;
                const int y = destination ? 144 : 64;
                std::vector<unsigned> draw_order;
                for (unsigned i = integer; i < count && origin + (static_cast<int>(i) - integer) * 164 < 320; ++i)
                    draw_order.push_back(i);
                for (int i = integer - 1; i >= 0 && origin + (i - integer + 1) * 164 > 0; --i)
                    draw_order.push_back(i);
                for (unsigned i : draw_order) {
                    const int x = origin + (static_cast<int>(i) - integer) * 164;
                    const auto card = array + i * 16U;
                    const auto type = reader.value(card, 1), controller = reader.value(card + 6, 1);
                    std::optional<std::string> heading, label;
                    std::optional<unsigned> balloons;
                    bool draws_save_symbols = false;
                    bool adventure_two = false;
                    if (type == 1) {
                        if (controller > 2) return false;
                        heading = menu_text(119);
                        const auto settings = reader.value(address(0x80126530, 0x80126AD0) + controller * 4U);
                        if (!reader.range(settings, 0x54, 4) || reader.value(settings + 0x4B, 1) > 1) return false;
                        const bool blank = reader.value(settings + 0x4B, 1) != 0;
                        label = blank ?
                            reader.text(reader.value(address(0x800E03B0, 0x800E0930) + controller * 4U)) :
                            filename(reader.value(settings + 0x50), true);
                        if (!blank) {
                            draws_save_symbols = true;
                            const auto pointer = reader.value(settings);
                            if (reader.range(pointer, 2, 2)) balloons = reader.value(pointer, 2);
                            else unqualified_sprite_field = true;
                            adventure_two = (reader.value(settings + 0x10) & 4U) != 0;
                        }
                    } else if (type == 2 || type == 10) {
                        heading = menu_text(119); label = menu_text(type == 2 ? 120 : 181);
                    } else if ((type >= 3 && type <= 6) || type == 8) {
                        if (controller > 3) return false;
                        heading = menu_text(86 + controller);
                        if (type == 3) {
                            draws_save_symbols = true;
                            balloons = reader.value(card + 2, 1);
                            adventure_two = reader.value(card + 3, 1) != 0;
                            label = filename(reader.value(card + 4, 2), false);
                            auto prefix = reader.text(reader.value(address(0x800E1E10, 0x800E2390)));
                            const auto ext = reader.value(card + 8);
                            if (!label || !prefix || prefix->size() > 10 || !reader.range(ext, 1)) return false;
                            const auto ch = reader.value(ext, 1);
                            if (ch < 32 || ch > 126) return false;
                            *label += *prefix + static_cast<char>(ch) + ")";
                        } else if (type == 4 || type == 6) label = reader.text(reader.value(card + 8));
                        else label = menu_text(type == 5 ? 121 : 122);
                    } else if (type == 7 || type == 9) label = menu_text(type == 7 ? 81 : 123);
                    else return false;
                    if (!label || (type != 7 && type != 9 && !heading)) return false;
                    std::vector<InkRect> later_ink;
                    auto label_visible = visible_text(*label, 1, x + (revision == rom::Revision::UsV80 ? 80 : 79),
                        y + 47, false, false, &later_ink);
                    if (!label_visible) return false;
                    if (!visible_text(*label, 1, x + (revision == rom::Revision::UsV80 ? 81 : 80),
                        y + 48, false, false, &later_ink)) return false;
                    const auto name_ink = later_ink;
                    if (heading) {
                        auto heading_visible = visible_text(*heading, 0, x + 79, y - 1, true, false, &later_ink);
                        if (!heading_visible) return false;
                        if (!visible_text(*heading, 0, x + 81, y + 1, true, false, &later_ink)) return false;
                    }
                    // texrect_draw paints this icon after the count/Adventure
                    // sprites. Its copied texture dimensions may overlap them.
                    const unsigned texture_kind = type == 1 || type == 3 || type == 10 ? 0 : type == 2 || type == 4 ? 1 :
                        type == 5 || type == 9 ? 2 : type == 6 ? 3 : type == 8 ? 4 : 5;
                    const auto draw_texture = address(0x800DFC10, 0x800E0190) + texture_kind * 16U;
                    const auto texture = reader.value(draw_texture);
                    if (reader.value(draw_texture + 8) != 0) return false;
                    if (texture) {
                        if (!reader.range(texture, 32, 4)) return false;
                        const int left = x + 60 + static_cast<std::int16_t>(reader.value(draw_texture + 4, 2));
                        const int top = y + 6 + static_cast<std::int16_t>(reader.value(draw_texture + 6, 2));
                        const auto width = reader.value(texture, 1), height = reader.value(texture + 1, 1);
                        if (!width || !height) return false;
                        later_ink.push_back({left, top, left + static_cast<int>(width), top + static_cast<int>(height)});
                    }
                    const int index = static_cast<int>(i + (destination ? 80U : 0U));
                    const bool highlighted = active && i == selected;
                    drawn_cards.push_back({index, x, y, active, highlighted, draws_save_symbols, adventure_two,
                        balloons, {}, std::move(later_ink), heading, *label, name_ink});
                }
                return true;
            };
            if (phase >= 3 && !cards(false)) return unavailable;
            if (phase >= 5 && !cards(true)) return unavailable;
            for (unsigned i = 0; i < drawn_cards.size(); ++i) {
                auto& card = drawn_cards[i];
                std::vector<InkRect> later_ink;
                for (unsigned j = i + 1; j < drawn_cards.size(); ++j) {
                    const auto& later = drawn_cards[j];
                    later_ink.push_back({later.x, later.y, later.x + 160, later.y + 64});
                    later_ink.insert(later_ink.end(), later.later_ink.begin(), later.later_ink.end());
                }
                // Text can extend beyond its card and be covered by a later
                // background. Emit only glyphs still visible in draw order.
                auto label = visible_text(card.label, 1, card.x + (revision == rom::Revision::UsV80 ? 80 : 79),
                    card.y + 47, false, false, nullptr, &later_ink);
                if (!label) return unavailable;
                if (card.heading) {
                    auto heading_cover = later_ink;
                    heading_cover.insert(heading_cover.end(), card.name_ink.begin(), card.name_ink.end());
                    auto heading = visible_text(*card.heading, 0, card.x + 79, card.y - 1, true, false, nullptr, &heading_cover);
                    if (!heading) return unavailable;
                    card.printed = *heading;
                }
                if (!label->empty()) { if (!card.printed.empty()) card.printed += ": "; card.printed += *label; }
                later_ink.insert(later_ink.end(), card.later_ink.begin(), card.later_ink.end());
                if (card.symbols) {
                    const auto append = [&](const std::string& field) { if (!card.printed.empty()) card.printed += ", "; card.printed += field; };
                    if (card.balloons && *card.balloons <= 99) {
                        auto first = sprite_visible(2, *card.balloons / 10U, card.x + 27, card.y + 49, later_ink);
                        auto second = sprite_visible(2, *card.balloons % 10U, card.x + 35, card.y + 49, later_ink);
                        if (!first || !second) unqualified_sprite_field = true;
                        else if (*first && *second) append(std::to_string(*card.balloons) + " balloons");
                        else if (*first || *second) append("visible balloon count fragment \"" +
                            (*first ? std::to_string(*card.balloons / 10U) : "...") +
                            (*second ? std::to_string(*card.balloons % 10U) : "...") + "\"");
                    } else unqualified_sprite_field = true;
                    auto adventure = sprite_visible(card.adventure_two ? 12 : 11, 0, card.x + 32, card.y + 24, later_ink);
                    if (!adventure) unqualified_sprite_field = true;
                    else if (*adventure) append(card.adventure_two ? "Adventure Two" : "Adventure One");
                }
                if (card.printed.empty()) continue;
                result.menu_entries.push_back({card.index, card.printed, card.active, card.highlighted});
                if (card.highlighted) { result.selection = card.index; result.selection_label = card.printed; }
            }
            std::sort(result.menu_entries.begin(), result.menu_entries.end(), [](const MenuEntry& a, const MenuEntry& b) { return a.index < b.index; });
            if (unqualified_sprite_field) {
                if (!result.menu_detail.empty()) result.menu_detail += ". ";
                result.menu_detail += "Some balloon counts or Adventure icons are unavailable";
            }
            if (phase == 6) {
                result.selection = 160; result.selection_label = "OK?";
                result.menu_entries.push_back({160, "OK?", true, true});
            }
        } else if (menu == 19) {
            // menu_game_select_init/render: gMenuStage is the last option,
            // strings live at MenuElement.t +0x14, stride 0x20, odd entries.
            const auto last = reader.value(addresses.MenuStage);
            const auto elements = reader.value(address(0x80126460, 0x80126A00));
            if ((last != 1 && last != 2) || cursor > last || !reader.range(elements, (last * 2U + 4U) * 32U, 4)) return unavailable;
            for (unsigned i = 0; i <= last; ++i)
                if (!add_text(reader.value(elements + (i * 2U + 3U) * 32U + 0x14), i, i == cursor)) return unavailable;
            result.kind = StateKind::GameSelectMenu; result.menu_name = "Game select";
        } else if (menu == 3) {
            // Stock roster whitelist. The voice IDs in each 14-byte entry are
            // the authored identity shown by the selected character portrait.
            const auto roster = reader.value(address(0x801263CC, 0x8012696C));
            const std::uint32_t rosters[] = {address(0x800DFDD0, 0x800E0350), address(0x800DFE40, 0x800E03C0),
                                            address(0x800DFEC0, 0x800E0440), address(0x800DFF40, 0x800E04C0)};
            const unsigned counts[] = {8, 9, 9, 10};
            unsigned count = 0;
            for (unsigned i = 0; i < 4; ++i) if (roster == rosters[i]) count = counts[i];
            unsigned selected = 0xFF;
            unsigned active = 0;
            unsigned status = 0;
            for (unsigned port = 0; port < 4; ++port) {
                const auto slot = reader.value(addresses.PlayersCharacterArray + port, 1);
                const auto participating = reader.value(addresses.ActivePlayersArray + port, 1);
                const auto player_status = reader.value(addresses.CharacterSelectStatus + port, 1);
                if (participating > 1 || player_status > 2 ||
                    (!participating && (slot != 0xFF || player_status != 0)) ||
                    (participating && slot == 0xFF)) return unavailable;
                if (!participating) continue;
                selected = slot; status = player_status; ++active;
            }
            const auto ready = reader.value(addresses.NumberOfReadyPlayers);
            if (!count || selected >= count || active != 1 || reader.value(addresses.NumberOfActivePlayers) != 1 ||
                ready > 1 || ready != (status != 0 ? 1U : 0U)) return unavailable;
            const char* names[] = {"Krunch", "Bumper", "Tiptup", "Conker", "Timber", "Banjo", "Drumstick", "Pipsy", "T.T.", "Diddy Kong"};
            for (unsigned i = 0; i < count; ++i) {
                const auto identity = reader.value(roster + i * 14U + 12U, 2);
                if (identity >= 10) return unavailable;
                result.menu_entries.push_back({static_cast<int>(i), names[identity], status == 0, i == selected});
            }
            result.selection = selected; result.selection_label = result.menu_entries[selected].label;
            result.kind = StateKind::CharacterMenu; result.menu_name = "Character select";
            // charselect_render_text runs before input and before CONFIRMED1
            // advances to READY2. Keep portrait semantics for that first-A
            // transient; the stable READY sample has rendered the visible OK?.
            if (status == 2) {
                result.menu_detail = "Selected character: " + result.selection_label;
                for (auto& entry : result.menu_entries) entry.selected = false;
                result.selection = count; result.selection_label = "OK?";
                result.menu_entries.push_back({static_cast<int>(count), "OK?", true, true});
            }
        } else if (menu == 6) {
            const auto copy = reader.value(address(0x80126484, 0x80126A24));
            const auto erase = reader.value(address(0x80126488, 0x80126A28));
            const auto initials = reader.value(address(0x80126CC0, 0x80127280));
            const auto confirm = reader.value(address(0x80126494, 0x80126A34));
            const auto stage = reader.value(addresses.MenuStage);
            const auto adventure = reader.value(address(0x800DF494, 0x800DFA14));
            if (copy > 1 || erase > 1 || initials > 1 || copy + erase + initials > 1 ||
                ((copy || erase) && confirm > (copy ? 2U : 1U)) || stage > 2 || adventure > 1) return unavailable;
            result.menu_name = "File select";
            if (initials) {
                if (!initials_entry()) return unavailable;
            } else {
                const auto selected = reader.value(copy && confirm == 1 ? address(0x80126490, 0x80126A30) :
                    copy || erase ? address(0x8012648C, 0x80126A2C) : address(0x800DF4CC, 0x800DFA4C));
                if (selected >= 3) return unavailable;
                for (unsigned i = 0; i < 3; ++i) {
                    const auto slot = address(0x801264A0, 0x80126A40) + i * 12U;
                    const auto started = reader.value(slot + 1, 1), second = reader.value(slot, 1);
                    if (started > 1 || second > 1) return unavailable;
                    auto name = reader.text(started ? slot + 4 : address(0x800E03B0, 0x800E0930) + i * 4U);
                    if (!name || name->size() > 3) return unavailable;
                    std::string label = "File " + std::to_string(i + 1) + ": " + *name;
                    if (started) label += ", " + std::to_string(reader.value(slot + 2, 2)) + " balloons";
                    else label += ", new game";
                    const bool enabled = copy ? (confirm == 0 ? started != 0 : started == 0) :
                                         erase ? started != 0 : !started || second == adventure;
                    const bool highlighted = (copy || erase || stage == 0) && i == selected;
                    result.menu_entries.push_back({static_cast<int>(i), label, enabled, highlighted});
                    if (highlighted) { result.selection = i; result.selection_label = label; }
                }
                if (copy || erase) {
                    auto prompt = (copy && confirm < 2) ? menu_text(confirm == 0 ? 77 : 78) :
                                  (erase && confirm == 0) ? menu_text(79) : std::optional<std::string>{"OK?"};
                    if (!prompt) return unavailable;
                    result.menu_name = *prompt;
                    if ((copy && confirm == 2) || (erase && confirm == 1)) {
                        result.menu_detail = result.selection_label;
                        result.selection = 3; result.selection_label = "OK?";
                        for (auto& entry : result.menu_entries) entry.selected = false;
                        result.menu_entries.push_back({3, "OK?", true, true});
                    }
                } else {
                    for (unsigned i = 0; i < 2; ++i) {
                        auto label = menu_text(80 + i);
                        if (!label) return unavailable;
                        const bool highlighted = stage == i + 1;
                        result.menu_entries.push_back({static_cast<int>(3 + i), *label, true, highlighted});
                        if (highlighted) { result.selection = 3 + i; result.selection_label = *label; }
                    }
                }
            }
            result.kind = StateKind::FileSelectMenu;
        } else if (menu == 5) {
            // Adventure's selected door is Settings.unk4C->mapID. The preview
            // index starts at zero and this stock menu never changes it.
            if (reader.value(address(0x800DF4C4, 0x800DFA44)) != 0 || reader.value(addresses.NumberOfActivePlayers) != 1) return unavailable;
            const auto settings = reader.value(address(0x80123510, 0x80123A90));
            if (!reader.range(settings, 0x50, 4)) return unavailable;
            const auto door = reader.value(settings + 0x4C);
            if (!reader.range(door, 3, 4)) return unavailable;
            const auto id = reader.value(door + 2, 1);
            auto name = level_name(id); if (!name) return unavailable;
            const auto stage = reader.value(addresses.MenuStage);
            // The negative preview stage returns immediately; it has no setup GUI.
            if (stage > 1) return unavailable;
            const auto table = reader.value(address(0x8012117C, 0x801216FC));
            const auto courses = reader.value(settings + 4);
            if (!reader.range(table, (id + 1U) * 6U, 4) || !reader.range(courses, (id + 1U) * 4U, 4)) return unavailable;
            const auto race_type = reader.value(table + id * 6U + 1U, 1);
            const auto cleared = reader.value(courses + id * 4U) & 6U;
            const auto world = reader.value(settings + 0x48, 1);
            if (world > 5) return unavailable;
            const bool silver = reader.value(address(0x8011AEF4, 0x8011B474), 1) == 0 && cleared == 2 &&
                (world == 5 || (reader.value(settings + 12, 2) & (1U << world)));
            result.kind = StateKind::TrackSelectMenu; result.menu_name = "Adventure track"; result.menu_detail = *name;
            if (race_type & 64U) {
                // Actual challenge instructions are supplied by the open text
                // window above. Do not infer their content from race rules.
                return unavailable;
            } else if (silver) {
                auto label = menu_text(191); // v80 replacement for CHALLENGE.
                if (revision == rom::Revision::UsV77) label = menu_text(13);
                auto coins = menu_text(11);
                if (!label || !coins) return unavailable;
                result.selection = 0; result.selection_label = *coins + " " + *label;
                result.menu_entries.push_back({0, result.selection_label, true, true});
                result.menu_detail += ". Press A or Start to race; B to return.";
            } else if (stage == 1) {
                result.selection = 0; result.selection_label = "OK?";
                result.menu_entries.push_back({0, "OK?", true, true});
            } else {
                const auto vehicles = reader.value(table + id * 6U + 2U, 1);
                const auto mask = vehicles ? (vehicles >> 4U) & 7U : 1U;
                const auto selected = reader.value(addresses.PlayerSelectVehicle, 1);
                if (selected >= 3 || !(mask & (1U << selected))) return unavailable;
                const char* names[] = {"Car", "Hovercraft", "Plane"};
                result.menu_name = "Vehicle"; result.selection = selected; result.selection_label = names[selected];
                for (unsigned i = 0; i < 3; ++i) if (mask & (1U << i))
                    result.menu_entries.push_back({static_cast<int>(i), names[i], i == selected || cleared == 6, i == selected});
            }
        } else if (menu == 20) {
            // trophyround_render draws only this current world/round/course;
            // there is no selectable track list or future-round preview here.
            const auto world = reader.value(addresses.TrophyRaceWorldId);
            const auto round = reader.value(address(0x800E0FEC, 0x800E156C));
            const auto worlds = reader.value(address(0x80121174, 0x801216F4));
            // menu_init sets this to1; trophy's loop clears it to0. Other
            // menus use countdown values, but they are not trophy-intro states.
            const auto lockout = reader.value(address(0x801263C4, 0x80126964));
            const auto tv = reader.value(0x80000300);
            if (reader.value(addresses.NumberOfActivePlayers) != 1 || world == 0 || world > 5 ||
                round > 3 || worlds <= world || worlds > 8 || lockout > 1 || tv == 0 || tv > 2 ||
                reader.value(addresses.LevelLoadTimer, 2) != 0 ||
                reader.value(address(0x800E31AC, 0x800E373C)) != 0 ||
                reader.value(address(0x8012A7F0, 0x8012ADB0)) != 0) return unavailable;
            const auto framebuffer = reader.value(address(0x801262C8, 0x80126868));
            if (framebuffer > 1 || reader.value(address(0x801262B0, 0x80126850) + framebuffer * 4U) != 320 ||
                reader.value(address(0x801262B8, 0x80126858) + framebuffer * 4U) != 240) return unavailable;
            const auto section = reader.value(address(0x8011AD6C, 0x8011B2EC));
            const auto offsets = reader.value(address(0x8011AD70, 0x8011B2F0));
            const auto count = reader.value(address(0x8011ADA0, 0x8011B320));
            if (count <= 28 || count > 128 || !reader.range(offsets, count * 4U, 4)) return unavailable;
            const auto misc = [&](unsigned index, unsigned required) -> std::optional<std::uint32_t> {
                // get_misc_asset indexes s32 words, not byte offsets. Adjacent
                // offsets bound each asset without reading another page's names.
                const auto first = reader.value(offsets + index * 4U), next = reader.value(offsets + (index + 1U) * 4U);
                if (next <= first || first > kMemorySize / 4U || next - first > kMemorySize / 4U ||
                    (next - first) * 4U < required || !reader.range(section, next * 4ULL, 4)) return std::nullopt;
                return section + first * 4U;
            };
            const auto tracks = misc(26, 30), hubs = misc(27, worlds);
            if (!tracks || !hubs) return unavailable;
            const auto course_id = reader.value(*tracks + (world - 1U) * 6U + round, 1);
            const auto hub_id = reader.value(*hubs + world, 1);
            if (course_id >= 128 || hub_id >= 128) return unavailable; // signed stock IDs; -1 is absent.
            auto world_name = level_name(hub_id), course_name = level_name(course_id);
            auto title = menu_text(70), round_name = menu_text(138 + round);
            if (!world_name || !course_name || !title || !round_name) return unavailable;
            const std::string labels[] = {*world_name, *title, *round_name, *course_name};
            const int y[] = {32, 64, 176, 208};
            const auto fonts = reader.value(address(0x8012A7E4, 0x8012ADA4));
            std::vector<FontGlyph> ink[4];
            for (unsigned i = 0; i < 4; ++i) {
                auto glyphs = font_glyphs(reader, fonts, 2, labels[i], 160, y[i], false);
                if (!glyphs) return unavailable;
                ink[i] = std::move(*glyphs);
            }
            result.kind = StateKind::TrackSelectMenu; result.menu_name = "Trophy race intro";
            for (unsigned i = 0; i < 4; ++i) {
                std::vector<FontRect> later;
                for (unsigned j = i + 1; j < 4; ++j) for (const auto& glyph : ink[j])
                    if (glyph.ch != ' ') later.push_back(glyph.ink);
                auto visible = visible_font_text(labels[i], ink[i], later);
                if (visible.text.empty()) continue;
                result.menu_entries.push_back({static_cast<int>(i), visible.text, true, false});
                if (i == 1) result.menu_name = visible.text;
                else {
                    if (!result.menu_detail.empty()) result.menu_detail += ". ";
                    result.menu_detail += visible.text;
                }
            }
            // Native help describes accepted stock input. It is not printed
            // trophy text, a guest menu option, or an activation implementation.
            const std::string help = "Press A or Start to continue";
            result.menu_entries.push_back({4, help, lockout == 0, lockout == 0});
            if (lockout == 0) { result.selection = 4; result.selection_label = help; }
        } else if (menu == 17) {
            const auto stage = reader.value(addresses.MenuStage);
            if (stage > 1 || reader.value(addresses.NumberOfActivePlayers) != 1) return unavailable;
            result.kind = StateKind::ResultsMenu; result.menu_name = "Rankings";
            if (stage == 0) {
                result.selection = 0; result.selection_label = "Rankings appearing";
                result.menu_entries.push_back({0, result.selection_label, false, true});
            } else {
                const auto count = reader.value(address(0x80126C14, 0x801271D4));
                const auto selected = reader.value(address(0x80126A68, 0x80127028));
                const auto suboption = reader.value(address(0x800E0988, 0x800E0F08));
                if (!count || count > 8 || suboption > 2) return unavailable;
                if (suboption) {
                    for (unsigned i = 0; i < 2; ++i) {
                        auto label = menu_text(i ? 85 : 134); if (!label) return unavailable;
                        result.menu_entries.push_back({static_cast<int>(i), *label, true, i + 1 == suboption});
                    }
                    result.selection = suboption - 1; result.selection_label = result.menu_entries[result.selection].label;
                    result.menu_name = "Confirm quit";
                } else if (!pointer_entries(address(0x80126BF0, 0x801271B0), count, selected)) return unavailable;
                const auto settings = reader.value(address(0x80123510, 0x80123A90));
                if (!reader.range(settings, 0x64, 4)) return unavailable;
                for (unsigned i = 0; i < 4; ++i) {
                    auto label = reader.text(reader.value(address(0x800E0BCC, 0x800E114C) + i * 4U));
                    if (!label) return unavailable;
                    if (!result.menu_detail.empty()) result.menu_detail += ", ";
                    result.menu_detail += *label + ": " + std::to_string(std::min(reader.value(settings + 0x5C + i * 2U, 2), 999U));
                }
            }
        } else if (menu == 15) {
            const auto type = reader.value(address(0x801267D0, 0x80126D70));
            const auto x = reader.value(address(0x801269C8, 0x80126F88));
            const auto y = reader.value(address(0x801269CC, 0x80126F8C));
            if (x >= 6 || y >= 5 || (x == 5 && y == 4) || type > 1) return unavailable;
            result.menu_name = "Track select";
            if (type == 0) {
                // Render names runs before track-view/input. These nine records
                // retain the submitted labels and offsets; the cursor and level
                // table can already refer to the next, still invisible, track.
                if (reader.value(0x80000300) == 0 || reader.value(0x80000300) > 2 ||
                    reader.value(address(0x80126474, 0x80126A14)) != 160 ||
                    reader.value(address(0x80126478, 0x80126A18)) != 120 ||
                    reader.value(address(0x8012647C, 0x80126A1C)) != 320 ||
                    reader.value(address(0x80126480, 0x80126A20)) != 240) return unavailable;
                const auto framebuffer = reader.value(address(0x801262C8, 0x80126868));
                if (framebuffer > 1 || reader.value(address(0x801262B0, 0x80126850) + framebuffer * 4) != 320 ||
                    reader.value(address(0x801262B8, 0x80126858) + framebuffer * 4) != 240) return unavailable;
                using Rect = FontRect;
                using Glyph = FontGlyph;
                struct Cell { std::string text; std::vector<Glyph> glyphs; std::vector<Rect> cover, heading;
                    int x, y; unsigned flags; bool locked, unknown = false; };
                std::vector<Cell> cells;
                const auto signed16 = [&](std::uint32_t p) { return static_cast<int>(static_cast<std::int16_t>(reader.value(p, 2))); };
                const auto font = reader.value(address(0x8012A7E4, 0x8012ADA4));
                if (!font_glyphs(reader, font, 2, "", 0, 0, false)) return unavailable;
                const auto glyphs = [&](const std::string& text, int cx, int cy, bool compact) {
                    return font_glyphs(reader, font, 2, text, cx, cy, compact);
                };
                // Rectangles are deliberately exclusion-only. Transparent pixels
                // and unused model vertices can withhold text, never reveal it.
                const auto textures = [&](std::uint32_t list, int cx, int cy, bool scaled, bool flip, std::vector<Rect>& out) {
                    const unsigned count = scaled ? 3 : 1;
                    if (!reader.range(list, (count + 1) * 8, 4)) return false;
                    for (unsigned i = 0; i < count; ++i) {
                        const auto p = list + i * 8, texture = reader.value(p);
                        if (!reader.range(texture, 2)) return false;
                        const int w = reader.value(texture, 1), h = reader.value(texture + 1, 1);
                        if (!w || !h) return false;
                        const float scale = scaled ? 1.25f : 1.0f;
                        const float ox = signed16(p + 4) * scale, oy = signed16(p + 6) * scale;
                        const float left = flip ? cx - ox - w * scale : cx + ox;
                        out.push_back({static_cast<int>(std::floor(left)) - 1, static_cast<int>(std::floor(cy + oy)) - 1,
                            static_cast<int>(std::ceil(left + w * scale)) + 1, static_cast<int>(std::ceil(cy + oy + h * scale)) + 1});
                    }
                    return reader.value(list + count * 8) == 0;
                };
                const auto border = [&](unsigned image, int cx, int cy, std::vector<Rect>& out) {
                    const auto images = reader.value(address(0x800DF75C, 0x800DFCDC));
                    if (!reader.range(images, 7 * 32, 4)) return false;
                    const auto transform = images + image * 32, asset = reader.value(transform + 6, 2);
                    if (asset != image + 1) return false;
                    const auto instance = reader.value(address(0x80126550, 0x80126AF0) + asset * 4);
                    if (!instance) return true; // Stock menu_element_render draws nothing.
                    const auto opacity = reader.value(address(0x800DF764, 0x800DFCE4));
                    if (opacity > 255) return false;
                    if (!opacity) return true;
                    if (reader.value(transform, 2) || reader.value(transform + 2, 2) || reader.value(transform + 4, 2) ||
                        reader.value(transform + 8) != std::bit_cast<std::uint32_t>(0.215f) || reader.value(transform + 20) != std::bit_cast<std::uint32_t>(-500.0f) ||
                        reader.value(address(0x800DF454, 0x800DF9D4)) != 0x3F800000) return false;
                    const auto ids = reader.value(address(0x800DF750, 0x800DFCD0)), count = reader.value(address(0x800DF754, 0x800DFCD4), 2);
                    if (count <= asset || count > 128 || !reader.range(ids, count * 2, 2) || (reader.value(ids + asset * 2, 2) & 0xC000)) return false;
                    const auto parent = reader.value(address(0x80120D70, 0x801212F0));
                    if (!reader.range(parent, 64, 4) || reader.value(address(0x80120D1C, 0x8012129C)) != 0) return false;
                    for (unsigned i = 0; i < 16; ++i) {
                        if (reader.value(parent + i * 4) != (i % 5 == 0 ? 0x3F800000U : 0U) ||
                            reader.value(address(0x800DD2B8, 0x800DD828) + i * 4) != (i == 0 || i == 5 ? 0x3F800000U : i == 15 ? 0x43200000U : 0U)) return false;
                    }
                    if (!reader.range(instance, 4, 4)) return false;
                    const auto model = reader.value(instance);
                    if (!reader.range(model, 0x26, 4)) return false;
                    const auto vertices = reader.value(model + 4), n = reader.value(model + 0x24, 2);
                    if (!n || n > 2048 || !reader.range(vertices, n * 10, 2)) return false;
                    int left = 100000, top = 100000, right = -100000, bottom = -100000;
                    for (unsigned i = 0; i < n; ++i) {
                        const float px = cx + signed16(vertices + i * 10) * 0.215f, py = cy - signed16(vertices + i * 10 + 2) * 0.215f;
                        left = std::min(left, static_cast<int>(std::floor(px)) - 1); right = std::max(right, static_cast<int>(std::ceil(px)) + 1);
                        top = std::min(top, static_cast<int>(std::floor(py)) - 1); bottom = std::max(bottom, static_cast<int>(std::ceil(py)) + 1);
                    }
                    out.push_back({left, top, right, bottom}); return true;
                };
                auto previous_hub = 0U;
                bool incomplete = false;
                const auto records = address(0x80126930, 0x80126EF0);
                for (unsigned i = 0; i < 9; ++i) {
                    const auto p = records + i * 16, visible = reader.value(p + 12, 1);
                    if (!visible) continue; // Unsubmitted records may contain stale pointers.
                    if (visible > 2) return unavailable;
                    const auto flags = reader.value(p + 14, 1), image = reader.value(p + 15, 1);
                    if ((flags & 0x70) || image < 4 || image > 6) return unavailable;
                    auto text = reader.text(reader.value(p + 4));
                    if (!text) return unavailable;
                    const int dx = signed16(p + 8), dy = signed16(p + 10), cx = 160 + dx, cy = 120 - dy;
                    auto ink = glyphs(*text, cx, cy + 88, false);
                    if (!ink) return unavailable;
                    Cell cell{*text, *ink, {}, {}, dx, dy, flags, reader.value(p + 4) == reader.value(address(0x800E0978, 0x800E0EF8))};
                    const auto hub = reader.value(p);
                    if (hub != previous_hub) {
                        auto heading = reader.text(hub); if (!heading) return unavailable;
                        // Snowflake's compact heading has no retained flag. Both
                        // possible bounds are safe exclusion supersets.
                        for (bool compact : {false, true}) for (int shadow : {0, 1}) {
                            auto hi = glyphs(*heading, 160 + shadow, cy - 88 + shadow * 3, compact);
                            if (!hi) return unavailable;
                            for (const auto& g : *hi) if (g.ch != ' ') cell.heading.push_back(g.ink);
                        }
                        previous_hub = hub;
                    }
                    if (reader.value(p + 13, 1) && cy - 60 < 240 && cy + 60 > 0) {
                        const auto list = visible == 1 ? address(0x800E05D4, 0x800E0B54) : address(0x800E05F4, 0x800E0B74);
                        bool known_scale = true;
                        if (flags & 0x80) {
                            // The copied viewport has already moved after names
                            // were submitted. trackmenu_track_view preserves its
                            // 160x120 size at delay0; use the record's old anchor,
                            // only after validating that copied size. Unsupported
                            // scaling cannot be used to admit any affected ink.
                            const auto viewport = address(0x800DD064, 0x800DD5D4);
                            const auto vx1 = static_cast<std::int32_t>(reader.value(viewport));
                            const auto vy1 = static_cast<std::int32_t>(reader.value(viewport + 4));
                            const auto vx2 = static_cast<std::int32_t>(reader.value(viewport + 8));
                            const auto vy2 = static_cast<std::int32_t>(reader.value(viewport + 12));
                            known_scale = static_cast<std::int64_t>(vx2) - vx1 == 160 &&
                                static_cast<std::int64_t>(vy2) - vy1 == 120;
                        }
                        if (!known_scale) cell.unknown = true;
                        else {
                            // s16 offsets and integer viewport coords truncate
                            // separately, so widen copied viewport draw gates.
                            const int half = (flags & 0x80) ? 81 : 80;
                            if (cx - half < 320 && cx > 0 && !textures(list, cx, cy, true, true, cell.cover)) cell.unknown = true;
                            if (cx < 320 && cx + half > 0 && !textures(list, cx, cy, true, false, cell.cover)) cell.unknown = true;
                        }
                    }
                    if (!border(image, cx, cy, cell.cover)) cell.unknown = true;
                    constexpr int offsets[4][2] = {{0,-62},{85,0},{0,62},{-85,0}};
                    for (unsigned j = 0; j < 4; ++j) if (flags & (1U << j)) {
                        const auto list = reader.value(address(0x800E0678, 0x800E0BF8) + j * 4);
                        for (int shift : {-1, 1}) if (!textures(list, cx + offsets[j][0] + shift, cy + offsets[j][1] + shift, false, false, cell.cover)) cell.unknown = true;
                    }
                    // Preserve renderer slot identity even when absent cells skip.
                    cell.flags |= i << 8;
                    cells.push_back(std::move(cell));
                }
                const auto real = [&](std::uint32_t p) { return std::bit_cast<float>(reader.value(p)); };
                const float sx = real(address(0x801269DC, 0x80126F9C)), sy = real(address(0x801269E4, 0x80126FA4));
                const float tx = real(address(0x801269E8, 0x80126FA8)), ty = real(address(0x801269EC, 0x80126FAC));
                if (!std::isfinite(sx) || !std::isfinite(sy) || !std::isfinite(tx) || !std::isfinite(ty) ||
                    std::abs(sx) > 2000 || std::abs(sy) > 1200 || std::abs(tx) > 2000 || std::abs(ty) > 1200) return unavailable;
                const auto centers = std::count_if(cells.begin(), cells.end(), [](const Cell& c) { return std::abs(c.x) <= 4 && std::abs(c.y) <= 4; });
                const bool stable = centers == 1 && tx == x * 320.0f && ty == -static_cast<float>(y) * 240 &&
                    std::abs(sx - tx) <= 4 && std::abs(sy - ty) <= 4;
                const bool matching_preview = reader.value(address(0x801269F4, 0x80126FB4)) == x &&
                    reader.value(address(0x801269F8, 0x80126FB8)) == y;
                for (unsigned i = 0; i < cells.size(); ++i) {
                    const auto& cell = cells[i];
                    bool unknown = cell.unknown;
                    std::vector<Rect> later = cell.cover;
                    for (unsigned j = i + 1; j < cells.size(); ++j) {
                        unknown |= cells[j].unknown;
                        later.insert(later.end(), cells[j].cover.begin(), cells[j].cover.end());
                        later.insert(later.end(), cells[j].heading.begin(), cells[j].heading.end());
                        for (const auto& g : cells[j].glyphs) if (g.ch != ' ') later.push_back(g.ink);
                    }
                    if (unknown) { incomplete = true; continue; }
                    auto visible = visible_font_text(cell.text, cell.glyphs, later);
                    auto& label = visible.text;
                    const bool complete = visible.complete;
                    if (label.empty()) continue;
                    const int index = 100 + (cell.flags >> 8);
                    // Locked cells draw the literal default '?' even while
                    // track_view retains the last unlocked preview. Their unique
                    // submitted center and settled cursor qualify this visible
                    // marker without borrowing that previous track's identity.
                    const bool locked_marker = cell.locked && complete && cell.text == "?";
                    const bool selected = stable && std::abs(cell.x) <= 4 && std::abs(cell.y) <= 4 &&
                        (locked_marker || (matching_preview && (cell.flags & 0x80)));
                    result.menu_entries.push_back({index, label, !cell.locked, selected});
                    if (selected) { result.selection = index; result.selection_label = label;
                        result.menu_detail = "Grid position: row " + std::to_string(y + 1) + ", column " + std::to_string(x + 1); }
                }
                if (incomplete) { if (!result.menu_detail.empty()) result.menu_detail += ". "; result.menu_detail += "Some track labels are unavailable."; }
            } else {
                const auto id = reader.value(address(0x801268E8, 0x80126EA8) + (y * 6U + x) * 2U, 2);
                auto name = level_name(id);
                if (!name) return unavailable;
                if (reader.value(addresses.NumberOfActivePlayers) != 1) return unavailable;
                result.menu_detail = *name;
                const auto stage = reader.value(addresses.MenuStage);
                if (stage == 0xFFFFFFFFU || (stage == 2 && x == 4 &&
                    (reader.value(address(0x80126448, 0x801269E8) + 4U) & 1U))) {
                    const auto selected = reader.value(address(0x800E0418, 0x800E0998));
                    if (selected > 1) return unavailable;
                    result.menu_name = "Adventure mode";
                    for (unsigned i = 0; i < 2; ++i) {
                        auto label = menu_text(146 + i); if (!label) return unavailable;
                        result.menu_entries.push_back({static_cast<int>(i), *label, true, i == selected});
                    }
                    result.selection = selected; result.selection_label = result.menu_entries[selected].label;
                } else if (x < 4 && stage == 0) {
                    const auto table = reader.value(address(0x8012117C, 0x801216FC));
                    if (!reader.range(table, (id + 1U) * 6U, 4)) return unavailable;
                    const auto flags = (reader.value(table + id * 6U + 2U, 1) >> 4U) & 7U;
                    const auto selected = reader.value(addresses.PlayerSelectVehicle, 1);
                    if (!flags || selected >= 3 || !(flags & (1U << selected))) return unavailable;
                    const auto settings = reader.value(address(0x80123510, 0x80123A90));
                    if (!reader.range(settings, 8, 4)) return unavailable;
                    const auto courses = reader.value(settings + 4);
                    if (!reader.range(courses, (id + 1U) * 4U, 4)) return unavailable;
                    const bool alternatives = (reader.value(courses + id * 4U) & 2U) != 0;
                    const char* names[] = {"Car", "Hovercraft", "Plane"};
                    result.menu_name = "Vehicle";
                    // Only authored available vehicle images appear in this view.
                    for (unsigned i = 0; i < 3; ++i) if (flags & (1U << i))
                        result.menu_entries.push_back({static_cast<int>(i), names[i], i == selected || alternatives, i == selected});
                    result.selection = selected; result.selection_label = names[selected];
                } else if (x < 4 && stage == 1) {
                    const auto selected = reader.value(address(0x800E0414, 0x800E0994));
                    if (selected > 1) return unavailable;
                    result.menu_name = "Time trial"; result.selection = selected;
                    result.menu_entries = {{0, "Off", true, selected == 0}, {1, "On", true, selected == 1}};
                    result.selection_label = result.menu_entries[selected].label;
                } else if (stage == 2 && x < 5) {
                    result.menu_name = "Start race"; result.selection = 0; result.selection_label = "OK?";
                    result.menu_entries.push_back({0, "OK?", true, true});
                } else return unavailable;
            }
            result.kind = StateKind::TrackSelectMenu;
        }
        return result;
    }
    if (mode == 0 && reader.value(addresses.PostRaceViewport, 1) != 0 &&
        reader.value(addresses.LevelLoadTimer, 2) == 0 && reader.value(addresses.MenuDelay) == 0) {
        const auto stage = reader.value(addresses.MenuStage);
        result.kind = StateKind::ResultsMenu; result.menu_name = "Race results";
        if (stage == 6) {
            // Gate on the actual open option window, not stale option globals.
            auto lines = live_window(7);
            if (!lines || lines->empty()) return unavailable;
            if (reader.value(address(0x80126C1C, 0x801271DC)) || reader.value(address(0x80126A98, 0x80127058))) {
                dialogue(*lines, "Race results message"); return result;
            }
            const auto suboption = reader.value(address(0x800E0988, 0x800E0F08));
            const auto count = reader.value(address(0x80126C14, 0x801271D4));
            const auto selected = reader.value(address(0x80126A68, 0x80127028));
            if (!count || count > 8 || suboption > 2) return unavailable;
            if (suboption) {
                for (unsigned i = 0; i < 2; ++i) {
                    auto label = menu_text(i ? 85 : 134); if (!label) return unavailable;
                    result.menu_entries.push_back({static_cast<int>(i), *label, true, i + 1 == suboption});
                }
                result.selection = suboption - 1; result.selection_label = result.menu_entries[result.selection].label;
                result.menu_name = "Confirm quit";
            } else if (!pointer_entries(address(0x80126BF0, 0x801271B0), count, selected)) return unavailable;
            // Each published choice must also have a current rendered text node.
            for (const auto& entry : result.menu_entries)
                if (std::none_of(lines->begin(), lines->end(), [&](const LiveText& line) { return line.text == entry.label; })) return unavailable;
        } else if (stage == 4) {
            if (!initials_entry()) return unavailable;
        } else if (stage == 2 || stage == 3 || stage == 5) {
            // During sliding entry/exit, portions of the result row may be off
            // screen. Publish its values only in the source's stationary HOLD.
            if (reader.value(address(0x800DF794, 0x800DFD14)) != 1) return unavailable;
            const auto elements = stage == 2 ? address(0x800E0BEC, 0x800E116C) :
                                  stage == 3 ? address(0x800E0CEC, 0x800E126C) : address(0x800E0E4C, 0x800E13CC);
            if (reader.value(address(0x800DF798, 0x800DFD18)) != elements) return unavailable;
            const char* racer_names[] = {"Krunch", "Diddy Kong", "Drumstick", "Bumper", "Banjo", "Conker", "Tiptup", "T.T.", "Pipsy", "Timber"};
            const unsigned count = stage == 2 ? 7 : stage == 3 ? 10 : 8;
            std::string summary;
            for (unsigned i = 0; i < count; ++i) {
                const auto entry = elements + i * 32U;
                const auto pointer = reader.value(entry + 20);
                if (!pointer) break; // draw_menu_elements' actual terminator.
                if (!reader.value(entry + 16, 1) || pointer == address(0x80126850, 0x80126DF0)) continue;
                const auto type = reader.value(entry + 19, 1);
                std::string label;
                if (type == 0) {
                    auto text = reader.text(pointer, true); if (!text) return unavailable;
                    label = *text;
                    if (summary.find(label) != std::string::npos) continue; // drop shadow duplicate.
                } else if (type == 1) {
                    if (!reader.range(pointer, 2, 2)) return unavailable;
                    auto frames = reader.value(pointer, 2);
                    const auto refresh = reader.value(address(0x80126170, 0x80126710));
                    if (refresh == 50) frames = static_cast<unsigned>(static_cast<float>(frames) * 1.2f);
                    else if (refresh != 60) return unavailable;
                    const auto minutes = frames / 3600U, seconds = frames / 60U % 60U, hundredths = frames % 60U * 100U / 60U;
                    label = std::to_string(minutes) + ":" + (seconds < 10 ? "0" : "") + std::to_string(seconds) + "." +
                        (hundredths < 10 ? "0" : "") + std::to_string(hundredths);
                    if (stage == 2 && i >= 3 && i <= 5) label = "Lap " + std::to_string(i - 2U) + ": " + label;
                } else if (type == 3) {
                    for (unsigned character = 0; character < 10; ++character)
                        if (pointer == reader.value(address(0x800E0AF0, 0x800E1070) + character * 4U)) label = racer_names[character];
                    if (label.empty()) return unavailable;
                    if (stage == 3 && i < 8) label = std::to_string(8U - i) + ": " + label;
                } else return unavailable;
                if (!label.empty()) { if (!summary.empty()) summary += ", "; summary += label; }
            }
            if (summary.empty()) return unavailable;
            result.selection = 0; result.selection_label = summary;
            result.menu_detail = "Press A or Start to continue.";
            result.menu_entries.push_back({0, summary, false, true});
        } else return unavailable;
        return result;
    }
    if (mode != 0 || reader.value(addresses.LevelLoadTimer, 2) != 0) return result;
    const auto paused = reader.value(addresses.IsPaused, 1);
    if (paused > 1) return result;
    // mode_game/menu_pause render pause options in hubs and challenges too.
    // gIsInRace is FALSE there: only DEFAULT/BOSS levels set it in load_level.
    // Restrict race HUD fields after decoding this gameplay pause screen.
    if (paused == 1) {
        const auto cap = reader.value(address(0x800E0984, 0x800E0F04));
        const auto selected = reader.value(address(0x80126A68, 0x80127028));
        const auto suboption = reader.value(address(0x800E0988, 0x800E0F08));
        if (suboption > 2 || cap == 0 || cap > 8 || selected >= cap) return result;
        if (suboption) {
            for (unsigned i = 0; i < 2; ++i) {
                auto label = menu_text(i == 0 ? 134 : 85); if (!label) return unavailable;
                result.menu_entries.push_back({static_cast<int>(i), *label, true, i + 1 == suboption});
            }
            result.selection = suboption - 1; result.selection_label = result.menu_entries[result.selection].label;
            result.menu_name = "Confirm quit";
        } else {
            if (!pointer_entries(address(0x80126A40, 0x80127000), cap, selected)) return unavailable;
            result.menu_name = "Pause options";
        }
        result.kind = StateKind::PauseMenu; result.paused = true;
        return result;
    }
    const auto countdown = reader.value(addresses.RaceStartTimer);
    if (reader.value(addresses.IsInRace) != 1 || countdown > 100) return result;
    result.kind = StateKind::Race;
    result.paused = paused != 0;
    result.countdown = static_cast<int>(countdown);

    // Restrict racer details to an authored ordinary race and controller 1.
    // Bosses, absent/finished players, malformed counts or pointers remain a
    // valid race-status sample with explicitly unavailable racer details.
    const auto count = reader.value(addresses.NumberOfRacers);
    const auto header = reader.value(addresses.CurrentLevelHeader);
    const auto ports = reader.value(addresses.RacersByPort);
    // game_ui.c render_hud gates: HUD enabled, race HUD revealed, ordinary
    // single-player view, no cutscene, no header HUD-suppression bit.
    // render_hud routes gIsTimeTrial (is_in_time_trial), not the setup toggle
    // gTimeTrialEnabled, to hud_main_time_trial, which has no race position.
    // Withhold the aggregate until each field's visible layout is modeled.
    if (reader.value(address(0x8011AEF5, 0x8011B475), 1) != 0 ||
        reader.value(address(0x80126CD2, 0x80127292), 1) != 0 ||
        reader.value(address(0x80126D34, 0x801272F4), 1) != 1 ||
        reader.value(addresses.NumberOfActivePlayers) != 1 ||
        reader.value(address(0x800DF4C0, 0x800DFA40)) != 0 ||
        reader.value(address(0x8012718A, 0x8012774A), 1) != 0 ||
        reader.value(addresses.CutsceneCameraActive, 1) != 0 ||
        count == 0 || count > 10 || !reader.range(header, 0xBD, 4) ||
        (reader.value(header + 0xBC, 1) & 2) != 0 ||
        reader.value(header + 0x4C, 1) != 0 || !reader.range(ports, count * 4U, 4)) return result;
    const auto laps = reader.value(header + 0x4B, 1);
    const auto object = reader.value(ports);
    if (laps == 0 || laps > 5 || !reader.range(object, 0x68, 4)) return result;
    const auto racer = reader.value(object + 0x64);
    if (!reader.range(racer, 0x1D9, 4) || reader.value(racer, 2) != 0 ||
        reader.value(racer + 0x1D8, 1) != 0) return result;
    const auto position = reader.value(racer + 0x1AE, 2);
    const auto lap = reader.value(racer + 0x194, 1);
    const auto bananas = reader.value(racer + 0x185, 1);
    if (position == 0 || position > count || lap > laps || bananas > 10) return result;
    // HUD uses countLap, NOT lap (game_ui.c:2255-2280); it caps the final lap.
    result.racer = RacerStatus{static_cast<int>(position), static_cast<int>(std::min(lap + 1U, laps)),
                               static_cast<int>(laps), static_cast<int>(bananas), std::nullopt};
    // hud_render_player reads gEventCountdown >> 1. RaceStartTimer is a
    // different cached value and cannot identify the authored Ready/GO art.
    const auto start_prompt = [&]() -> RaceStartPrompt {
        // Track setup hands off with gMenuDelay > 40 and never clears it.
        // That menu-only counter is not a HUD visibility gate in a race.
        const auto event = reader.value(address(0x8011ADB0, 0x8011B330));
        if (event > 100 || reader.value(addresses.TracksMode) != 1 ||
            reader.value(address(0x80126D08, 0x801272C8)) != 0 || reader.value(addresses.HudNumPlayers) != 0 ||
            reader.value(address(0x80126D24, 0x801272E4)) != 0 || reader.value(address(0x80126D28, 0x801272E8)) != 0 ||
            (reader.value(address(0x80126CD3, 0x80127293), 1) & 1U) ||
            reader.value(address(0x80120D15, 0x80121295), 1) != 0 ||
            reader.value(address(0x800E31AC, 0x800E373C)) != 0 ||
            reader.value(address(0x800DD384, 0x800DD8F4)) == 0xFFFFFFFFU ||
            reader.value(addresses.PostRaceViewport, 1) != 0 ||
            reader.value(0x80000300) == 0 || reader.value(0x80000300) > 2) return RaceStartPrompt::None;
        const auto framebuffer = reader.value(address(0x801262C8, 0x80126868));
        if (framebuffer > 1 || reader.value(address(0x801262B0, 0x80126850) + framebuffer * 4U) != 320 ||
            reader.value(address(0x801262B8, 0x80126858) + framebuffer * 4U) != 240) return RaceStartPrompt::None;
        for (unsigned i = 0; i < 16; ++i) {
            const auto expected = i == 0 || i == 5 ? 0x3F800000U : i == 15 ? 0x43200000U : 0U;
            if (reader.value(address(0x800DD2B8, 0x800DD828) + i * 4U) != expected) return RaceStartPrompt::None;
        }
        const auto windows = reader.value(address(0x8012A7E8, 0x8012ADA8));
        if (windows) {
            if (!reader.range(windows, 8U * 40U, 4)) return RaceStartPrompt::None;
            for (unsigned i = 0; i < 8; ++i) {
                // Slot4's only stock writer is the overworld level-name banner;
                // its objects are freed in Tracks mode. Its flags are cleared
                // after rendering, so they alone are not visibility evidence.
                if (i != 4 && (reader.value(windows + i * 40U + 0x1E, 2) & 0x8000U)) return RaceStartPrompt::None;
            }
        }
        const bool ready = (event >> 1U) > 0;
        const unsigned index = ready ? 13 : 12, sprite_id = ready ? 22 : 7, tiles = ready ? 3 : 2;
        const auto hud = reader.value(addresses.CurrentHud);
        if (hud != reader.value(addresses.PlayerHud) || !reader.range(hud, 14U * 32U, 4)) return RaceStartPrompt::None;
        const auto element = hud + index * 32U;
        if (reader.value(element, 2) || reader.value(element + 2, 2) || reader.value(element + 4, 2) ||
            reader.value(element + 6, 2) != sprite_id || reader.value(element + 8) != 0x3F800000U ||
            reader.value(element + 12) != 0 || reader.value(element + 16) != std::bit_cast<std::uint32_t>(30.0f) ||
            reader.value(element + 20) != 0 || reader.value(element + 24, 2) != 0) return RaceStartPrompt::None;
        // GO is drawn before its timer/position mutate. Only this centered hold
        // has a source-proven unchanged transform at submission; omit the tail.
        const auto timer = reader.value(element + 26, 1);
        if (!ready && (timer == 0 || timer >= 60)) return RaceStartPrompt::None;
        const auto ids = reader.value(address(0x80126CF0, 0x801272B0));
        const auto assets = reader.value(address(0x80126CF4, 0x801272B4));
        const auto asset_count = reader.value(address(0x80126CF8, 0x801272B8));
        if (asset_count <= sprite_id || asset_count > 128 || !reader.range(ids, asset_count * 2U, 2) ||
            !reader.range(assets, asset_count * 4U, 4) ||
            reader.value(ids + sprite_id * 2U, 2) != (ready ? 0x803FU : 0x8040U)) return RaceStartPrompt::None;
        const auto sprite = reader.value(assets + sprite_id * 4U);
        // tex_load_sprite's single-frame allocation has a fixed bounded layout:
        // aligned Sprite/frame table, tile triangles, frame commands, vertices,
        // then texture pointers. Qualify the actual links, not plausible bounds.
        const auto triangles = sprite + 0x20U, list = triangles + tiles * 32U;
        const auto vertices = list + tiles * 32U + 8U, textures = vertices + tiles * 40U;
        if (!reader.range(sprite, 32U + tiles * 108U + 8U, 16) ||
            reader.value(sprite, 2) != 1 || reader.value(sprite + 2, 2) != tiles ||
            !reader.value(sprite + 4, 2) || (reader.value(sprite + 6, 2) & 10U) ||
            reader.value(sprite + 8) != textures || reader.value(sprite + 12) != list) return RaceStartPrompt::None;
        // Both stock US assets were audited: each retained texture header and
        // generated quad must match the authored tile dimensions and anchors.
        constexpr int go[2][4] = {{64,31,18,6}, {52,6,23,36}};
        constexpr int get_ready[3][4] = {{160,12,20,5}, {160,12,17,16}, {152,12,17,27}};
        const int anchor_x = ready ? 97 : 49, anchor_y = ready ? 22 : 24;
        auto command = list;
        const auto dma = [&](std::uint32_t opcode, std::uint32_t pointer) {
            return reader.range(pointer, 1) && reader.value(command) == opcode &&
                reader.value(command + 4) == (pointer & 0x1FFFFFFFU);
        };
        for (unsigned i = 0; i < tiles; ++i) {
            const auto& tile = ready ? get_ready[i] : go[i];
            const auto texture = reader.value(textures + i * 4U);
            // Texture counts retain the asset's byte order: load_texture
            // reads numOfTextures >> 8, so one texture is serialized 0x0100.
            if (!reader.range(texture, 0x20, 4) || reader.value(texture, 1) != static_cast<unsigned>(tile[0]) ||
                reader.value(texture + 1, 1) != static_cast<unsigned>(tile[1]) ||
                reader.value(texture + 3, 1) != static_cast<unsigned>(tile[2]) ||
                reader.value(texture + 4, 1) != static_cast<unsigned>(tile[3]) || reader.value(texture + 0x12, 2) != 0x0100U) return RaceStartPrompt::None;
            const auto texture_commands = reader.value(texture + 10, 2), texture_list = reader.value(texture + 12);
            if (!texture_commands || texture_commands > 64 || !reader.range(texture_list, texture_commands * 8U, 4) ||
                !dma(0x07000000U | (texture_commands << 16U) | (texture_commands * 8U), texture_list)) return RaceStartPrompt::None;
            command += 8;
            if (i == 0) {
                const unsigned n = tiles * 4U;
                if (!dma(0x04000000U | ((((n - 1U) << 3U) | 1U) << 16U) | (n * 18U + 8U), vertices)) return RaceStartPrompt::None;
                command += 8;
            }
            if (!dma(0x05110020U, triangles + i * 32U)) return RaceStartPrompt::None;
            command += 8;
            const unsigned base = i * 4U;
            const unsigned indices[] = {base + 3, base + 2, base + 1, base + 4, base + 3, base + 1};
            for (unsigned j = 0; j < 6; ++j)
                if (reader.value(triangles + i * 32U + (j < 3 ? 1U + j : 16U + j - 2U), 1) != indices[j]) return RaceStartPrompt::None;
            const unsigned u = (tile[0] - 1) << 5U, v = (tile[1] - 1) << 5U;
            const unsigned uv[2][6] = {{u,v,u,0,1,0}, {1,v,u,v,1,0}};
            for (unsigned t = 0; t < 2; ++t) {
                if (reader.value(triangles + i * 32U + t * 16U, 1) != 0x40U) return RaceStartPrompt::None;
                for (unsigned j = 0; j < 6; ++j)
                    if (reader.value(triangles + i * 32U + t * 16U + 4U + j * 2U, 2) != uv[t][j]) return RaceStartPrompt::None;
            }
            const int left = tile[2] - anchor_x, right = left + tile[0] - 1;
            const int top = anchor_y - tile[3] - 1, bottom = anchor_y - tile[3] - tile[1];
            const int x[4] = {left, right, right, left}, y[4] = {top, top, bottom, bottom};
            for (unsigned j = 0; j < 4; ++j) {
                const auto vertex = vertices + (i * 4U + j) * 10U;
                if (static_cast<std::int16_t>(reader.value(vertex, 2)) != x[j] ||
                    static_cast<std::int16_t>(reader.value(vertex + 2, 2)) != y[j] || reader.value(vertex + 4, 2) != 0 ||
                    reader.value(vertex + 6) != 0xFFFFFFFFU) return RaceStartPrompt::None;
            }
        }
        if (reader.value(command) != 0xE7000000U || reader.value(command + 4) != 0 ||
            reader.value(command + 8) != 0xB8000000U || reader.value(command + 12) != 0) return RaceStartPrompt::None;
        return ready ? RaceStartPrompt::GetReady : RaceStartPrompt::Go;
    };
    result.race_start_prompt = start_prompt();
    const auto quantity = reader.value(racer + 0x173, 1);
    const auto type = reader.value(racer + 0x172, 1);
    const auto level = reader.value(racer + 0x174, 1);
    if (quantity == 0) result.racer->held_item = HeldItem{};
    else if (quantity <= 10 && type < 5 && level < 3)
        result.racer->held_item = HeldItem{static_cast<ItemCategory>(type + 1), static_cast<int>(level + 1), static_cast<int>(quantity)};
    return result;
}

bool observation_demanded() {
    return (demand_epoch.load(std::memory_order_acquire) & 1U) != 0;
}
void set_observation_demand(bool enabled) {
    if (observation_demanded() == enabled) return;
    std::scoped_lock lock(publication_mutex);
    const auto epoch = demand_epoch.load(std::memory_order_relaxed);
    if (((epoch & 1U) != 0) == enabled) return;
    demand_epoch.store(epoch + 1, std::memory_order_release);
    latest.reset();
}
SessionToken begin_session() {
    std::scoped_lock lock(publication_mutex);
    ++generation;
    latest.reset();
    sequence = 0;
    return generation;
}
void reset_session() { (void)begin_session(); }
SessionToken current_session() {
    std::scoped_lock lock(publication_mutex);
    return generation;
}
void end_session(SessionToken token) {
    std::scoped_lock lock(publication_mutex);
    if (token == 0 || token != generation) return;
    ++generation;
    latest.reset();
    sequence = 0;
}

void observe_submission(std::span<const std::uint8_t> memory, rom::Revision revision,
                        std::uint32_t scene, SessionToken token, Clock::time_point now) {
    const auto epoch = demand_epoch.load(std::memory_order_acquire);
    if ((epoch & 1U) == 0) return;
    auto decoded = decode_submission(memory, revision, scene);
    std::scoped_lock lock(publication_mutex);
    if (token == 0 || token != generation ||
        epoch != demand_epoch.load(std::memory_order_acquire)) return;
    decoded.session = token;
    decoded.sequence = ++sequence;
    latest = std::move(decoded);
    received = now;
}
std::optional<Observation> read_observation(Clock::time_point now) {
    if (!observation_demanded()) return std::nullopt;
    std::scoped_lock lock(publication_mutex);
    if (!latest || now < received || now - received > kReceiptTimeout) return std::nullopt;
    return latest;
}
std::string describe_observation(const Observation& observation) {
    if (observation.kind == StateKind::OptionsMenu && observation.menu_name == "Options")
        return "Options. Observed selection: " + observation.selection_label + ".";
    if (!observation.menu_name.empty()) return observation.menu_name + "." +
        (observation.selection_label.empty() ? "" : " " + observation.selection_label + ".") +
        (observation.menu_detail.empty() ? "" : " " + observation.menu_detail + ".");
    if (observation.kind != StateKind::Race) return "Game status unavailable.";
    std::string description = observation.paused ? "Race paused." : "Race.";
    if (observation.race_start_prompt == RaceStartPrompt::GetReady) description += " Get Ready.";
    else if (observation.race_start_prompt == RaceStartPrompt::Go) description += " GO.";
    if (!observation.racer) return description + " Player status unavailable.";
    const auto& racer = *observation.racer;
    description += " Player 1 position " + std::to_string(racer.position) +
           ", lap " + std::to_string(racer.lap) + " of " + std::to_string(racer.total_laps) +
           ", bananas " + std::to_string(racer.bananas) + ".";
    if (!racer.held_item) return description + " Held item unavailable.";
    const auto& item = *racer.held_item;
    if (item.category == ItemCategory::None) return description + " No held item.";
    const char* categories[] = {"None", "Boost", "Missile", "Trap", "Shield", "Magnet"};
    const auto index = static_cast<unsigned>(item.category);
    if (index >= std::size(categories)) return description + " Held item unavailable.";
    return description + " " + categories[index] + ", level " + std::to_string(item.level) +
        ", quantity " + std::to_string(item.quantity) + ".";
}
} // namespace dkr::runtime::accessibility
