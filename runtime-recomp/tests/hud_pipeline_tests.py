"""HUD hook instruction/mapping and protected-baseline checks, without a game."""

import json
import pathlib
import re
import struct
import sys
import zipfile

ROOT = pathlib.Path(__file__).resolve().parents[2]


def symbols(path):
    """Read the retail ELF symbols; do not duplicate addresses in the test."""
    data = pathlib.Path(path).read_bytes()
    assert data[:5] == b"\x7fELF\x01"
    endian = ">" if data[5] == 2 else "<"
    offset = struct.unpack_from(endian + "I", data, 32)[0]
    size, count = struct.unpack_from(endian + "HH", data, 46)
    sections = [struct.unpack_from(endian + "10I", data, offset + i * size) for i in range(count)]
    result = {}
    for section in sections:
        if section[1] != 2:
            continue  # SHT_SYMTAB
        strings = sections[section[6]]
        for pos in range(section[4], section[4] + section[5], section[9]):
            name, value, *_ = struct.unpack_from(endian + "IIIBBH", data, pos)
            start = strings[4] + name
            result[data[start : data.index(b"\0", start)].decode()] = value
    return result


def hud_display_list(header, revision):
    fields = re.findall(r"std::uint32_t\s+(\w+)\s*;", header.split("struct AddressTable {", 1)[1].split("};", 1)[0])
    table = header.split("AddressTable kUs" + revision.upper() + "{", 1)[1].split("};", 1)[0]
    values = [int(value, 16) for value in re.findall(r"0x([0-9A-Fa-f]+)U", table)]
    assert len(fields) == len(values)
    return dict(zip(fields, values))["HudDisplayList"]


def instruction(path, address):
    data = pathlib.Path(path).read_bytes()
    assert data[:5] == b"\x7fELF\x01"
    endian = ">" if data[5] == 2 else "<"
    offset = struct.unpack_from(endian + "I", data, 32)[0]
    size, count = struct.unpack_from(endian + "HH", data, 46)
    for i in range(count):
        section = struct.unpack_from(endian + "10I", data, offset + i * size)
        _, kind, _, start, pos, length, *_ = section
        if kind == 1 and start <= address < start + length:
            return struct.unpack_from(endian + "I", data, pos + address - start)[0]
    raise AssertionError(hex(address))


assert len(sys.argv) == 6, "generated v77, generated v80, ELF v77, ELF v80, baseline source ZIP"
runtime = (ROOT / "runtime-recomp/src/game/runtime_hud_layout.cpp").read_text()
minimap_begin = runtime.split("void dkr::runtime::hud::begin_minimap(", 1)[1].split("\n}", 1)[0]
minimap_end = runtime.split("void dkr::runtime::hud::end_minimap(", 1)[1].split("\n}", 1)[0]
assert "rdram && context && g_group_frame" in minimap_begin
assert "g_map_holder = revision_addresses::HudDisplayList;" in minimap_begin
assert "BeginWidget(rdram,g_map_holder,Widget::Minimap,0)" in minimap_begin
assert "EndWidget(rdram,g_map_holder,g_map_widget_active)" in minimap_end
with zipfile.ZipFile(sys.argv[5]) as baseline:

    def previous(path):
        matches = [n for n in baseline.namelist() if n.endswith(path)]
        assert len(matches) == 1, (path, matches)
        return baseline.read(matches[0]).decode("utf-8-sig").replace("\r\n", "\n")

    for rev, generated, elf in zip(("v77", "v80"), sys.argv[1:3], sys.argv[3:5]):
        header = "runtime-recomp/src/game/revision_addresses.hpp"
        retail = symbols(elf)
        current_holder = hud_display_list((ROOT / header).read_text(), rev)
        assert current_holder == retail["gHudDL"], (rev, hex(current_holder), hex(retail["gHudDL"]))
        assert current_holder != retail["gHudMtx"]
        print(rev, "minimap command holder matches retail gHudDL:", hex(current_holder))
        path = f"runtime-recomp/dkr.us.{rev}.recomp-policy.json"
        policy = json.loads((ROOT / path).read_text())
        old = json.loads(previous(path))
        # No existing hook, replacement or revision mapping was changed.
        for key, value in old.items():
            if key != "functionHooks":
                assert policy[key] == value, key
        for hook in old["functionHooks"]:
            assert hook in policy["functionHooks"], hook
        added = [h for h in policy["functionHooks"] if h not in old["functionHooks"]]
        assert len(added) == 13, (rev, len(added))
        code = "\n".join(p.read_text() for p in pathlib.Path(generated).glob("*.c"))
        for h in added:
            address = int(h["beforeVram"], 16)
            assert code.count(h["text"] + "\n    // " + h["beforeVram"] + ":") == 1, h
            op = instruction(elf, address)
            if "dkr_hud_rect_begin" in h["text"]:
                assert op == (0x27BDFFD0 if h["function"] == "texrect_draw" else 0x27BDFF50)
            elif "dkr_hud_rect_extent" in h["text"]:
                assert op == 0x8FA900C8
            elif "dkr_hud_text_begin" in h["text"]:
                assert op == 0x27BDFFD8
            elif "dkr_hud_rect_end" in h["text"] or "dkr_hud_text_end" in h["text"]:
                assert op == 0x03E00008
        print(rev, len(added), "new hooks verified; every pre-existing patch retained")
    protected = [
        "split_screen_rt64.hpp",
        "split_screen_policy.hpp",
        "postrace_viewport_rt64.hpp",
        "runtime_netplay.cpp",
        "runtime_input.cpp",
        "rt64_renderer.cpp",
        "runtime_enhancements.cpp",
    ]
    for name in protected:
        path = "runtime-recomp/src/game/" + name
        if not (ROOT / path).exists():
            continue
        assert (ROOT / path).read_text() == previous(path), "Unrelated change: " + path
    path = "runtime-recomp/src/game/hud_layout_policy.hpp"
    current = (ROOT / path).read_text().replace(", FitToViewport, Custom", ", FitToViewport")
    current = current.replace("// Legacy numeric values are persistent: SafeArea must never become Custom.\n", "")
    assert current.strip() == previous(path).strip(), "Protected quadrant HUD policy changed"
print("Protected world/online/3-4-player policies remain byte-for-byte unchanged")
