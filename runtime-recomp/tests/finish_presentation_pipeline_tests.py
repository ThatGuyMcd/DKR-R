"""Verify both new hooks against generated placement AND original ELF opcodes."""

import json
import pathlib
import struct
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]


def instruction(elf, address):
    data = pathlib.Path(elf).read_bytes()
    assert data[:5] == b"\x7fELF\x01"
    endian = ">" if data[5] == 2 else "<"
    offset = struct.unpack_from(endian + "I", data, 32)[0]
    size, count = struct.unpack_from(endian + "HH", data, 46)
    for index in range(count):
        section = struct.unpack_from(endian + "10I", data, offset + index * size)
        _, kind, _, start, pos, length, *_ = section
        if kind == 1 and start <= address < start + length:
            return struct.unpack_from(endian + "I", data, pos + address - start)[0]
    raise AssertionError(f"instruction not found: {address:x}")


expected = {"v77": (0x80058DC4, 0x800955F0), "v80": (0x80058E04, 0x80095AF4)}
assert len(sys.argv) == 5, "generated v77, generated v80, ELF v77, ELF v80"
for (rev, anchors), generated_dir, elf in zip(expected.items(), sys.argv[1:3], sys.argv[3:5], strict=True):
    policy = json.loads((ROOT / f"dkr.us.{rev}.recomp-policy.json").read_text())
    hooks = policy["functionHooks"]
    generated = "\n".join(p.read_text() for p in pathlib.Path(generated_dir).glob("*.c"))
    for symbol, function, address in zip(
        ("dkr_presentation_finish_camera_node", "dkr_postrace_wooden_frame_draw"),
        ("update_camera_finish_race", "postrace_viewport"),
        anchors,
        strict=True,
    ):
        matches = [h for h in hooks if symbol in h["text"]]
        assert len(matches) == 1
        hook = matches[0]
        assert hook["function"] == function and int(hook["beforeVram"], 16) == address
        assert generated.count(hook["text"] + "\n    // " + hook["beforeVram"] + ":") == 1
    assert instruction(elf, anchors[0]) == 0x8E0A0000, "lw t2,0(s0): actual camera pointer"
    assert instruction(elf, anchors[1]) >> 26 == 3, "actual wooden-frame JAL"
    assert instruction(elf, anchors[1] + 4) == 0x24040004, "delay slot selects menu image 4"
    for obsolete in ("dkr_presentation_finish_camera_enter", "dkr_presentation_viewport_camera_mode"):
        assert obsolete not in generated
        assert all(obsolete not in h["text"] for h in hooks)
    postrace = [h for h in hooks if "dkr_postrace_presentation_start(" in h["text"]]
    assert len(postrace) == 1 and postrace[0]["function"] == "postrace_start"
    assert postrace[0]["text"].startswith(
        "extern void dkr_netplay_postrace_barrier(uint8_t*, recomp_context*); dkr_netplay_postrace_barrier(rdram, ctx);"
    )
    print(f"[test][finish-pipeline] {rev}: ELF instructions, unique hooks, obsolete disables removed PASS")
