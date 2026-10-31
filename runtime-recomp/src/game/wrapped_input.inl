// Included only in the build-local imgui_widgets.cpp. The opt-in is scoped to
// one review widget; ordinary launcher inputs retain upstream behaviour.
static thread_local float DkrReviewWrapWidth = 0.0f;

static float DkrReviewAdvance(ImWchar c)
{
    ImGuiContext& g = *GImGui;
    return c == '\r' || c == '\n' ? 0.0f : g.Font->GetCharAdvance(c) * (g.FontSize / g.Font->FontSize);
}

static float DkrReviewMeasure(const ImWchar* begin, const ImWchar* end)
{
    float width = 0.0f;
    for (; begin < end; ++begin)
        width += DkrReviewAdvance(*begin);
    return width;
}

// Keep every character (including spaces) in the edit buffer. Both STB's
// hit-testing/navigation and our drawing use these exact visual row breaks.
static const ImWchar* DkrReviewRowEnd(const ImWchar* begin, const ImWchar* end)
{
    float width = 0.0f;
    const ImWchar* word_break = NULL;
    for (const ImWchar* p = begin; p < end; ++p)
    {
        if (*p == '\n')
            return p + 1;
        const float advance = DkrReviewAdvance(*p);
        if (width + advance > DkrReviewWrapWidth && p > begin)
            return word_break ? word_break : p;
        width += advance;
        if (*p == ' ' || *p == '\t' || *p == '-')
            word_break = p + 1;
    }
    return end;
}

static const ImWchar* DkrReviewInkEnd(const ImWchar* begin, const ImWchar* end)
{
    while (end > begin && (end[-1] == '\n' || end[-1] == '\r'))
        --end;
    return end;
}

static ImVec2 DkrReviewPosition(const ImWchar* begin, const ImWchar* end, int index, bool row_end = false)
{
    const ImWchar* cursor = begin + ImClamp(index, 0, (int)(end - begin));
    float y = GImGui->FontSize;
    while (begin < end)
    {
        const ImWchar* next = DkrReviewRowEnd(begin, end);
        if (cursor < next || (row_end && cursor == next && next[-1] != '\n') ||
            (next == end && end[-1] != '\n'))
            return ImVec2(DkrReviewMeasure(begin, cursor), y);
        begin = next;
        y += GImGui->FontSize;
    }
    return ImVec2(0.0f, y);
}

static int DkrReviewRowBoundary(ImGuiInputTextState* state, int index, bool last)
{
    const ImWchar* begin = state->TextW.Data;
    const ImWchar* end = begin + state->CurLenW;
    const ImWchar* cursor = begin + ImClamp(index, 0, state->CurLenW);
    for (const ImWchar* row = begin; row < end; )
    {
        const ImWchar* next = DkrReviewRowEnd(row, end);
        if (cursor < next || (state->Stb.cursor_at_end_of_line && cursor == next && next[-1] != '\n') ||
            (next == end && end[-1] != '\n'))
            return (int)((last ? DkrReviewInkEnd(row, next) : row) - begin);
        row = next;
    }
    return state->CurLenW;
}

static int DkrReviewPreviousRow(ImGuiInputTextState* state, int current)
{
    const ImWchar* begin = state->TextW.Data;
    const ImWchar* end = begin + state->CurLenW;
    int previous = 0;
    for (const ImWchar* row = begin; row < end && row - begin < current; )
    {
        previous = (int)(row - begin);
        row = DkrReviewRowEnd(row, end);
    }
    return previous;
}

static bool DkrReviewVerticalRowEnd(ImGuiInputTextState* state)
{
    const ImWchar* begin = state->TextW.Data;
    const ImWchar* end = begin + state->CurLenW;
    for (const ImWchar* row = begin; row < end; )
    {
        const ImWchar* next = DkrReviewRowEnd(row, end);
        if (next - begin == state->Stb.cursor && next[-1] != '\n')
            return state->Stb.has_preferred_x && state->Stb.preferred_x >= DkrReviewMeasure(row, next) - 0.5f;
        row = next;
    }
    return false;
}

static float DkrReviewHeight(const ImWchar* begin, const ImWchar* end)
{
    float height = GImGui->FontSize;
    while (begin < end)
    {
        const ImWchar* next = DkrReviewRowEnd(begin, end);
        if (next < end || next[-1] == '\n')
            height += GImGui->FontSize;
        begin = next;
    }
    return height;
}

static void DkrReviewSelection(ImDrawList* draw, ImVec2 origin, const ImVec4& clip,
                               const ImWchar* begin, const ImWchar* end,
                               int selected_begin, int selected_end, ImU32 colour)
{
    int first = 0;
    for (const ImWchar* row = begin; row < end; )
    {
        const ImWchar* next = DkrReviewRowEnd(row, end);
        const int last = (int)(next - begin);
        const int a = ImMax(first, selected_begin), b = ImMin(last, selected_end);
        if (a < b)
        {
            const float x = DkrReviewMeasure(row, begin + a);
            const float width = ImMax(1.0f, DkrReviewMeasure(begin + a, begin + b));
            ImRect rect(origin + ImVec2(x, 0), origin + ImVec2(x + width, GImGui->FontSize));
            rect.ClipWith(ImRect(clip.x, clip.y, clip.z, clip.w));
            if (rect.GetWidth() > 0 && rect.GetHeight() > 0)
                draw->AddRectFilled(rect.Min, rect.Max, colour);
        }
        origin.y += GImGui->FontSize;
        first = last;
        row = next;
    }
}

// UTF-8 remains the sole stored/submitted representation. The temporary wide
// buffer is only a layout view, as in upstream InputText's active edit state.
static float DkrReviewDraw(ImDrawList* draw, ImVec2 origin, ImU32 colour,
                            const char* text, const char* text_end)
{
    if (!text_end)
        text_end = text + strlen(text);
    ImVector<ImWchar> wide;
    wide.resize((int)(text_end - text) + 1);
    const int length = ImTextStrFromUtf8(wide.Data, wide.Size, text, text_end);
    const ImWchar* end = wide.Data + length;
    const float height = DkrReviewHeight(wide.Data, end);
    ImVector<char> row_text;
    row_text.resize(length * 4 + 1);
    for (const ImWchar* row = wide.Data; row < end; )
    {
        const ImWchar* next = DkrReviewRowEnd(row, end);
        const ImWchar* ink_end = DkrReviewInkEnd(row, next);
        const int bytes = ImTextStrToUtf8(row_text.Data, row_text.Size, row, ink_end);
        draw->AddText(GImGui->Font, GImGui->FontSize, origin, colour, row_text.Data, row_text.Data + bytes);
        origin.y += GImGui->FontSize;
        row = next;
    }
    return height;
}

// Public only to the launcher translation unit. No global flag, ABI changes,
// new dependency version or modification of the checked-out ImGui is needed.
bool DkrReviewInputText(const char* label, char* buffer, size_t capacity, const ImVec2& size)
{
    struct WrapScope
    {
        float previous = DkrReviewWrapWidth;
        ~WrapScope() { DkrReviewWrapWidth = previous; }
    } scope;
    // Reserve the scrollbar from the start: typing the first overflowing row
    // must not change line widths and cause an oscillating reflow.
    DkrReviewWrapWidth = ImMax(1.0f, size.x - ImGui::GetStyle().FramePadding.x * 2.0f
                                    - ImGui::GetStyle().ScrollbarSize - 1.0f);
    const ImGuiID id = ImGui::GetID(label);
    ImGuiStorage* storage = ImGui::GetStateStorage();
    const ImGuiID width_id = ImHashStr("review-wrap-width", 0, id);
    const ImGuiID font_id = ImHashStr("review-wrap-font", 0, id);
    if (storage->GetFloat(width_id) != DkrReviewWrapWidth || storage->GetFloat(font_id) != GImGui->FontSize)
    {
        if (ImGuiInputTextState* state = ImGui::GetInputTextState(id))
        {
            state->CursorFollow = true;
            state->Stb.has_preferred_x = 0;
            state->Stb.cursor_at_end_of_line = 0;
        }
        storage->SetFloat(width_id, DkrReviewWrapWidth);
        storage->SetFloat(font_id, GImGui->FontSize);
    }
    return ImGui::InputTextMultiline(label, buffer, capacity, size, ImGuiInputTextFlags_NoHorizontalScroll);
}
