"""Audit both retail-specific Magic Code hook anchors and generated placement."""

import json
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
EXPECTED = {
    "v77": ("0x8008A484", "0x8008E2F4", "0x8006C90C"),
    "v80": ("0x8008A934", "0x8008E7AC", "0x8006CB4C"),
}

for revision, generated_path in zip(EXPECTED, sys.argv[1:], strict=True):
    policy = json.loads((ROOT / f"dkr.us.{revision}.recomp-policy.json").read_text())
    hooks = policy["functionHooks"]
    keys = [(h["function"], h["beforeVram"]) for h in hooks]
    assert len(keys) == len(set(keys)), f"duplicate hook anchors in {revision}"
    generated = "\n".join(p.read_text() for p in pathlib.Path(generated_path).glob("*.c"))
    for function, symbol, address in zip(
        ("menu_magic_codes_loop", "menu_file_select_loop", "main_game_loop"),
        ("dkr_magic_code_credits_started", "dkr_magic_code_balloon_awarded", "dkr_magic_codes_frame_complete"),
        EXPECTED[revision],
        strict=True,
    ):
        matches = [h for h in hooks if symbol in h["text"]]
        assert len(matches) == 1, (revision, symbol)
        hook = matches[0]
        assert (hook["function"], hook["beforeVram"]) == (function, address), hook
        needle = hook["text"] + "\n    // " + address + ":"
        assert generated.count(needle) == 1, f"missing or duplicate generated hook {symbol}"
    # Preserve the mode-filter implementation. Do not 'fix' intentional retail
    # Adventure/time-trial/challenge exclusions by bypassing get_filtered_cheats.
    assert all(h["function"] != "get_filtered_cheats" for h in hooks)
    assert all(p["function"] != "get_filtered_cheats" for p in policy["instructionPatches"])
    balloon_anchor = generated.index(
        "extern void dkr_magic_code_balloon_awarded(uint8_t*, recomp_context*); "
        "dkr_magic_code_balloon_awarded(rdram, ctx);"
    )
    assert "sh          $t4, 0x0($v0)" in generated[balloon_anchor : balloon_anchor + 280]
    assert "MEM_H(0X0, ctx->r2) = ctx->r12;" in generated[balloon_anchor : balloon_anchor + 280]
    print(f"[test][magic-code-pipeline] {revision}: unique native success and commit anchors PASS")
