"""Opt-in review editor, composed onto accepted sources in a build directory.

Never changes the dependency checkout, generated guest code or frozen capsule.
No application/test execution. All replacements are bounded, counted anchors.
"""
from pathlib import Path
import argparse
import hashlib
import json

AUTH = Path(__file__).resolve().parent


def once(text, old, new):
    if text.count(old) != 1:
        raise ValueError('Review editor anchor changed: ' + old[:100])
    return text.replace(old, new, 1)


def between(text, begin, end, transform):
    if text.count(begin) != 1:
        raise ValueError('Review editor block changed: ' + begin)
    start = text.index(begin)
    finish = text.index(end, start)
    return text[:start] + transform(text[start:finish]) + text[finish:]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def widgets(text):
    text = once(text, '// Wrapper for stb_textedit.h to edit text',
                '#include "wrapped_input.inl"\n\n// Wrapper for stb_textedit.h to edit text')
    anchor = '    const ImVec2 size = InputTextCalcTextSizeW(obj->Ctx, text + line_start_idx, text + obj->CurLenW, &text_remaining, NULL, true);'
    text = once(text, anchor, '''    ImVec2 size;
    if (DkrReviewWrapWidth > 0.0f)
    {
        text_remaining = DkrReviewRowEnd(text + line_start_idx, text + obj->CurLenW);
        size = ImVec2(DkrReviewMeasure(text + line_start_idx, text_remaining), obj->Ctx->FontSize);
    }
    else
    {
        size = InputTextCalcTextSizeW(obj->Ctx, text + line_start_idx, text + obj->CurLenW, &text_remaining, NULL, true);
    }''')
    # Upstream tracks an unused end-of-line affinity bit. Use it only in our
    # opt-in editor so End can sit after the last glyph of a soft-wrapped row.
    text = once(text, '    stb_textedit_key(this, &Stb, key);', '''    stb_textedit_key(this, &Stb, key);
    if (DkrReviewWrapWidth > 0.0f)
    {
        const int plain_key = key & ~STB_TEXTEDIT_K_SHIFT;
        const bool vertical = plain_key == STB_TEXTEDIT_K_UP || plain_key == STB_TEXTEDIT_K_DOWN ||
                              plain_key == STB_TEXTEDIT_K_PGUP || plain_key == STB_TEXTEDIT_K_PGDOWN;
        Stb.cursor_at_end_of_line = plain_key == STB_TEXTEDIT_K_LINEEND ||
                                   (vertical && DkrReviewVerticalRowEnd(this));
    }''')
    text = once(text, 'bool child_visible = BeginChildEx(label, id, frame_bb.GetSize(), true, ImGuiWindowFlags_NoMove);',
                'bool child_visible = BeginChildEx(label, id, frame_bb.GetSize(), true, ImGuiWindowFlags_NoMove | (DkrReviewWrapWidth > 0.0f ? ImGuiWindowFlags_AlwaysVerticalScrollbar : 0));')
    begin = '        {\n            // Find lines numbers straddling'
    end = '        // Scroll\n'
    text = between(text, begin, end, lambda prior: '''        if (DkrReviewWrapWidth > 0.0f)
        {
            const ImWchar* text_end = text_begin + state->CurLenW;
            cursor_offset = DkrReviewPosition(text_begin, text_end, state->Stb.cursor, state->Stb.cursor_at_end_of_line != 0);
            select_start_offset = DkrReviewPosition(text_begin, text_end, ImMin(state->Stb.select_start, state->Stb.select_end));
            text_size = ImVec2(inner_size.x, DkrReviewHeight(text_begin, text_end));
        }
        else
''' + prior)
    text = once(text, '''        if (render_selection)
        {
            const ImWchar* text_selected_begin''', '''        if (render_selection && DkrReviewWrapWidth > 0.0f)
        {
            DkrReviewSelection(draw_window->DrawList, draw_pos - draw_scroll, clip_rect,
                               text_begin, text_begin + state->CurLenW,
                               ImMin(state->Stb.select_start, state->Stb.select_end),
                               ImMax(state->Stb.select_start, state->Stb.select_end),
                               GetColorU32(ImGuiCol_TextSelectedBg, render_cursor ? 1.0f : 0.6f));
        }
        else if (render_selection)
        {
            const ImWchar* text_selected_begin''')
    for position in ('draw_pos - draw_scroll', 'draw_pos'):
        old = '            draw_window->DrawList->AddText(g.Font, g.FontSize, ' + position + ', col, buf_display, buf_display_end, 0.0f, is_multiline ? NULL : &clip_rect);'
        text = once(text, old, '''            if (DkrReviewWrapWidth > 0.0f)
                text_size.y = DkrReviewDraw(draw_window->DrawList, ''' + position + ''', col, buf_display, buf_display_end);
            else
                ''' + old.strip())
    return text


def textedit(text):
    # Keep stock undo, Unicode handling, paste, filtering, IME and input events.
    # Only row boundaries differ in the opt-in editor.
    text = once(text, '      if (n < i + r.num_chars)\n',
                '      if (n < i + r.num_chars || (DkrReviewWrapWidth > 0.0f && str->Stb.cursor_at_end_of_line && n == i + r.num_chars && r.num_chars > 0 && STB_TEXTEDIT_GETCHAR(str, n - 1) != STB_TEXTEDIT_NEWLINE))\n')
    text = once(text, '''            prev_scan = find.prev_first > 0 ? find.prev_first - 1 : 0;
            while (prev_scan > 0 && STB_TEXTEDIT_GETCHAR(str, prev_scan - 1) != STB_TEXTEDIT_NEWLINE)
               --prev_scan;''', '''            prev_scan = find.prev_first > 0 ? find.prev_first - 1 : 0;
            if (DkrReviewWrapWidth > 0.0f)
               prev_scan = DkrReviewPreviousRow(str, find.prev_first);
            else while (prev_scan > 0 && STB_TEXTEDIT_GETCHAR(str, prev_scan - 1) != STB_TEXTEDIT_NEWLINE)
               --prev_scan;''')
    for direction in ('START', 'END'):
        for shifted in (False, True):
            start = '      case STB_TEXTEDIT_K_LINE' + direction + (' | STB_TEXTEDIT_K_SHIFT' if shifted else '') + ':'
            def boundary(block):
                anchor = '         else while (state->cursor'
                if block.count(anchor) != 1:
                    raise ValueError('STB row boundary changed')
                return block.replace(anchor, '''         else if (DkrReviewWrapWidth > 0.0f)
            state->cursor = DkrReviewRowBoundary(str, state->cursor, ''' + ('true' if direction == 'END' else 'false') + ''');
''' + anchor, 1)
            text = between(text, start, '         break;', boundary)
    # A new pointer position must not inherit an earlier End-key affinity.
    for function in ('click', 'drag'):
        start = 'static void stb_textedit_' + function + '('
        # Only replace the opening brace, not every nested block.
        pos = text.index(start)
        brace = text.index('{\n', pos)
        text = text[:brace] + '{\n   if (DkrReviewWrapWidth > 0.0f) state->cursor_at_end_of_line = 0;\n' + text[brace + 2:]
    return text


def store_ui(text):
    text = once(text, 'std::size_t maximum = 500, bool multiline = false)',
                'std::size_t maximum = 500, bool multiline = false, bool review = false)')
    text = once(text, '''    bool changed = multiline ? ImGui::InputTextMultiline("##text", buffer.data(), buffer.size(), {edit, 90.F})
                             : ImGui::InputText("##text", buffer.data(), buffer.size());''', '''    if (review)
        ImGui::PushFont(PaddockReading(14.F, false, 1.45F).font);
    const bool changed = review && multiline
        ? DkrReviewInputText("##text", buffer.data(), buffer.size(), {edit, ImGui::GetTextLineHeight() * 6.F + ImGui::GetStyle().FramePadding.y * 2.F})
        : multiline ? ImGui::InputTextMultiline("##text", buffer.data(), buffer.size(), {edit, 90.F})
                    : ImGui::InputText("##text", buffer.data(), buffer.size());
    if (review)
        ImGui::PopFont();''')
    return text


def put(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    data = value.encode('utf-8')
    if not path.exists() or path.read_bytes() != data:
        path.write_bytes(data)


def stage(args):
    ui = args.ui.resolve(strict=True)
    store = args.store.resolve(strict=True)
    renderer = args.widgets.resolve(strict=True)
    out = args.output.resolve()
    root = AUTH.parents[1]
    for protected in ('extern', 'runtime-recomp/RecompiledFuncs', 'runtime-recomp/RecompiledPatches'):
        if out.is_relative_to(root / protected):
            raise ValueError('Protected source directories are read-only')
    inputs = (ui, store / 'mod_store_ui.inl', store / 'store_player_details.inl',
              renderer, renderer.parent / 'imstb_textedit.h')
    # Source roots are read-only. Refuse accidental publication over inputs.
    for source in inputs:
        if out == source.parent or out in source.parents or out.is_relative_to(source.parent):
            raise ValueError('Choose a separate build-local output directory')
    outputs = {}
    value = ui.read_text(encoding='utf-8')
    anchor = '#include "imgui/backends/imgui_impl_sdlrenderer2.h"'
    value = once(value, anchor, anchor + '\nbool DkrReviewInputText(const char*, char*, size_t, const ImVec2&);')
    outputs['native/runtime_ui.cpp'] = value
    outputs['native/mod_store_ui.inl'] = store_ui(inputs[1].read_text(encoding='utf-8'))
    value = inputs[2].read_text(encoding='utf-8')
    outputs['native/store_player_details.inl'] = between(value, 'void StoreReviewForm(', '\nvoid StoreDetails(',
        lambda _: (AUTH / 'native/review_form.inl').read_text(encoding='utf-8').rstrip() + '\n')
    outputs['renderer/imgui_widgets.cpp'] = widgets(renderer.read_text(encoding='utf-8'))
    outputs['renderer/imstb_textedit.h'] = textedit(inputs[4].read_text(encoding='utf-8'))
    outputs['renderer/wrapped_input.inl'] = (AUTH / 'native/wrapped_input.inl').read_text(encoding='utf-8')
    for name, contents in outputs.items():
        put(out / name, contents)
    receipt = {'schema': 1, 'scope': 'review-editor-only',
               'inputs': {str(p): digest(p) for p in inputs},
               'authored': {p.relative_to(AUTH).as_posix(): digest(p) for p in sorted(AUTH.rglob('*'))
                            if p.is_file() and p.suffix in ('.py', '.inl', '.cmake')},
               'outputs': {name: digest(out / name) for name in outputs}}
    put(out / 'review-editor-receipt.json', json.dumps(receipt, indent=2) + '\n')
    for source in inputs:
        if digest(source) != receipt['inputs'][str(source)]:
            raise ValueError('Read-only source changed during composition')
    print('Review editor composed: six isolated sources; no game/tests executed.')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('ui', 'store', 'widgets', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    stage(parser.parse_args())
