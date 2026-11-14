// Run the freshly emitted retail loop with font-call observations. The
// fixture header resolves data through the reviewed ELF/symbol map.
#include "recomp.h"
#include "track_select_draw_guest.h"
#include "filename_symbols.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <stdexcept>
#include <source_location>
#include <string>
#include <vector>

static dkr_track_draw_state scope{};
static unsigned checks, begins, ends;
static int red;
struct Glyph {
    int x, y;
    std::string text;
    bool selected;
};
static std::vector<Glyph> glyphs;
static void check(bool okay, std::source_location at = std::source_location::current()) {
    ++checks;
    if (!okay)
        throw std::runtime_error("Native initials carousel regression at line " + std::to_string(at.line()));
}
static gpr guest(unsigned value) {
    return static_cast<std::int32_t>(value);
}
static std::string string_at(std::uint8_t* rdram, gpr address) {
    std::string value;
    for (int i = 0; i < 128; ++i) {
        const auto c = MEM_BU(i, address);
        if (!c)
            return value;
        value += static_cast<char>(c);
    }
    throw std::runtime_error("Unterminated fixture string");
}
extern "C" {
void filename_render(std::uint8_t*, recomp_context*);
unsigned dkr_track_select_draw(std::uint8_t* rdram, recomp_context* ctx, unsigned event, unsigned slot) {
    check(slot == sMenuCurrDisplayList && event <= 1);
    if (event == 0) {
        check(!scope.active);
        ++begins;
    } else {
        check(scope.active);
        ++ends;
    }
    return dkr_track_draw_guest(&scope, rdram, ctx, event);
}
void set_text_background_colour(std::uint8_t*, recomp_context*) {}
void set_text_font(std::uint8_t*, recomp_context*) {}
void set_text_colour(std::uint8_t*, recomp_context* ctx) {
    red = static_cast<int>(ctx->r4);
}
void draw_text(std::uint8_t* rdram, recomp_context* ctx) {
    check(scope.active && static_cast<unsigned>(ctx->r4) == sMenuCurrDisplayList);
    glyphs.push_back({static_cast<int>(ctx->r5), static_cast<int>(ctx->r6), string_at(rdram, ctx->r7), red == 255});
}
void filename_trim(std::uint8_t* rdram, recomp_context* ctx) {
    const auto value = string_at(rdram, ctx->r4);
    for (unsigned i = 0; i <= value.size(); ++i)
        MEM_B(i, ctx->r5) = i < value.size() ? value[i] : 0;
}
void do_break(std::uint32_t) {
    throw std::runtime_error("Unexpected native arithmetic trap");
}
}
int main(int argc, char** argv) {
    check(argc == 2);
    std::vector<std::uint8_t> memory(8U * 1024U * 1024U);
    std::ifstream data(argv[1], std::ios::binary);
    data.read(reinterpret_cast<char*>(memory.data()), memory.size());
    check(data.gcount() == static_cast<std::streamsize>(memory.size()));
    auto* rdram = memory.data();
    recomp_context registers{};
    auto* ctx = &registers;
    ctx->f_odd = &ctx->f0.u32h;
    constexpr unsigned target = 0x80020000U, name = 0x80020100U, title = 0x80020200U, texts = 0x80020300U;
    const auto word = [&](unsigned address, unsigned value) { MEM_W(0, guest(address)) = value; };
    const auto text = [&](unsigned address, const std::string& value) {
        for (unsigned i = 0; i <= value.size(); ++i)
            MEM_B(i, guest(address)) = i < value.size() ? value[i] : 0;
    };
    const std::string alphabet = string_at(rdram, guest(gFileNameValidChars)).substr(0, 28);
    check(alphabet.size() == 28);
    text(name, "DKR");
    text(title, "ENTER YOUR INITIALS");
    word(gNameEntryTargetX, target);
    word(gNameEntryString, name);
    word(gMenuText, texts);
    word(texts + 32, title);
    word(gFilenameFont, 1);
    word(gFilenameX, 160);
    word(gFilenameY, 120);
    for (int selected = 0; selected < 31; ++selected)
        for (float fraction : {-.99F, -.5F, 0.F, .01F, .5F, .99F})
            for (int row_y : {187, 196, 215}) {
                glyphs.clear();
                word(target, selected);
                word(gEnterInitalsY, row_y);
                const float offset = selected + fraction;
                word(gNameEntryOffsetX, std::bit_cast<unsigned>(offset));
                ctx->r29 = guest(0x80700000U);
                ctx->r31 = guest(0x80000004U);
                MEM_W(-124, ctx->r29) = 0xDEADBEEFU;
                MEM_W(16, ctx->r29) = 0xCAFEBABEU;
                filename_render(rdram, ctx);
                check(!scope.active && !scope.pending && begins == ends);
                check(static_cast<unsigned>(ctx->r29) == 0x80700000U && static_cast<unsigned>(ctx->r31) == 0x80000004U);
                check(static_cast<unsigned>(MEM_W(-124, ctx->r29)) == 0xDEADBEEFU &&
                      static_cast<unsigned>(MEM_W(16, ctx->r29)) == 0xCAFEBABEU);
                check(MEM_W(0, guest(target)) == selected && string_at(rdram, guest(name)) == "DKR");
                check(static_cast<unsigned>(MEM_W(0, guest(gNameEntryOffsetX))) == std::bit_cast<unsigned>(offset));
                std::vector<Glyph> row;
                std::copy_if(glyphs.begin(), glyphs.end(), std::back_inserter(row),
                             [&](const auto& g) { return g.y == row_y; });
                check(row.size() >= 24 && row.size() <= 25);
                std::sort(row.begin(), row.end(), [](const auto& a, const auto& b) { return a.x < b.x; });
                check(row.front().x <= -280 && row.back().x >= 600);
                const int base = static_cast<int>(offset), center = 160 - static_cast<int>((offset - base) * 40.F);
                for (unsigned i = 0; i < row.size(); ++i) {
                    const auto& glyph = row[i];
                    check(glyph.x > -336 && glyph.x < 656);
                    if (i)
                        check(glyph.x - row[i - 1].x == 40);
                    const int index = ((base + (glyph.x - center) / 40) % 31 + 31) % 31;
                    const auto expected = index < 28    ? alphabet.substr(index, 1)
                                          : index == 28 ? "SP"
                                          : index == 29 ? "DEL"
                                                        : "OK";
                    if (glyph.text != expected || glyph.selected != (index == selected))
                        std::fprintf(stderr, "selected=%d offset=%f center=%d x=%d text=%s expected=%s lit=%d\n",
                                     selected, offset, center, glyph.x, glyph.text.c_str(), expected.c_str(),
                                     int(glyph.selected));
                    check(glyph.text == expected && glyph.selected == (index == selected));
                }
                check(std::count_if(glyphs.begin(), glyphs.end(), [](const auto& g) { return g.text == "DKR"; }) == 2);
            }
    std::printf("Native v%d initials carousel: %u checks passed\n", DKR_TEST_REVISION, checks);
}
