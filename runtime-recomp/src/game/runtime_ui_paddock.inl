// Included by runtime_ui.cpp inside its UI namespace, and by the mod-browser UI
// test. Presentation only.
//
// The "race paddock" look first used by MODS / HACKS: painted signs in the
// launcher lettering, and a plain reading face for everything a player reads.
// Sizes below are CSS pixels, as in tools/launcher-html. Dear ImGui sizes a face
// by its ascent - descent, CSS by its em square, so every face carries the
// ratio between the two.

// ------------------------------------------------------------------ colour

constexpr ImU32 PaddockRgb(unsigned rgb, unsigned alpha = 255U) {
    return IM_COL32((rgb >> 16U) & 0xFFU, (rgb >> 8U) & 0xFFU, rgb & 0xFFU,
                    alpha);
}

// Through the style alpha, so BeginDisabled dims painted widgets like ImGui's.
inline ImU32 PaddockApply(ImU32 colour) {
    return ImGui::GetColorU32(colour);
}

inline ImU32 PaddockCol(unsigned rgb, unsigned alpha = 255U) {
    return ImGui::GetColorU32(PaddockRgb(rgb, alpha));
}

inline ImU32 PaddockMix(ImU32 from, ImU32 to, float t) {
    t = std::clamp(t, 0.0F, 1.0F);
    const auto channel = [&](unsigned shift) -> ImU32 {
        const float a = static_cast<float>((from >> shift) & 0xFFU);
        const float b = static_cast<float>((to >> shift) & 0xFFU);
        return static_cast<ImU32>(std::lround(a + (b - a) * t)) << shift;
    };
    return channel(IM_COL32_R_SHIFT) | channel(IM_COL32_G_SHIFT) |
           channel(IM_COL32_B_SHIFT) | channel(IM_COL32_A_SHIFT);
}

// ------------------------------------------------------------------ motion

// cubic-bezier(.2, 0, 0, 1): a quick start and a long settle.
inline float PaddockEase(float t) {
    t = std::clamp(t, 0.0F, 1.0F);
    if (t <= 0.0F || t >= 1.0F) return t;
    const auto bezier = [](float u, float p1, float p2) {
        const float v = 1.0F - u;
        return 3.0F * v * v * u * p1 + 3.0F * v * u * u * p2 + u * u * u;
    };
    float low = 0.0F;
    float high = 1.0F;
    for (int step = 0; step < 18; ++step) {
        const float middle = (low + high) * 0.5F;
        if (bezier(middle, 0.2F, 0.0F) < t) {
            low = middle;
        } else {
            high = middle;
        }
    }
    return bezier((low + high) * 0.5F, 0.0F, 1.0F);
}

// The launcher and the in-game overlay are separate ImGui contexts with their
// own clocks and frame counters; paddock motion shares this one instead.
inline double PaddockClock() {
    using namespace std::chrono;
    static const steady_clock::time_point start = steady_clock::now();
    return duration<double>(steady_clock::now() - start).count();
}

struct PaddockTweenState {
    float value = 0.0F;
    double stamp = 0.0;
};
std::map<ImGuiID, PaddockTweenState> g_paddock_tweens;

// Linear progress toward `on`; callers ease it. A widget seen for the first
// time starts settled, so a page never animates in from nothing.
inline float PaddockTween(ImGuiID id, bool on, float in_seconds,
                          float out_seconds) {
    auto [found, added] = g_paddock_tweens.try_emplace(id);
    PaddockTweenState& state = found->second;
    const double now = PaddockClock();
    if (added) {
        state.value = on ? 1.0F : 0.0F;
    } else {
        const float elapsed = static_cast<float>(std::clamp(now - state.stamp, 0.0, 0.1));
        const float duration = on ? in_seconds : out_seconds;
        const float step = duration <= 0.0F ? 1.0F : elapsed / duration;
        state.value = on ? std::min(state.value + step, 1.0F)
                         : std::max(state.value - step, 0.0F);
    }
    state.stamp = now;
    return state.value;
}

inline ImGuiID PaddockKey(const char* purpose, ImGuiID seed) {
    return ImHashStr(purpose, 0, seed);
}

// Scales everything drawn from `first_vertex` on about `centre`: the CSS
// `transform: scale()` of a pressed control, without touching layout.
inline void PaddockScaleVertices(ImDrawList* draw, int first_vertex,
                                 ImVec2 centre, float scale) {
    if (scale >= 0.9999F) return;
    for (int index = first_vertex; index < draw->VtxBuffer.Size; ++index) {
        ImVec2& position = draw->VtxBuffer[index].pos;
        position.x = centre.x + (position.x - centre.x) * scale;
        position.y = centre.y + (position.y - centre.y) * scale;
    }
}

// ------------------------------------------------------------------ faces

constexpr std::array<float, 7> kPaddockReadingSizes{{
    11.0F, 12.0F, 13.0F, 14.0F, 15.0F, 16.0F, 20.0F}};
// Index 0 is the regular face, 1 semibold.
std::array<std::array<ImFont*, kPaddockReadingSizes.size()>, 2>
    g_paddock_reading{};
float g_paddock_reading_scale = 1.33F;
// The launcher lettering (Racing Banana with Jumpman digits) at sign sizes.
// The launcher's 19 px body font covers the size in between.
constexpr std::array<float, 7> kPaddockSignSizes{{
    12.0F, 15.0F, 20.0F, 24.0F, 30.0F, 34.0F, 38.0F}};
std::array<ImFont*, kPaddockSignSizes.size()> g_paddock_sign{};
// Codes (Quick Join, Friend Codes) in a monospaced face; index 0 regular,
// 1 bold. Empty where the system has no such face.
constexpr std::array<float, 3> kPaddockMonoSizes{{12.0F, 16.0F, 32.0F}};
std::array<std::array<ImFont*, kPaddockMonoSizes.size()>, 2> g_paddock_mono{};
float g_paddock_mono_scale = 1.0F;
float g_paddock_mono_ascent = 0.8F;

// (ascent - descent) / unitsPerEm from the hhea and head tables; `ascent`
// receives ascent / unitsPerEm.
inline float PaddockFaceScale(const unsigned char* data, std::size_t size,
                              float* ascent = nullptr) {
    const auto u16 = [&](std::size_t at) -> unsigned {
        return at + 2U <= size
            ? (static_cast<unsigned>(data[at]) << 8U) | data[at + 1U] : 0U;
    };
    const auto s16 = [&](std::size_t at) -> int {
        return static_cast<std::int16_t>(u16(at));
    };
    const auto u32 = [&](std::size_t at) -> std::size_t {
        return at + 4U <= size
            ? (static_cast<std::size_t>(data[at]) << 24U) |
                  (static_cast<std::size_t>(data[at + 1U]) << 16U) |
                  (static_cast<std::size_t>(data[at + 2U]) << 8U) |
                  data[at + 3U]
            : 0U;
    };
    std::size_t hhea = 0U;
    std::size_t head = 0U;
    const unsigned tables = u16(4U);
    for (unsigned table = 0U; table < tables; ++table) {
        const std::size_t record = 12U + 16U * table;
        if (record + 16U > size) break;
        if (std::memcmp(data + record, "hhea", 4U) == 0) hhea = u32(record + 8U);
        if (std::memcmp(data + record, "head", 4U) == 0) head = u32(record + 8U);
    }
    const unsigned units = head != 0U ? u16(head + 18U) : 0U;
    if (hhea == 0U || units == 0U) return 1.0F;
    if (ascent != nullptr) {
        *ascent = static_cast<float>(s16(hhea + 4U)) / static_cast<float>(units);
    }
    const int extent = s16(hhea + 4U) - s16(hhea + 6U);
    return extent > 0 ? static_cast<float>(extent) / static_cast<float>(units)
                      : 1.0F;
}

#if defined(_WIN32)
// System faces are read from the copies Windows installed; they are never
// redistributed.
inline const std::vector<char>& PaddockSystemFont(const char* file) {
    static std::map<std::string, std::vector<char>> faces;
    auto [found, added] = faces.try_emplace(file);
    if (added) {
        const char* windows = std::getenv("WINDIR");
        std::filesystem::path path = std::filesystem::u8path(
            windows != nullptr && *windows != '\0' ? windows : "C:\\Windows");
        path /= "Fonts";
        path /= file;
        std::ifstream input(path, std::ios::binary);
        std::vector<char> bytes((std::istreambuf_iterator<char>(input)),
                                std::istreambuf_iterator<char>());
        const bool truetype = bytes.size() > 12U &&
            std::memcmp(bytes.data(), "\x00\x01\x00\x00", 4U) == 0;
        if (truetype) found->second = std::move(bytes);
    }
    return found->second;
}

inline const std::vector<char>& PaddockSystemFace(bool semibold) {
    return PaddockSystemFont(semibold ? "seguisb.ttf" : "segoeui.ttf");
}
#endif

// Adds the reading face at every paddock size: Segoe UI where Windows has it,
// otherwise the embedded Selawik, its metric-compatible open replacement.
inline void LoadPaddockReadingFonts(ImFontAtlas* atlas) {
    static constexpr ImWchar kRanges[] = {
        0x0020, 0x00FF,  // Basic Latin and Latin-1 Supplement.
        0x2010, 0x2027,  // Dashes, quotes, bullet and ellipsis.
        0x2030, 0x203A,
        0x2190, 0x2193,  // Arrows.
        0,
    };
    for (auto& face : g_paddock_reading) face.fill(nullptr);
    for (std::size_t weight = 0U; weight < 2U; ++weight) {
        const bool semibold = weight == 1U;
        const char* data = semibold ? dkr_selawik_semibold_font
                                    : dkr_selawik_regular_font;
        std::size_t size = semibold ? dkr_selawik_semibold_font_size
                                    : dkr_selawik_regular_font_size;
#if defined(_WIN32)
        const std::vector<char>& system = PaddockSystemFace(semibold);
        if (!system.empty()) {
            data = system.data();
            size = system.size();
        }
#endif
        const float scale = PaddockFaceScale(
            static_cast<const unsigned char*>(static_cast<const void*>(data)),
            size);
        if (!semibold) g_paddock_reading_scale = scale;
        for (std::size_t index = 0U; index < kPaddockReadingSizes.size(); ++index) {
            ImFontConfig config{};
            config.FontDataOwnedByAtlas = false;
            // Sub-pixel advances keep the reading face's natural spacing; a
            // little extra coverage matches how Windows renders it.
            config.OversampleH = 3;
            config.OversampleV = 1;
            config.PixelSnapH = false;
            config.RasterizerMultiply = 1.25F;
            config.GlyphRanges = kRanges;
            g_paddock_reading[weight][index] = atlas->AddFontFromMemoryTTF(
                const_cast<void*>(static_cast<const void*>(data)), static_cast<int>(size),
                kPaddockReadingSizes[index] * scale, &config, kRanges);
        }
    }
}

// Consolas where Windows has it. Elsewhere codes fall back to the reading face.
inline void LoadPaddockMonoFonts(ImFontAtlas* atlas) {
    for (auto& face : g_paddock_mono) face.fill(nullptr);
#if defined(_WIN32)
    static constexpr ImWchar kRanges[] = {
        0x0020, 0x007E,
        0x00B7, 0x00B7,  // Middle dot.
        0x2190, 0x2193,  // Arrows.
        0,
    };
    for (std::size_t weight = 0U; weight < 2U; ++weight) {
        const std::vector<char>& face = PaddockSystemFont(weight == 1U ? "consolab.ttf" : "consola.ttf");
        if (face.empty()) continue;
        float ascent = 0.8F;
        const float scale = PaddockFaceScale(
            static_cast<const unsigned char*>(static_cast<const void*>(face.data())),
            face.size(), &ascent);
        if (weight == 0U) {
            g_paddock_mono_scale = scale;
            g_paddock_mono_ascent = ascent;
        }
        for (std::size_t index = 0U; index < kPaddockMonoSizes.size(); ++index) {
            // Regular codes only ever appear small; 32 px tiles are bold.
            if (weight == 0U && kPaddockMonoSizes[index] > 16.0F) continue;
            ImFontConfig config{};
            config.FontDataOwnedByAtlas = false;
            config.OversampleH = 2;
            config.OversampleV = 1;
            config.PixelSnapH = false;
            config.GlyphRanges = kRanges;
            g_paddock_mono[weight][index] = atlas->AddFontFromMemoryTTF(
                const_cast<char*>(face.data()), static_cast<int>(face.size()),
                kPaddockMonoSizes[index] * scale, &config, kRanges);
        }
    }
#else
    (void)atlas;
#endif
}

// ------------------------------------------------------------------ type

struct PaddockType {
    ImFont* font = nullptr;
    float size = 0.0F;      // Dear ImGui size: the face's ascent - descent.
    float line = 0.0F;      // CSS line box.
    float ascent = 0.0F;    // CSS ascent, for the baseline inside the line box.
    float content = 0.0F;   // CSS content area.
    float tracking = 0.0F;  // CSS letter-spacing, after every glyph.
};

inline PaddockType PaddockReading(float px, bool semibold = false,
                                  float line_height = 1.5F) {
    PaddockType type;
    const auto& faces = g_paddock_reading[semibold ? 1U : 0U];
    std::size_t best = kPaddockReadingSizes.size();
    for (std::size_t index = 0U; index < kPaddockReadingSizes.size(); ++index) {
        if (faces[index] == nullptr) continue;
        if (best == kPaddockReadingSizes.size() ||
            std::abs(kPaddockReadingSizes[index] - px) <
                std::abs(kPaddockReadingSizes[best] - px)) {
            best = index;
        }
    }
    if (best < kPaddockReadingSizes.size()) {
        type.font = faces[best];
        type.size = px * g_paddock_reading_scale;
    } else {
        type.font = ImGui::GetFont();
        type.size = px * 1.33F;
    }
    // Segoe UI's Windows metrics (2210 / 514 on a 2048 em) place the baseline
    // for both faces, so a fallback never moves a line.
    type.ascent = px * 2210.0F / 2048.0F;
    type.content = px * 2724.0F / 2048.0F;
    type.line = std::round(px * line_height * 100.0F) / 100.0F;
    return type;
}

inline PaddockType PaddockSign(float px, float line_height = 1.2F,
                               float tracking_em = 0.015F) {
    PaddockType type;
    ImFont* body = ImGui::GetIO().FontDefault;
    float best_size = 19.0F;
    type.font = body;
    for (std::size_t index = 0U; index < kPaddockSignSizes.size(); ++index) {
        if (g_paddock_sign[index] == nullptr) continue;
        if (type.font == nullptr ||
            std::abs(kPaddockSignSizes[index] - px) < std::abs(best_size - px)) {
            type.font = g_paddock_sign[index];
            best_size = kPaddockSignSizes[index];
        }
    }
    if (type.font == nullptr) type.font = ImGui::GetFont();
    type.size = px;
    type.ascent = px;
    type.content = px;
    type.line = px * line_height;
    type.tracking = px * tracking_em;
    return type;
}

// A code face (Cascadia Mono / Consolas in the launcher study). Falls back to
// the semibold reading face when no monospaced face was loaded.
inline PaddockType PaddockMono(float px, bool bold = false, float line_height = 1.5F,
                               float tracking_em = 0.0F) {
    const auto& faces = g_paddock_mono[bold ? 1U : 0U];
    std::size_t best = kPaddockMonoSizes.size();
    for (std::size_t index = 0U; index < kPaddockMonoSizes.size(); ++index) {
        if (faces[index] == nullptr) continue;
        if (best == kPaddockMonoSizes.size() ||
            std::abs(kPaddockMonoSizes[index] - px) <
                std::abs(kPaddockMonoSizes[best] - px)) {
            best = index;
        }
    }
    if (best == kPaddockMonoSizes.size()) {
        PaddockType type = PaddockReading(px, true, line_height);
        type.tracking = px * tracking_em;
        return type;
    }
    PaddockType type;
    type.font = faces[best];
    type.size = px * g_paddock_mono_scale;
    type.ascent = px * g_paddock_mono_ascent;
    type.content = px * g_paddock_mono_scale;
    type.line = std::round(px * line_height * 100.0F) / 100.0F;
    type.tracking = px * tracking_em;
    return type;
}

// Offset from the top of a line box to the position ImGui draws text at.
inline float PaddockGlyphTop(const PaddockType& type) {
    const float font_scale = type.size / std::max(type.font->FontSize, 1.0F);
    const float baseline = (type.line - type.content) * 0.5F + type.ascent;
    return std::round(baseline - type.font->Ascent * font_scale);
}

inline float PaddockMeasure(const PaddockType& type, const char* begin,
                            const char* end) {
    if (begin >= end) return 0.0F;
    if (type.tracking == 0.0F) {
        return type.font->CalcTextSizeA(type.size, FLT_MAX, 0.0F, begin, end).x;
    }
    const float scale = type.size / std::max(type.font->FontSize, 1.0F);
    float width = 0.0F;
    for (const char* cursor = begin; cursor < end;) {
        unsigned int character = 0U;
        cursor += ImTextCharFromUtf8(&character, cursor, end);
        if (character == 0U) break;
        width += type.font->GetCharAdvance(static_cast<ImWchar>(character)) *
                     scale + type.tracking;
    }
    return width;
}

inline float PaddockMeasure(const PaddockType& type, std::string_view text) {
    return PaddockMeasure(type, text.data(), text.data() + text.size());
}

struct PaddockLine {
    const char* begin = nullptr;
    const char* end = nullptr;
    float width = 0.0F;
};

// Greedy word wrap with CSS `overflow-wrap: anywhere` for words that do not
// fit on a line of their own. Newlines always break.
inline std::vector<PaddockLine> PaddockWrap(const PaddockType& type,
                                            std::string_view text,
                                            float width) {
    std::vector<PaddockLine> lines;
    width = std::max(width, 1.0F);
    const char* cursor = text.data();
    const char* const end = text.data() + text.size();
    while (cursor <= end) {
        const char* paragraph_end = static_cast<const char*>(
            std::memchr(cursor, '\n', static_cast<std::size_t>(end - cursor)));
        if (paragraph_end == nullptr) paragraph_end = end;
        PaddockLine line{cursor, cursor, 0.0F};
        const char* word = cursor;
        while (word < paragraph_end) {
            const char* word_end = word;
            while (word_end < paragraph_end && *word_end != ' ') ++word_end;
            const float line_with_word =
                PaddockMeasure(type, line.begin, word_end);
            if (line.end == line.begin || line_with_word <= width) {
                if (line.end != line.begin || line_with_word <= width) {
                    line.end = word_end;
                    line.width = line_with_word;
                } else {
                    // A single word wider than the line: break inside it.
                    const char* piece = line.begin;
                    while (piece < word_end) {
                        const char* next = piece;
                        float piece_width = 0.0F;
                        while (next < word_end) {
                            unsigned int character = 0U;
                            const int bytes = ImTextCharFromUtf8(
                                &character, next, word_end);
                            const float grown =
                                PaddockMeasure(type, piece, next + bytes);
                            if (grown > width && next > piece) break;
                            next += bytes;
                            piece_width = grown;
                        }
                        if (next >= word_end) {
                            line = {piece, next, piece_width};
                        } else {
                            lines.push_back({piece, next, piece_width});
                        }
                        piece = next;
                    }
                }
            } else {
                lines.push_back(line);
                line = {word, word, 0.0F};
                continue;
            }
            word = word_end;
            while (word < paragraph_end && *word == ' ') ++word;
        }
        lines.push_back(line);
        if (paragraph_end >= end) break;
        cursor = paragraph_end + 1;
    }
    return lines;
}

// CSS `text-wrap: balance`: the narrowest width that keeps the line count.
inline std::vector<PaddockLine> PaddockWrapBalanced(const PaddockType& type,
                                                    std::string_view text,
                                                    float width) {
    std::vector<PaddockLine> lines = PaddockWrap(type, text, width);
    if (lines.size() < 2U) return lines;
    const std::size_t count = lines.size();
    float low = width / static_cast<float>(count);
    float high = width;
    for (int step = 0; step < 8; ++step) {
        const float middle = (low + high) * 0.5F;
        if (PaddockWrap(type, text, middle).size() > count) {
            low = middle;
        } else {
            high = middle;
        }
    }
    return PaddockWrap(type, text, high);
}

inline void PaddockDrawRun(ImDrawList* draw, const PaddockType& type,
                           ImVec2 line_top, ImU32 colour, const char* begin,
                           const char* end) {
    const ImVec2 position{std::round(line_top.x),
                          line_top.y + PaddockGlyphTop(type)};
    if (type.tracking == 0.0F) {
        draw->AddText(type.font, type.size, position, colour, begin, end);
        return;
    }
    const float scale = type.size / std::max(type.font->FontSize, 1.0F);
    float x = position.x;
    for (const char* cursor = begin; cursor < end;) {
        unsigned int character = 0U;
        cursor += ImTextCharFromUtf8(&character, cursor, end);
        if (character == 0U) break;
        const auto glyph = static_cast<ImWchar>(character);
        if (character != ' ') {
            type.font->RenderChar(draw, type.size, {std::round(x), position.y},
                                  colour, glyph);
        }
        x += type.font->GetCharAdvance(glyph) * scale + type.tracking;
    }
}

struct PaddockTextStyle {
    ImU32 colour = 0;
    ImU32 shadow = 0;          // text-shadow: 0 <shadow_y> 0 <shadow>
    float shadow_y = 2.0F;
    bool centre = false;
};

inline void PaddockDrawLines(ImDrawList* draw, const PaddockType& type,
                             ImVec2 position, float width,
                             const std::vector<PaddockLine>& lines,
                             const PaddockTextStyle& style) {
    float y = position.y;
    for (const PaddockLine& line : lines) {
        const float x = style.centre
            ? position.x + (width - line.width) * 0.5F : position.x;
        if ((style.shadow & IM_COL32_A_MASK) != 0U) {
            PaddockDrawRun(draw, type, {x, y + style.shadow_y}, style.shadow,
                           line.begin, line.end);
        }
        PaddockDrawRun(draw, type, {x, y}, style.colour, line.begin, line.end);
        y += type.line;
    }
}

inline float PaddockTextHeight(const PaddockType& type, std::string_view text,
                               float width, bool balance = false) {
    if (text.empty()) return 0.0F;
    const auto lines = balance ? PaddockWrapBalanced(type, text, width)
                               : PaddockWrap(type, text, width);
    return type.line * static_cast<float>(lines.size());
}

// Wrapped text drawn at `position`; returns its height.
inline float PaddockTextAt(ImDrawList* draw, const PaddockType& type,
                           ImVec2 position, float width, std::string_view text,
                           const PaddockTextStyle& style,
                           bool balance = false) {
    if (text.empty()) return 0.0F;
    const auto lines = balance ? PaddockWrapBalanced(type, text, width)
                               : PaddockWrap(type, text, width);
    PaddockDrawLines(draw, type, position, width, lines, style);
    return type.line * static_cast<float>(lines.size());
}

// Wrapped text as one layout item at the cursor.
inline float PaddockText(const PaddockType& type, ImU32 colour,
                         std::string_view text, float width,
                         bool balance = false, ImU32 shadow = 0U) {
    const ImVec2 position = ImGui::GetCursorScreenPos();
    PaddockTextStyle style;
    style.colour = PaddockApply(colour);
    style.shadow = shadow != 0U ? PaddockApply(shadow) : 0U;
    const float height = PaddockTextAt(ImGui::GetWindowDrawList(), type,
                                       position, width, text, style, balance);
    ImGui::Dummy({width, std::max(height, 0.0F)});
    return height;
}

// A single line, cut with an ellipsis (CSS `text-overflow: ellipsis`).
inline std::string PaddockEllipsize(const PaddockType& type,
                                    std::string_view text, float width) {
    if (PaddockMeasure(type, text) <= width) return std::string(text);
    constexpr std::string_view kEllipsis = "\xE2\x80\xA6";
    const float room = width - PaddockMeasure(type, kEllipsis);
    const char* cursor = text.data();
    const char* const end = text.data() + text.size();
    const char* fit = cursor;
    while (cursor < end) {
        unsigned int character = 0U;
        cursor += ImTextCharFromUtf8(&character, cursor, end);
        if (PaddockMeasure(type, text.data(), cursor) > room) break;
        fit = cursor;
    }
    return std::string(text.data(), fit) + std::string(kEllipsis);
}

inline void PaddockGap(float height) {
    if (height > 0.0F) ImGui::Dummy({0.0F, height});
}

// ------------------------------------------------------------------ shapes

struct PaddockRadii {
    float top_left = 0.0F;
    float top_right = 0.0F;
    float bottom_right = 0.0F;
    float bottom_left = 0.0F;
};

inline PaddockRadii PaddockRound(float radius) {
    return {radius, radius, radius, radius};
}

inline PaddockRadii PaddockInset(const PaddockRadii& radii, float by) {
    return {std::max(radii.top_left - by, 0.0F),
            std::max(radii.top_right - by, 0.0F),
            std::max(radii.bottom_right - by, 0.0F),
            std::max(radii.bottom_left - by, 0.0F)};
}

inline void PaddockPath(ImDrawList* draw, ImVec2 a, ImVec2 b,
                        const PaddockRadii& radii) {
    const float limit = std::max(std::min(b.x - a.x, b.y - a.y) * 0.5F, 0.0F);
    const float tl = std::min(radii.top_left, limit);
    const float tr = std::min(radii.top_right, limit);
    const float br = std::min(radii.bottom_right, limit);
    const float bl = std::min(radii.bottom_left, limit);
    if (tl > 0.5F) draw->PathArcToFast({a.x + tl, a.y + tl}, tl, 6, 9);
    else draw->PathLineTo(a);
    if (tr > 0.5F) draw->PathArcToFast({b.x - tr, a.y + tr}, tr, 9, 12);
    else draw->PathLineTo({b.x, a.y});
    if (br > 0.5F) draw->PathArcToFast({b.x - br, b.y - br}, br, 0, 3);
    else draw->PathLineTo(b);
    if (bl > 0.5F) draw->PathArcToFast({a.x + bl, b.y - bl}, bl, 3, 6);
    else draw->PathLineTo({a.x, b.y});
}

inline void PaddockFill(ImDrawList* draw, ImVec2 a, ImVec2 b,
                        const PaddockRadii& radii, ImU32 colour) {
    if ((colour & IM_COL32_A_MASK) == 0U || b.x <= a.x || b.y <= a.y) return;
    PaddockPath(draw, a, b, radii);
    draw->PathFillConvex(colour);
}

// A CSS border: `width` px drawn inside the box edge.
inline void PaddockStroke(ImDrawList* draw, ImVec2 a, ImVec2 b,
                          const PaddockRadii& radii, ImU32 colour,
                          float width) {
    if ((colour & IM_COL32_A_MASK) == 0U || width <= 0.0F) return;
    const float half = width * 0.5F;
    PaddockPath(draw, {a.x + half, a.y + half}, {b.x - half, b.y - half},
                PaddockInset(radii, half));
    draw->PathStroke(colour, ImDrawFlags_Closed, width);
}

struct PaddockPanelStyle {
    PaddockRadii radii{};
    ImU32 fill = 0U;
    ImU32 border = 0U;
    float border_width = 0.0F;
    ImU32 ring = 0U;              // box-shadow: inset 0 0 0 <ring_width>
    float ring_width = 0.0F;
    ImU32 drop = 0U;              // box-shadow: 0 <drop_offset> 0 <drop>
    float drop_offset = 0.0F;
    ImU32 highlight = 0U;         // box-shadow: inset 0 <highlight_width> 0
    float highlight_width = 0.0F;
};

inline void PaddockPanel(ImDrawList* draw, ImVec2 a, ImVec2 b,
                         const PaddockPanelStyle& style) {
    if (style.drop_offset > 0.0F) {
        PaddockFill(draw, {a.x, a.y + style.drop_offset},
                    {b.x, b.y + style.drop_offset}, style.radii,
                    PaddockApply(style.drop));
    }
    PaddockFill(draw, a, b, style.radii, PaddockApply(style.fill));
    const float edge = style.border_width;
    if (style.highlight_width > 0.0F) {
        draw->PushClipRect({a.x, a.y + edge},
                           {b.x, a.y + edge + style.highlight_width}, true);
        PaddockFill(draw, {a.x + edge, a.y + edge}, {b.x - edge, b.y - edge},
                    PaddockInset(style.radii, edge),
                    PaddockApply(style.highlight));
        draw->PopClipRect();
    }
    if (style.ring_width > 0.0F) {
        PaddockStroke(draw, {a.x + edge, a.y + edge}, {b.x - edge, b.y - edge},
                      PaddockInset(style.radii, edge), PaddockApply(style.ring),
                      style.ring_width);
    }
    PaddockStroke(draw, a, b, style.radii, PaddockApply(style.border), edge);
}

// One row of a CSS conic-gradient checker: `first` is its top-left quadrant.
inline void PaddockChecker(ImDrawList* draw, ImVec2 origin, int cells,
                           float cell, unsigned first, unsigned second) {
    for (int index = 0; index < cells; ++index) {
        const float x = origin.x + cell * static_cast<float>(index);
        draw->AddRectFilled({x, origin.y}, {x + cell, origin.y + cell},
                            PaddockCol(index % 2 == 0 ? first : second));
    }
}

inline void PaddockDashes(ImDrawList* draw, ImVec2 a, float width,
                          ImU32 colour, float thickness) {
    const float dash = thickness * 3.0F;
    const float gap = thickness * 2.0F;
    const int count = std::max(static_cast<int>((width + gap) / (dash + gap)), 1);
    const float spare = width - (dash * count + gap * (count - 1));
    const float stretched_gap = count > 1 ? gap + spare / (count - 1) : 0.0F;
    for (int index = 0; index < count; ++index) {
        const float x = a.x + (dash + stretched_gap) * index;
        draw->AddRectFilled({x, a.y}, {x + dash, a.y + thickness}, colour);
    }
}

inline void PaddockChevron(ImDrawList* draw, ImVec2 centre, float degrees,
                           float size, ImU32 colour, float thickness) {
    const float radians = degrees * 3.14159265F / 180.0F;
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    const float half = size * 0.5F - thickness * 0.5F;
    const auto point = [&](float x, float y) {
        return ImVec2{centre.x + x * c - y * s, centre.y + x * s + y * c};
    };
    const std::array<ImVec2, 3> points{{
        point(half, -half), point(half, half), point(-half, half)}};
    draw->AddPolyline(points.data(), 3, colour, 0, thickness);
}

inline void PaddockFocusRing(ImDrawList* draw, ImVec2 a, ImVec2 b,
                             float radius) {
    const ImU32 colour = ImGui::GetColorU32(ImGuiCol_NavHighlight);
    draw->AddRect({a.x - 4.5F, a.y - 4.5F}, {b.x + 4.5F, b.y + 4.5F}, colour,
                  radius + 4.5F, 0, 3.0F);
}

inline void PaddockSpinner(ImDrawList* draw, ImVec2 centre, float radius,
                           float thickness, ImU32 colour) {
    const float angle = static_cast<float>(ImGui::GetTime() * 4.0);
    draw->PathArcTo(centre, radius, angle, angle + 4.5F, 24);
    draw->PathStroke(colour, 0, thickness);
}

// Draws a panel beneath content that is only measured once it is drawn.
class PaddockBox {
public:
    PaddockBox(float width, ImVec2 padding, float min_height = 0.0F)
        : draw_(ImGui::GetWindowDrawList()),
          origin_(ImGui::GetCursorScreenPos()),
          width_(width),
          padding_(padding),
          min_height_(min_height) {
        splitter_.Split(draw_, 2);
        splitter_.SetCurrentChannel(draw_, 1);
        ImGui::SetCursorScreenPos({origin_.x + padding.x, origin_.y + padding.y});
        ImGui::BeginGroup();
    }
    PaddockBox(const PaddockBox&) = delete;
    PaddockBox& operator=(const PaddockBox&) = delete;

    float Inner() const { return std::max(width_ - padding_.x * 2.0F, 1.0F); }
    ImVec2 Origin() const { return origin_; }

    // Ends the content; `paint` receives the final rectangle on the layer
    // below it. Returns that rectangle's height.
    template <typename Paint>
    float End(Paint&& paint) {
        ImGui::Dummy({Inner(), 0.0F});
        ImGui::EndGroup();
        const float content = ImGui::GetItemRectMax().y - (origin_.y + padding_.y);
        const float height = std::max(content + padding_.y * 2.0F, min_height_);
        splitter_.SetCurrentChannel(draw_, 0);
        paint(draw_, origin_, ImVec2{origin_.x + width_, origin_.y + height});
        splitter_.Merge(draw_);
        ImGui::SetCursorScreenPos(origin_);
        ImGui::Dummy({width_, height});
        return height;
    }

private:
    ImDrawList* draw_;
    ImDrawListSplitter splitter_;
    ImVec2 origin_;
    float width_;
    ImVec2 padding_;
    float min_height_;
};

// ------------------------------------------------------------------ press

struct PaddockPress {
    bool pressed = false;
    bool hovered = false;
    bool held = false;
    bool focused = false;
    ImVec2 min{};
    ImVec2 max{};
    ImGuiID id = 0;
    int first_vertex = 0;
    float hover = 0.0F;  // eased 0..1
    float press = 0.0F;  // eased 0..1
};

// An invisible item that owns hover, press, focus and navigation. Paint the
// control afterwards and finish with PaddockEndPress.
inline PaddockPress PaddockBeginPress(const char* id, ImVec2 size,
                                      float hover_in = 0.15F,
                                      float hover_out = 0.15F) {
    PaddockPress press;
    size.x = std::max(size.x, 1.0F);
    size.y = std::max(size.y, 1.0F);
    press.pressed = ImGui::InvisibleButton(id, size);
    press.id = ImGui::GetItemID();
    press.min = ImGui::GetItemRectMin();
    press.max = ImGui::GetItemRectMax();
    press.hovered = ImGui::IsItemHovered();
    press.held = ImGui::IsItemActive();
    press.focused = ImGui::IsItemFocused() && ImGui::GetIO().NavVisible;
    press.hover = PaddockEase(PaddockTween(
        PaddockKey("hover", press.id), press.hovered, hover_in, hover_out));
    press.press = PaddockEase(PaddockTween(
        PaddockKey("press", press.id), press.held && press.hovered, 0.15F, 0.15F));
    press.first_vertex = ImGui::GetWindowDrawList()->VtxBuffer.Size;
    return press;
}

// `ring` false leaves the focus ring to the caller, for controls whose ring
// frames only part of the item.
inline void PaddockEndPress(const PaddockPress& press, float radius,
                            float scale_to = 0.96F, bool ring = true) {
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 centre{(press.min.x + press.max.x) * 0.5F,
                        (press.min.y + press.max.y) * 0.5F};
    PaddockScaleVertices(draw, press.first_vertex, centre,
                         1.0F - (1.0F - scale_to) * press.press);
    if (ring && press.focused) PaddockFocusRing(draw, press.min, press.max, radius);
}

inline const char* PaddockLabelEnd(const char* label) {
    return ImGui::FindRenderedTextEnd(label);
}

// ------------------------------------------------------------------ buttons

enum class PaddockButtonKind {
    Plain,     // .mods-button
    Primary,   // .mods-button.mods-primary
    Link,      // .mods-button.mods-link
    WarmLink,  // a link inside a card footer
    Selected,  // .mods-button.is-selected
    Flat,      // a launcher race button in the paddock
    Stop,      // Track Lab's STOP TESTING
    Settings,          // .settings-button
    SettingsSelected,  // .settings-button.is-selected
};

struct PaddockButtonLook {
    unsigned fill, fill_hover, border, border_hover, text, text_hover;
    unsigned fill_alpha = 255U, fill_hover_alpha = 255U;
    unsigned border_alpha = 255U, border_hover_alpha = 255U;
    float padding_x = 15.0F;
    float padding_y = 9.0F;
    float line = 1.4F;
    float font_px = 13.0F;
    float radius = 9.0F;
};

inline PaddockButtonLook PaddockLook(PaddockButtonKind kind) {
    switch (kind) {
    case PaddockButtonKind::Primary:
        return {0xFFC15A, 0xFFD388, 0xFFC15A, 0xFFD388, 0x1E241E, 0x1E241E,
                255U, 255U, 255U, 255U, 22.0F};
    case PaddockButtonKind::Link:
        return {0xFFFFFF, 0xFFFFFF, 0xFFFFFF, 0xFFFFFF, 0xB8DCE8, 0xFFFFFF,
                0U, 10U, 0U, 0U, 10.0F};
    case PaddockButtonKind::WarmLink:
        return {0xFFFFFF, 0xFFFFFF, 0xFFFFFF, 0xFFFFFF, 0xFFE0A0, 0xFFFFFF,
                0U, 10U, 0U, 0U, 10.0F};
    case PaddockButtonKind::Selected:
        return {0x143C3E, 0x244353, 0x58CDB7, 0x7A9BA7, 0xEAF3F5, 0xEAF3F5};
    case PaddockButtonKind::Flat:
        return {0x1C3C4C, 0x2C5060, 0x3C5864, 0x3C5864, 0xFFF6DA, 0xFFF6DA,
                255U, 255U, 255U, 255U, 16.0F, 10.0F, 1.5F};
    case PaddockButtonKind::Stop:
        return {0xEB6E0F, 0x2C5060, 0x3C5864, 0x3C5864, 0xFFF6DA, 0xFFF6DA,
                255U, 255U, 255U, 255U, 16.0F, 10.0F, 1.5F};
    case PaddockButtonKind::Settings:
        return {0x173A4E, 0x234E65, 0x476373, 0xB9CDD7, 0xFFF6DA, 0xFFF6DA,
                255U, 255U, 255U, 255U, 14.0F, 10.0F, 1.35F, 14.0F, 8.0F};
    case PaddockButtonKind::SettingsSelected:
        return {0xFFD078, 0xFFDC99, 0xFFD078, 0xFFDC99, 0x053373, 0x053373,
                255U, 255U, 255U, 255U, 14.0F, 10.0F, 1.35F, 14.0F, 8.0F};
    case PaddockButtonKind::Plain:
    default:
        return {0x162E3C, 0x244353, 0x34505E, 0x7A9BA7, 0xEAF3F5, 0xEAF3F5};
    }
}

inline float PaddockButtonWidth(const char* label, PaddockButtonKind kind) {
    const PaddockButtonLook look = PaddockLook(kind);
    const PaddockType type = PaddockReading(look.font_px, true, look.line);
    return std::ceil(PaddockMeasure(type, label, PaddockLabelEnd(label)) +
                     look.padding_x * 2.0F + 2.0F);
}

// width <= 0 sizes to the label. Long labels wrap and grow the height.
inline bool PaddockButton(const char* label,
                          PaddockButtonKind kind = PaddockButtonKind::Plain,
                          float width = 0.0F, float min_height = 42.0F) {
    const PaddockButtonLook look = PaddockLook(kind);
    const PaddockType type = PaddockReading(look.font_px, true, look.line);
    const char* end = PaddockLabelEnd(label);
    if (width <= 0.0F) width = PaddockButtonWidth(label, kind);
    if (kind == PaddockButtonKind::Settings || kind == PaddockButtonKind::SettingsSelected) {
        min_height = std::max(min_height, 44.0F);
    }
    const float wrap = std::max(width - look.padding_x * 2.0F - 2.0F, 1.0F);
    const auto lines = PaddockWrap(type, std::string_view(label, end - label), wrap);
    const float height = std::max(
        min_height, type.line * lines.size() + look.padding_y * 2.0F + 2.0F);
    const PaddockPress press = PaddockBeginPress(label, {width, height});
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const PaddockRadii radii = PaddockRound(look.radius);
    if (kind == PaddockButtonKind::Primary) {
        for (int layer = 3; layer >= 1; --layer) {
            const float spread = static_cast<float>(layer) * 3.0F;
            PaddockFill(draw, {press.min.x - spread * 0.5F, press.min.y + 3.0F - spread * 0.3F},
                        {press.max.x + spread * 0.5F, press.max.y + 3.0F + spread},
                        PaddockRound(9.0F + spread), PaddockCol(0x000000, 18U));
        }
    }
    const ImU32 fill = PaddockMix(PaddockRgb(look.fill, look.fill_alpha),
                                  PaddockRgb(look.fill_hover, look.fill_hover_alpha),
                                  press.hover);
    const ImU32 border = PaddockMix(PaddockRgb(look.border, look.border_alpha),
                                    PaddockRgb(look.border_hover, look.border_hover_alpha),
                                    press.hover);
    PaddockFill(draw, press.min, press.max, radii, PaddockApply(fill));
    PaddockStroke(draw, press.min, press.max, radii, PaddockApply(border), 1.0F);
    PaddockTextStyle style;
    style.colour = PaddockApply(PaddockMix(PaddockRgb(look.text),
                                         PaddockRgb(look.text_hover), press.hover));
    style.centre = true;
    const float text_height = type.line * lines.size();
    PaddockDrawLines(draw, type,
                     {press.min.x + look.padding_x + 1.0F,
                      press.min.y + std::round((height - text_height) * 0.5F)},
                     wrap, lines, style);
    PaddockEndPress(press, look.radius);
    return press.pressed;
}

// An underlined text link (.mods-text-link).
inline bool PaddockTextLink(const char* label) {
    const PaddockType type = PaddockReading(14.0F, true, 1.5F);
    const char* end = PaddockLabelEnd(label);
    const float width = std::ceil(PaddockMeasure(type, label, end));
    const PaddockPress press = PaddockBeginPress(label, {width, 40.0F});
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImU32 colour = PaddockApply(PaddockMix(PaddockRgb(0xFFCE72),
                                               PaddockRgb(0xFFEDC3), press.hover));
    const float top = press.min.y + (40.0F - type.line) * 0.5F;
    PaddockDrawRun(draw, type, {press.min.x, top}, colour, label, end);
    const float baseline = top + (type.line - type.content) * 0.5F + type.ascent;
    draw->AddRectFilled({press.min.x, baseline + 3.0F},
                        {press.min.x + width, baseline + 4.0F}, colour);
    PaddockEndPress(press, 3.0F, 1.0F);
    return press.pressed;
}

inline float PaddockSectionTabWidth(const char* label) {
    const PaddockType type = PaddockSign(19.0F, 1.4F);
    return std::ceil(7.0F + 9.0F + PaddockMeasure(type, label, PaddockLabelEnd(label)) +
                     32.0F + 4.0F);
}

inline float PaddockSectionTabHeight() {
    return std::ceil(PaddockSign(19.0F, 1.4F).line + 20.0F + 4.0F);
}

// A launcher-lettered tab with a turning chevron (.mods-section-nav button).
// `stretch` > 0 widens it, label left-aligned (.texture-disclosure).
inline bool PaddockSectionTab(const char* label, bool open, float stretch = 0.0F) {
    const PaddockType type = PaddockSign(19.0F, 1.4F);
    const char* end = PaddockLabelEnd(label);
    const float width = std::max(PaddockSectionTabWidth(label), stretch);
    const float height = PaddockSectionTabHeight();
    const PaddockPress press = PaddockBeginPress(label, {width, height});
    const float opened = PaddockEase(PaddockTween(
        PaddockKey("open", press.id), open, 0.2F, 0.2F));
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const auto state = [&](unsigned closed, unsigned hover, unsigned expanded) {
        return PaddockMix(PaddockMix(PaddockRgb(closed), PaddockRgb(hover),
                                     press.hover),
                          PaddockRgb(expanded), opened);
    };
    PaddockPanelStyle panel;
    panel.radii = {8.0F, 16.0F, 8.0F, 8.0F};
    panel.fill = state(0x075478, 0x08698F, 0xAA3322);
    panel.border = state(0x2987AA, 0xFFCA56, 0xFFC454);
    panel.border_width = 2.0F;
    panel.drop = PaddockRgb(0x03121C);
    panel.drop_offset = 3.0F;
    panel.highlight = PaddockRgb(0xFFFFFF, 20U);
    panel.highlight_width = 2.0F;
    PaddockPanel(draw, press.min, press.max, panel);
    const ImU32 text = PaddockApply(state(0xFFF2C8, 0xFFF7DC, 0xFFF5D5));
    const float content_top = press.min.y + 12.0F;
    const float chevron_x = press.min.x + 18.0F + 3.5F - 2.0F * (1.0F - opened);
    const float chevron_y = content_top + type.line * 0.5F - 2.0F * opened;
    PaddockChevron(draw, {chevron_x, chevron_y}, -45.0F + 90.0F * opened, 7.0F,
                   text, 1.5F);
    const float text_x = press.min.x + 18.0F + 7.0F + 9.0F;
    PaddockDrawRun(draw, type, {text_x, content_top + 2.0F},
                   PaddockCol(0x031623), label, end);
    PaddockDrawRun(draw, type, {text_x, content_top}, text, label, end);
    PaddockEndPress(press, 12.0F);
    return press.pressed;
}

// ------------------------------------------------------------------ inputs

// .ui-checkbox at `at`: a 24 px well and an amber tick, the CSS L of 3 px
// borders turned -45 degrees and lifted 2 px.
inline void PaddockCheckBox(ImDrawList* draw, ImVec2 at, bool checked) {
    const ImVec2 end{at.x + 24.0F, at.y + 24.0F};
    PaddockFill(draw, at, end, PaddockRound(5.0F), PaddockCol(0x102C3C));
    PaddockStroke(draw, at, end, PaddockRound(5.0F), PaddockCol(0x6D8998), 1.0F);
    if (!checked) return;
    const ImVec2 centre{at.x + 12.0F, at.y + 12.0F - 2.0F};
    const auto turn = [&](float x, float y) {
        constexpr float kHalf = 0.70710678F;
        return ImVec2{centre.x + (x + y) * kHalf, centre.y + (y - x) * kHalf};
    };
    const std::array<ImVec2, 3> tick{{turn(-4.5F, -3.0F), turn(-4.5F, 1.5F),
                                      turn(6.0F, 1.5F)}};
    draw->AddPolyline(tick.data(), 3, PaddockCol(0xFFD078), 0, 3.0F);
}

// The checkbox's own focus ring (.ui-checkbox:focus-visible): 3 px, 3 px out.
inline void PaddockCheckFocus(ImDrawList* draw, ImVec2 at) {
    draw->AddRect({at.x - 4.5F, at.y - 4.5F}, {at.x + 28.5F, at.y + 28.5F},
                  PaddockCol(0xFFD078), 9.5F, 0, 3.0F);
}

// An On / Off toggle (.mods-toggle): the launcher's checkbox with its state
// as a small label.
inline bool PaddockSwitch(const char* id, bool* value, const char* on_label,
                          const char* off_label, const char* description = nullptr) {
    const PaddockType type = PaddockReading(12.0F, false, 1.5F);
    const char* label = *value ? on_label : off_label;
    const float width = 24.0F + 12.0F + std::ceil(std::max(
        PaddockMeasure(type, on_label), PaddockMeasure(type, off_label)));
    const PaddockPress press = PaddockBeginPress(id, {width, 42.0F});
    if (description != nullptr && ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", description);
    }
    bool changed = false;
    if (press.pressed) {
        *value = !*value;
        changed = true;
    }
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 box{press.min.x, press.min.y + 9.0F};
    PaddockCheckBox(draw, box, *value);
    PaddockDrawRun(draw, type, {box.x + 36.0F, press.min.y + (42.0F - type.line) * 0.5F},
                   PaddockCol(0xFFF6DA), label, label + std::strlen(label));
    PaddockEndPress(press, 6.0F, 1.0F, false);
    if (press.focused) PaddockCheckFocus(draw, box);
    return changed;
}

enum class PaddockCheckKind {
    Plain,  // 24 px, the launcher's one checkbox skin (.ui-checkbox)
    Magic,  // Magic Code sign: thick light rim, dark well
};

// A 24 px checkbox with a wrapped label. `label_type` defaults to 14 px text.
inline bool PaddockCheckbox(const char* id, const char* label, bool* value,
                            float width, PaddockCheckKind kind = PaddockCheckKind::Plain,
                            const PaddockType* label_type = nullptr,
                            unsigned label_colour = 0xFFF6DA) {
    const PaddockType type = label_type != nullptr
        ? *label_type : PaddockReading(14.0F, false, 1.45F);
    const float text_width = std::max(width - 34.0F, 1.0F);
    const auto lines = PaddockWrap(type, label, text_width);
    const float text_height = type.line * lines.size();
    const float height = std::max({42.0F, text_height, 24.0F});
    const PaddockPress press = PaddockBeginPress(id, {width, height});
    bool changed = false;
    if (press.pressed) {
        *value = !*value;
        changed = true;
    }
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 box_min{press.min.x, press.min.y + std::round((height - 24.0F) * 0.5F)};
    const ImVec2 box_max{box_min.x + 24.0F, box_min.y + 24.0F};
    if (kind == PaddockCheckKind::Magic) {
        const PaddockRadii radii = PaddockRound(6.0F);
        PaddockFill(draw, {box_min.x, box_min.y + 2.0F}, {box_max.x, box_max.y + 2.0F},
                    radii, PaddockCol(0x02121D));
        PaddockFill(draw, box_min, box_max, radii,
                    PaddockCol(*value ? 0x073C36U : 0x04263CU));
        PaddockStroke(draw, box_min, box_max, radii,
                      PaddockCol(*value ? 0xFFE293U : 0x7DB9CEU), 2.0F);
        if (*value) {
            const float unit = 24.0F / 41.0F;
            const std::array<ImVec2, 3> tick{{
                {box_min.x + 7.45F * unit, box_min.y + 20.5F * unit},
                {box_min.x + 16.15F * unit, box_min.y + 29.2F * unit},
                {box_min.x + 33.55F * unit, box_min.y + 11.8F * unit}}};
            draw->AddPolyline(tick.data(), 3, PaddockCol(0x1AC2A3), 0, 5.8F * unit);
        }
    } else {
        PaddockCheckBox(draw, box_min, *value);
    }
    PaddockTextStyle style;
    style.colour = PaddockCol(label_colour);
    if (kind == PaddockCheckKind::Magic) style.shadow = PaddockCol(0x031623);
    PaddockDrawLines(draw, type,
                     {press.min.x + 34.0F, press.min.y + std::round((height - text_height) * 0.5F)},
                     text_width, lines, style);
    if (kind == PaddockCheckKind::Magic) {
        PaddockEndPress(press, 6.0F, 1.0F);
    } else {
        PaddockEndPress(press, 6.0F, 1.0F, false);
        if (press.focused) PaddockCheckFocus(draw, box_min);
    }
    return changed;
}

enum class PaddockFieldResult { None, Edited, GamepadActivated };

// A text field (.mods-search). A controller cannot type into it, so a gamepad
// activation is handed back for the caller's on-screen keyboard.
inline PaddockFieldResult PaddockSearch(const char* id, char* buffer,
                                        std::size_t capacity, const char* hint,
                                        float width) {
    const PaddockType type = PaddockReading(13.0F, false, 1.4F);
    ImGui::PushFont(type.font);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 9.0F);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0F);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                        {14.0F, std::max((44.0F - type.font->FontSize) * 0.5F, 0.0F)});
    ImGui::PushStyleColor(ImGuiCol_FrameBg, PaddockRgb(0x102432));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, PaddockRgb(0x102432));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, PaddockRgb(0x102432));
    ImGui::PushStyleColor(ImGuiCol_Border, PaddockRgb(0x34505E));
    ImGui::PushStyleColor(ImGuiCol_Text, PaddockRgb(0xEAF3F5));
    ImGui::PushStyleColor(ImGuiCol_TextDisabled, PaddockRgb(0x9BB0BD));
    ImGui::PushStyleColor(ImGuiCol_NavHighlight, PaddockRgb(0, 0U));
    ImGui::SetNextItemWidth(width);
    const bool edited = ImGui::InputTextWithHint(id, hint, buffer, capacity);
    const bool activated = ImGui::IsItemActivated();
    const bool active = ImGui::IsItemActive();
    const bool focused = ImGui::IsItemFocused() && ImGui::GetIO().NavVisible;
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    ImGui::PopStyleColor(7);
    ImGui::PopStyleVar(3);
    ImGui::PopFont();
    if (activated && GImGui->ActiveIdSource == ImGuiInputSource_Gamepad) {
        ImGui::ClearActiveID();
        return PaddockFieldResult::GamepadActivated;
    }
    if (active || focused) {
        ImGui::GetWindowDrawList()->AddRect(
            {min.x - 4.5F, min.y - 4.5F}, {max.x + 4.5F, max.y + 4.5F},
            PaddockCol(0xFFD11F), 13.5F, 0, 3.0F);
    }
    return edited ? PaddockFieldResult::Edited : PaddockFieldResult::None;
}

// A labelled drop-down (.mods-field select). Returns true when changed.
inline bool PaddockSelect(const char* id, const char* label, int* value,
                          const std::vector<std::string>& items, float width) {
    const PaddockType label_type = PaddockReading(11.0F, true, 1.5F);
    const PaddockType type = PaddockReading(13.0F, false, 1.4F);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    if (label != nullptr && *label != '\0') {
        PaddockDrawRun(draw, label_type, origin, PaddockCol(0xABC0CC), label,
                       label + std::strlen(label));
        ImGui::Dummy({width, label_type.line + 6.0F});
        // A new line starts at the group's edge; the field stays under its label.
        ImGui::SetCursorScreenPos({origin.x, origin.y + label_type.line + 6.0F});
    }
    *value = std::clamp(*value, 0, std::max(static_cast<int>(items.size()) - 1, 0));
    const char* preview = items.empty() ? "" : items[*value].c_str();
    ImGui::PushID(id);
    const PaddockPress press = PaddockBeginPress("##select", {width, 44.0F}, 0.15F, 0.15F);
    const char* popup = "##select-popup";
    if (press.pressed) ImGui::OpenPopup(popup);
    const bool open = ImGui::IsPopupOpen(popup);
    const PaddockRadii radii = PaddockRound(9.0F);
    PaddockFill(draw, press.min, press.max, radii, PaddockCol(0x102432));
    PaddockStroke(draw, press.min, press.max, radii,
                  PaddockCol(open ? 0xFFD11FU : 0x34505EU), 1.0F);
    const float text_width = width - 10.0F - 30.0F;
    const std::string shown = PaddockEllipsize(type, preview, text_width);
    PaddockDrawRun(draw, type, {press.min.x + 11.0F, press.min.y + (44.0F - type.line) * 0.5F},
                   PaddockCol(0xEAF3F5), shown.data(), shown.data() + shown.size());
    PaddockChevron(draw, {press.max.x - 17.0F, press.min.y + 20.0F}, 45.0F, 6.0F,
                   PaddockCol(0xEAF3F5), 1.5F);
    PaddockEndPress(press, 9.0F, 1.0F);
    bool changed = false;
    const float row = 34.0F;
    ImGui::SetNextWindowPos({press.min.x, press.max.y + 4.0F});
    ImGui::SetNextWindowSizeConstraints(
        {width, 0.0F},
        {std::max(width, 1.0F),
         std::min(row * static_cast<float>(items.size()) + 10.0F, 360.0F)});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {5.0F, 5.0F});
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 9.0F);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 1.0F);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {0.0F, 0.0F});
    ImGui::PushStyleColor(ImGuiCol_PopupBg, PaddockRgb(0x102432));
    ImGui::PushStyleColor(ImGuiCol_Border, PaddockRgb(0x34505E));
    if (ImGui::BeginPopup(popup, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, {0.0F, 0.5F});
        ImGui::PushStyleColor(ImGuiCol_Header, PaddockRgb(0x143C3E));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, PaddockRgb(0x244353));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, PaddockRgb(0x2C5060));
        ImGui::PushStyleColor(ImGuiCol_Text, PaddockRgb(0xEAF3F5));
        ImGui::PushFont(type.font);
        for (int index = 0; index < static_cast<int>(items.size()); ++index) {
            ImGui::PushID(index);
            const bool selected = index == *value;
            const std::string row_label = "  " + items[index];
            if (ImGui::Selectable(row_label.c_str(), selected, 0,
                                  {width - 10.0F, row})) {
                changed = *value != index;
                *value = index;
            }
            if (selected && ImGui::IsWindowAppearing()) ImGui::SetItemDefaultFocus();
            ImGui::PopID();
        }
        ImGui::PopFont();
        ImGui::PopStyleColor(4);
        ImGui::PopStyleVar();
        ImGui::EndPopup();
    }
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(4);
    ImGui::PopID();
    return changed;
}

// A level slider (.snd-range): a groove whose fill shows the value, and a
// ringed thumb that travels inside it.
struct PaddockRangeLook {
    float groove = 10.0F;      // track height
    float thumb = 26.0F;       // thumb diameter, ring included
    unsigned fill = 0xFFAB14;  // --snd-fill
    unsigned ring = 0xFFAB14;  // --snd-thumb
    unsigned track = 0x245A70; // --snd-track
    bool bipolar = false;      // the fill grows out of a centre mark
};

struct PaddockRangeResult {
    bool changed = false;
    bool activated = false;    // A or Enter: the caller's quick action
    bool reset = false;        // a double click
    bool focused = false;
    bool hovered = false;
};

// Left and right adjust the slider as soon as it has focus, like a browser
// range input, so A and Enter stay free for the row's quick action.
inline PaddockRangeResult PaddockRange(const char* id, int* value, int minimum,
                                       int maximum, float width,
                                       const PaddockRangeLook& look = {},
                                       float height = 44.0F) {
    PaddockRangeResult result;
    const ImGuiID item = ImGui::GetID(id);
    const bool nav_activate = GImGui->NavActivatePressedId == item;
    const bool pressed = ImGui::InvisibleButton(id, {std::max(width, 1.0F), height});
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    result.hovered = ImGui::IsItemHovered();
    result.focused = ImGui::IsItemFocused() && ImGui::GetIO().NavVisible;
    result.activated = pressed && nav_activate;
    result.reset = result.hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
    const int before = *value;
    const float span = std::max(static_cast<float>(maximum - minimum), 1.0F);
    const float travel = std::max(width - look.thumb, 1.0F);
    if (ImGui::IsItemActive() && GImGui->ActiveIdSource == ImGuiInputSource_Mouse) {
        const float t = (ImGui::GetIO().MousePos.x - min.x - look.thumb * 0.5F) / travel;
        *value = minimum + static_cast<int>(std::lround(std::clamp(t, 0.0F, 1.0F) * span));
    }
    if (ImGui::IsItemFocused()) {
        constexpr std::array<ImGuiKey, 4> kKeys{{
            ImGuiKey_GamepadDpadLeft, ImGuiKey_LeftArrow,
            ImGuiKey_GamepadDpadRight, ImGuiKey_RightArrow}};
        // Owning left and right keeps navigation from leaving the row sideways.
        for (const ImGuiKey key : kKeys) ImGui::SetKeyOwner(key, item);
        for (std::size_t index = 0; index < kKeys.size(); ++index) {
            if (ImGui::IsKeyPressed(kKeys[index], item, ImGuiInputFlags_Repeat)) {
                *value += index < 2U ? -1 : 1;
            }
        }
    }
    *value = std::clamp(*value, minimum, maximum);
    result.changed = *value != before;

    ImDrawList* draw = ImGui::GetWindowDrawList();
    const float t = static_cast<float>(*value - minimum) / span;
    const float mid_y = std::round((min.y + max.y) * 0.5F);
    const ImVec2 groove_min{min.x, mid_y - look.groove * 0.5F};
    const ImVec2 groove_max{max.x, mid_y + look.groove * 0.5F};
    const PaddockRadii groove_radii = PaddockRound(look.groove * 0.5F);
    PaddockFill(draw, groove_min, groove_max, groove_radii, PaddockCol(look.track));
    const auto fill_between = [&](float from, float to) {
        if (to - from < 0.5F) return;
        draw->PushClipRect({groove_min.x + from, groove_min.y},
                           {groove_min.x + to, groove_max.y}, true);
        PaddockFill(draw, groove_min, groove_max, groove_radii, PaddockCol(look.fill));
        draw->PopClipRect();
    };
    if (look.bipolar) {
        const float centre = width * 0.5F;
        fill_between(std::min(centre, width * t), std::max(centre, width * t));
        draw->AddRectFilled({std::round(groove_min.x + centre - 1.0F), groove_min.y},
                            {std::round(groove_min.x + centre + 1.0F), groove_max.y},
                            PaddockCol(0xD8EBF1));
    } else {
        fill_between(0.0F, width * t);
    }
    // box-shadow: inset 0 1px 2px #00000059
    draw->PushClipRect(groove_min, {groove_max.x, groove_min.y + 2.0F}, true);
    PaddockStroke(draw, groove_min, groove_max, groove_radii, PaddockCol(0x000000, 70U), 1.0F);
    draw->PopClipRect();

    const bool grown = (ImGui::IsItemActive() && result.hovered) || result.focused;
    const float scale = 1.0F + 0.12F * PaddockEase(PaddockTween(
        PaddockKey("thumb", item), grown, 0.15F, 0.15F));
    const float radius = look.thumb * 0.5F * scale;
    const ImVec2 thumb{min.x + look.thumb * 0.5F + travel * t, mid_y};
    draw->AddCircleFilled({thumb.x, thumb.y + 2.0F}, radius + 1.5F, PaddockCol(0x000000, 60U), 32);
    draw->AddCircleFilled(thumb, radius, PaddockCol(look.ring), 32);
    draw->AddCircleFilled(thumb, radius - 5.0F * scale, PaddockCol(0xFFF6DA), 32);
    return result;
}

// A help disclosure (.mods-help-disclosure). Draw the body between Begin and
// End only when Begin returns true.
class PaddockDisclosure {
public:
    PaddockDisclosure(const char* id, const char* label, bool& open, float width)
        : box_(width, {16.0F, 0.0F}) {
        const PaddockType type = PaddockReading(14.0F, true, 1.5F);
        const float inner = box_.Inner();
        const auto lines = PaddockWrap(type, label, inner - 15.0F);
        const float height = std::max(46.0F, type.line * lines.size() + 26.0F);
        const PaddockPress press = PaddockBeginPress(id, {inner, height});
        if (press.pressed) open = !open;
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const ImU32 colour = PaddockApply(PaddockMix(PaddockRgb(0xCFDEE6),
                                                   PaddockRgb(0xFFFFFF), press.hover));
        const float top = press.min.y + 13.0F;
        const ImVec2 marker{press.min.x + 3.5F, top + type.line * 0.5F};
        const ImU32 marker_colour = PaddockCol(0x8EAAB9);
        if (open) {
            draw->AddTriangleFilled({marker.x - 3.5F, marker.y - 2.0F},
                                    {marker.x + 3.5F, marker.y - 2.0F},
                                    {marker.x, marker.y + 2.5F}, marker_colour);
        } else {
            draw->AddTriangleFilled({marker.x - 2.0F, marker.y - 3.5F},
                                    {marker.x + 2.5F, marker.y},
                                    {marker.x - 2.0F, marker.y + 3.5F}, marker_colour);
        }
        PaddockTextStyle style;
        style.colour = colour;
        PaddockDrawLines(draw, type, {press.min.x + 15.0F, top}, inner - 15.0F,
                         lines, style);
        PaddockEndPress(press, 10.0F, 1.0F);
        open_ = open;
    }

    bool Open() const { return open_; }
    float Inner() const { return box_.Inner(); }

    void End() {
        if (open_) PaddockGap(16.0F);
        box_.End([](ImDrawList* draw, ImVec2 a, ImVec2 b) {
            PaddockPanelStyle panel;
            panel.radii = PaddockRound(10.0F);
            panel.fill = PaddockRgb(0xFFFFFF, 4U);
            panel.ring = PaddockRgb(0xFFFFFF, 13U);
            panel.ring_width = 1.0F;
            PaddockPanel(draw, a, b, panel);
        });
    }

private:
    PaddockBox box_;
    bool open_ = false;
};

// ------------------------------------------------------------------ labels

enum class PaddockTone { Neutral, On, Warning };

// A status badge with its dot (.mods-badge). Returns its size.
inline ImVec2 PaddockBadgeSize(std::string_view text) {
    const PaddockType type = PaddockReading(11.0F, true, 1.5F);
    return {std::ceil(PaddockMeasure(type, text) + 16.0F + 11.0F),
            std::ceil(type.line + 8.0F)};
}

inline void PaddockBadge(ImDrawList* draw, ImVec2 position, std::string_view text,
                         PaddockTone tone) {
    const PaddockType type = PaddockReading(11.0F, true, 1.5F);
    const ImVec2 size = PaddockBadgeSize(text);
    unsigned fill = 0x041E30;
    unsigned ink = 0xC4D9E3;
    if (tone == PaddockTone::On) {
        fill = 0x104E43;
        ink = 0xB4F4DD;
    } else if (tone == PaddockTone::Warning) {
        fill = 0x503D1E;
        ink = 0xFFE0A2;
    }
    PaddockFill(draw, position, {position.x + size.x, position.y + size.y},
                PaddockRound(4.0F), PaddockCol(fill));
    draw->AddCircleFilled({position.x + 10.5F, position.y + size.y * 0.5F}, 2.5F,
                          PaddockCol(ink), 10);
    PaddockDrawRun(draw, type, {position.x + 19.0F, position.y + 4.0F},
                   PaddockCol(ink), text.data(), text.data() + text.size());
}

enum class PaddockFormat { Legacy, Dkr };

inline ImVec2 PaddockFormatTagSize(PaddockFormat format) {
    const PaddockType type = PaddockSign(15.0F, 1.2F, 0.02F);
    const std::string_view text = format == PaddockFormat::Legacy ? "Legacy" : "DKR";
    return {std::ceil(PaddockMeasure(type, text) + 18.0F),
            std::ceil(type.line + 8.0F)};
}

inline void PaddockFormatTag(ImDrawList* draw, ImVec2 position, PaddockFormat format) {
    const PaddockType type = PaddockSign(15.0F, 1.2F, 0.02F);
    const bool legacy = format == PaddockFormat::Legacy;
    const std::string_view text = legacy ? "Legacy" : "DKR";
    const ImVec2 size = PaddockFormatTagSize(format);
    const ImVec2 end{position.x + size.x, position.y + size.y};
    PaddockFill(draw, position, end, PaddockRound(4.0F),
                PaddockCol(legacy ? 0x473423U : 0x114B4CU));
    PaddockStroke(draw, position, end, PaddockRound(4.0F),
                  PaddockCol(legacy ? 0xA2783AU : 0x469B90U), 1.0F);
    PaddockDrawRun(draw, type, {position.x + 9.0F, position.y + 4.0F},
                   PaddockCol(legacy ? 0xFFDDA5U : 0xC5F5E8U),
                   text.data(), text.data() + text.size());
}

// A calm tinted strip of text (.mods-alert).
inline void PaddockAlert(std::string_view text, float width) {
    const PaddockType type = PaddockReading(14.0F, false, 1.55F);
    const float height = PaddockTextHeight(type, text, width - 32.0F) + 24.0F;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    PaddockFill(draw, origin, {origin.x + width, origin.y + height},
                PaddockRound(9.0F), PaddockCol(0x42341D));
    PaddockTextStyle style;
    style.colour = PaddockCol(0xFFDC9A);
    PaddockTextAt(draw, type, {origin.x + 16.0F, origin.y + 12.0F}, width - 32.0F,
                  text, style);
    ImGui::Dummy({width, height});
}

// Amber status text (.mods-feedback).
inline void PaddockFeedback(std::string_view text, float width) {
    if (text.empty()) return;
    PaddockGap(12.0F);
    PaddockText(PaddockReading(13.0F, false, 1.55F), PaddockRgb(0xFFCF83), text, width);
    PaddockGap(12.0F);
}

// A bold lead-in and its explanation on one tinted line (.mods-inline-note).
inline void PaddockInlineNote(std::string_view strong, std::string_view rest,
                              float width) {
    const PaddockType bold = PaddockReading(13.0F, true, 1.55F);
    const PaddockType plain = PaddockReading(13.0F, false, 1.55F);
    const float inner = width - 32.0F;
    const float strong_width = PaddockMeasure(bold, strong);
    const bool one_line = strong_width + 16.0F + PaddockMeasure(plain, rest) <= inner;
    const float rest_height = one_line ? plain.line
                                       : PaddockTextHeight(plain, rest, inner);
    const float height = one_line ? plain.line + 24.0F
                                  : bold.line + 6.0F + rest_height + 24.0F;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    PaddockFill(draw, origin, {origin.x + width, origin.y + height},
                PaddockRound(9.0F), PaddockCol(0xFFFFFF, 6U));
    PaddockTextStyle strong_style;
    strong_style.colour = PaddockCol(0xE3EDF2);
    PaddockTextAt(draw, bold, {origin.x + 16.0F, origin.y + 12.0F}, inner, strong,
                  strong_style);
    PaddockTextStyle rest_style;
    rest_style.colour = PaddockCol(0xABC0CC);
    if (one_line) {
        PaddockTextAt(draw, plain, {origin.x + 16.0F + strong_width + 16.0F, origin.y + 12.0F},
                      inner, rest, rest_style);
    } else {
        PaddockTextAt(draw, plain, {origin.x + 16.0F, origin.y + 12.0F + bold.line + 6.0F},
                      inner, rest, rest_style);
    }
    ImGui::Dummy({width, height});
}

// A launcher-lettered heading (h2 / h3 in the paddock).
inline float PaddockHeading(std::string_view text, float px, unsigned colour,
                            float width, float line_height = 1.25F) {
    const PaddockType type = PaddockSign(px, line_height);
    return PaddockText(type, PaddockRgb(colour), text, width, true,
                       PaddockRgb(0x031623));
}

// A separator label with its hairline (.im-septext in the paddock).
inline void PaddockSeparatorText(std::string_view text, float width) {
    const PaddockType type = PaddockSign(12.0F, 1.5F, 0.05F);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const float text_width = PaddockMeasure(type, text);
    PaddockDrawRun(draw, type, {origin.x, origin.y + 2.0F}, PaddockCol(0x031623),
                   text.data(), text.data() + text.size());
    PaddockDrawRun(draw, type, origin, PaddockCol(0xFFCD67), text.data(),
                   text.data() + text.size());
    const float y = std::round(origin.y + type.line * 0.5F);
    draw->AddRectFilled({origin.x + text_width + 12.0F, y},
                        {origin.x + width, y + 1.0F}, PaddockCol(0xFFFFFF, 32U));
    ImGui::Dummy({width, type.line});
}

// ------------------------------------------------------------------ modals

// Restyles ordinary ImGui content, including existing ImGui::Button calls
// (routed through DkrRaceButton), as the paddock's calm flat controls.
class PaddockFlatScope {
public:
    PaddockFlatScope() {
        ImGui::PushFont(PaddockReading(14.0F).font);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {12.0F, 13.0F});
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {16.0F, 10.0F});
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 9.0F);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0F);
        ImGui::PushStyleColor(ImGuiCol_Text, PaddockRgb(0xFFF6DA));
        ImGui::PushStyleColor(ImGuiCol_TextDisabled, PaddockRgb(0xABC0CC));
        ImGui::PushStyleColor(ImGuiCol_Button, PaddockRgb(0x1C3C4C));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, PaddockRgb(0x2C5060));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, PaddockRgb(0x2C5060));
        ImGui::PushStyleColor(ImGuiCol_Border, PaddockRgb(0x3C5864));
        ImGui::PushStyleColor(ImGuiCol_Header, PaddockRgb(0x0A616E));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, PaddockRgb(0x0C7483));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, PaddockRgb(0x0A616E));
        ImGui::PushStyleColor(ImGuiCol_Separator, PaddockRgb(0xFFFFFF, 32U));
        ImGui::PushStyleColor(ImGuiCol_FrameBg, PaddockRgb(0x142C3B));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, PaddockRgb(0x174754));
        ImGui::PushStyleColor(ImGuiCol_TableRowBgAlt, PaddockRgb(0xFFFFFF, 15U));
        previous_font_ = g_race_button_flat_font;
        g_race_button_flat_font = PaddockReading(13.0F, true).font;
        ++g_race_button_flat;
    }
    ~PaddockFlatScope() {
        --g_race_button_flat;
        g_race_button_flat_font = previous_font_;
        ImGui::PopStyleColor(13);
        ImGui::PopStyleVar(4);
        ImGui::PopFont();
    }
    PaddockFlatScope(const PaddockFlatScope&) = delete;
    PaddockFlatScope& operator=(const PaddockFlatScope&) = delete;

private:
    ImFont* previous_font_ = nullptr;
};

int g_paddock_modal_frame = -1;
int g_paddock_modal_windows = 0;

// A modal window with the paddock's painted title bar. Only the window is
// styled here; wrap the call in a PaddockFlatScope for the body.
inline bool BeginPaddockModalWindow(const char* name, ImGuiWindowFlags flags) {
    // The title bar is sized from the current font and painted below. Never
    // push a font across Begin: PopFont would drop the popup's atlas texture.
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {24.0F, 24.0F});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 18.0F);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0F);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                        {24.0F, std::max((52.0F - ImGui::GetFontSize()) * 0.5F, 0.0F)});
    ImGui::PushStyleColor(ImGuiCol_PopupBg, PaddockRgb(0x112632));
    ImGui::PushStyleColor(ImGuiCol_Border, PaddockRgb(0xFFFFFF, 32U));
    ImGui::PushStyleColor(ImGuiCol_TitleBg, PaddockRgb(0x08567C));
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive, PaddockRgb(0x08567C));
    ImGui::PushStyleColor(ImGuiCol_TitleBgCollapsed, PaddockRgb(0x08567C));
    // The title is painted below, with its shadow.
    ImGui::PushStyleColor(ImGuiCol_Text, PaddockRgb(0, 0U));
    const bool visible = ImGui::BeginPopupModal(name, nullptr, flags);
    ImGui::PopStyleColor(6);
    ImGui::PopStyleVar(4);
    if (!visible) return false;
    g_paddock_modal_frame = ImGui::GetFrameCount();
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    // TitleBarRect() would measure with today's frame padding, not Begin's.
    const float bar_height = std::max(52.0F, ImGui::GetFontSize());
    const ImRect bar{window->Pos, {window->Pos.x + window->SizeFull.x, window->Pos.y + bar_height}};
    ImDrawList* draw = window->DrawList;
    draw->PushClipRect(window->Rect().Min, window->Rect().Max, false);
    draw->AddRectFilled({bar.Min.x, bar.Max.y - 2.0F}, {bar.Max.x, bar.Max.y},
                        PaddockCol(0xFFBC48));
    const char* end = PaddockLabelEnd(name);
    const PaddockType sign = PaddockSign(24.0F, 1.0F);
    const ImVec2 text{bar.Min.x + 24.0F,
                      std::round((bar.Min.y + bar.Max.y - 2.0F - sign.line) * 0.5F)};
    PaddockDrawRun(draw, sign, {text.x, text.y + 2.0F}, PaddockCol(0x031623), name, end);
    PaddockDrawRun(draw, sign, text, PaddockCol(0xFFF0C2), name, end);
    draw->PopClipRect();
    return true;
}

// ImGui draws the modal dim once the frame ends, from the style of that
// moment. Call just before ImGui::Render.
inline void ApplyPaddockModalDim() {
    static const ImVec4 kDefault = ImGui::GetStyle().Colors[ImGuiCol_ModalWindowDimBg];
    ImGui::GetStyle().Colors[ImGuiCol_ModalWindowDimBg] =
        g_paddock_modal_frame == ImGui::GetFrameCount()
            ? ImVec4{2.0F / 255.0F, 11.0F / 255.0F, 21.0F / 255.0F, 0.72F}
            : kDefault;
}
