// The original heading renderer walked bytes. Decode UTF-8 before measuring
// or rendering tracked glyphs so accents and Cyrillic remain a single glyph.
#include <cstring>

float TrackedTextWidth(ImFont* font, float size, const char* text, float tracking) {
    if (!font || !text) return 0.F;
    const float scale = size / std::max(font->FontSize, 1.F);
    float width = 0.F;
    const char* end = text + std::strlen(text);
    while (text < end) {
        unsigned codepoint = 0;
        const int bytes = ImTextCharFromUtf8(&codepoint, text, end);
        if (bytes <= 0) break;
        text += bytes;
        if (const auto* glyph = font->FindGlyph(ImWchar(codepoint))) width += glyph->AdvanceX * scale;
        if (text < end) width += tracking;
    }
    return width;
}
void AddTrackedText(ImDrawList* draw, ImFont* font, float size, ImVec2 position,
                    ImU32 colour, const char* text, float tracking) {
    if (!draw || !font || !text) return;
    const float scale = size / std::max(font->FontSize, 1.F);
    const char* end = text + std::strlen(text);
    while (text < end) {
        unsigned codepoint = 0;
        const int bytes = ImTextCharFromUtf8(&codepoint, text, end);
        if (bytes <= 0) break;
        draw->AddText(font, size, position, colour, text, text + bytes);
        text += bytes;
        if (const auto* glyph = font->FindGlyph(ImWchar(codepoint))) position.x += glyph->AdvanceX * scale;
        if (text < end) position.x += tracking;
    }
}
