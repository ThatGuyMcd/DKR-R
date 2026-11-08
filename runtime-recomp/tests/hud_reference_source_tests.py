"""Compare every authored HUD anchor with the pinned Golden Balloon source.

Usage: python hud_reference_source_tests.py <read-only Golden Balloon checkout>
The reference is a test input, never a build/runtime dependency.
"""

import pathlib
import re
import subprocess
import sys

root = pathlib.Path(__file__).resolve().parents[2]
reference = pathlib.Path(sys.argv[1])
commit = subprocess.check_output(["git", "-C", str(reference), "rev-parse", "HEAD"], text=True).strip()
assert commit == "83a847ccbd3e6334c9cc9c697122d5b3cddb91c1", commit

header = (root / "extern/dkr-decomp/src/game_ui.h").read_text()
enum = re.search(r"enum HudTypes\s*\{(.*?)\}", header, re.S).group(1)
names = re.findall(r"\b(HUD_[A-Z0-9_]+)\b", re.sub(r"//[^\n]*", "", enum))
names = [name for name in names if name != "HUD_ELEMENT_COUNT"]
assert len(names) == 59, names

gb = (reference / "game/src/game_ui.c").read_text(encoding="utf-8")
table = gb.split("sHudWidescreenAnchor[HUD_ELEMENT_COUNT][HUD_WIDESCREEN_MODE_COUNT] = {", 1)[1].split("\n};", 1)[0]
upstream = {}
for name, kind, value in re.findall(r"\[(HUD_[A-Z0-9_]+)\]\s*=\s*HUD_ANCHOR_(ALL_MODES|MODES)\((.*?)\)", table, re.S):
    row = [
        {"LEFT": -1, "CENTER": 0, "RIGHT": 1}[anchor]
        for anchor in re.findall(r"MDKR_HUD_ANCHOR_(LEFT|CENTER|RIGHT)", value)
    ]
    upstream[name] = row * 5 if kind == "ALL_MODES" else row
assert set(names) == set(upstream)

code = (root / "runtime-recomp/src/game/hud_reference_layout.hpp").read_text()
aliases = {
    name: [int(v) for v in value.split(",")]
    for name, value in re.findall(r"\b(left|centre|right|lap|banana|challenge)\{([^{}]+)\}", code)
}
local = code.split("anchors{{", 1)[1].split("}};", 1)[0]
tokens = re.findall(r"Row\{[^{}]+\}|\b[a-z]+\b", local)
assert len(tokens) == 59
for name, token in zip(names, tokens):
    row = [int(v) for v in token[4:-1].split(",")] if token.startswith("Row{") else aliases[token]
    assert row == upstream[name], (name, row, upstream[name])

runtime = (root / "runtime-recomp/src/game/runtime_hud_layout.cpp").read_text()
begin = runtime.split("bool BeginWidget(", 1)[1].split("void EndWidget", 1)[0]
assert "hg::bounds" not in begin and "hg::transform" not in begin and "hg::editable" not in begin
assert "reference::anchor" in begin and "reference::transform" in begin
assert "MEM_H(6,address) != 40" in runtime
assert names[33] == "HUD_COURSE_ARROWS"
assert "g_frame_layout == 1, g_general_pass_active, authored_x)" in begin
element_begin = runtime.split("void dkr::runtime::hud::begin_element(", 1)[1].split("// Accepted 3/4-player path", 1)[0]
assert "if (g_group_frame)" in element_begin
assert "*widget == Widget::CourseArrows ? ReadFloat(rdram,address,0x0C) : 0.0F" in element_begin
bridge = (root / "runtime-recomp/src/game/f3ddkr_rt64.cpp").read_text()
assert "presentation_variant == hud::reference::kPassVariant" in bridge
assert "hud::push_hud_projection" in bridge and bridge.count("hud::pop_hud_projection") == 2
assert "hud_color_image_count" in bridge
assert "hud_layout_editor.cpp" not in (root / "runtime-recomp/CMakeLists.txt").read_text()
print("Golden Balloon reference parity: all 59 slots x 5 modes (295 anchors) match; production routing guards passed")
