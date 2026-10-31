"""Read-only checks of the scoped draw Patch Pipeline on both reviewed revisions."""

import copy
import json
import os
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
from compose_legacy_mod_policy import compose_track_menu, elf_functions, elf_sections
from source_text import Source
from track_select_draw_policy import compose_track_draw


class TrackDrawPipeline(unittest.TestCase):
    def inputs(self, revision):
        default = (
            ROOT / "extern/dkr-decomp/build/dkr.us.v77.elf"
            if revision == 77
            else Path("G:/DiddykongWorkFolder/hud-fit-viewport-20260824/dkr-decomp-v80/build/dkr.us.v80.elf")
        )
        elf = Path(os.environ.get(f"DKR_REVIEWED_ELF_V{revision}", default))
        if not elf.exists():
            self.skipTest("Reviewed ELF unavailable")
        return elf, elf_sections(elf), elf_functions(elf, (1, 2))

    def test_signed_draw_sites_and_stack_table_are_bounded(self):
        for revision in (77, 80):
            elf, sections, symbols = self.inputs(revision)
            result = compose_track_draw(
                {"instructionPatches": [], "functionHooks": []}, elf, f"us.v{revision}", sections, symbols
            )
            self.assertEqual(len(result["instructionPatches"]), 13)
            self.assertEqual(len(result["functionHooks"]), 13)
            self.assertEqual(
                {p["function"] for p in result["instructionPatches"]},
                {"trackmenu_render_names", "trackmenu_render_2D", "texrect_draw_scaled", "texrect_draw"},
            )
            table = [p for p in result["instructionPatches"] if p["function"] == "trackmenu_render_names"]
            self.assertEqual(
                {int(p["value"], 0) for p in table},
                {0x27BDFE80, 0x27BD0180, 0x27B00090, 0x27B30180, 0x2416FFFE, 0x24010003},
            )
            self.assertEqual(sum(int(p["value"], 0) == 0x27B00090 for p in table), 2)
            self.assertEqual(sum("9U" in h["text"] for h in result["functionHooks"]), 1)
            original = copy.deepcopy(result)
            with self.assertRaises(ValueError):
                compose_track_draw(result, elf, f"us.v{revision}", sections, symbols)
            self.assertEqual(result, original)

    def test_custom_rows_receive_separate_fifteen_cell_storage(self):
        for revision in (77, 80):
            elf, sections, symbols = self.inputs(revision)
            fragment = json.loads(
                (ROOT / f"runtime-recomp/legacy-track-menu.v{revision}.recomp-fragment.json").read_text()
            )
            result = compose_track_menu({"instructionPatches": [], "functionHooks": []}, fragment, sections, symbols)
            hooks = [h for h in result["functionHooks"] if h["function"] == "trackmenu_render_names"]
            self.assertEqual(len(hooks), 1)
            text = hooks[0]["text"]
            self.assertIn("(uint32_t)ctx->r29 + 0x90U", text)
            self.assertIn("4U, dkr_legacy_fields, 5U", text)
            self.assertNotIn("static const uint32_t", text)

    def test_instruction_drift_and_wrong_revision_are_rejected(self):
        for revision in (77, 80):
            elf, sections, symbols = self.inputs(revision)
            with self.assertRaises(ValueError):
                compose_track_draw(
                    {"instructionPatches": [], "functionHooks": []},
                    elf,
                    f"us.v{80 if revision == 77 else 77}",
                    sections,
                    symbols,
                )
            start = next(iter(symbols["trackmenu_render_names"]))[0]
            corrupted = []
            for address, data in sections:
                changed = bytearray(data)
                if address <= start < address + len(data):
                    changed[start - address] ^= 1
                corrupted.append((address, bytes(changed)))
            with self.assertRaises(ValueError):
                compose_track_draw(
                    {"instructionPatches": [], "functionHooks": []}, elf, f"us.v{revision}", corrupted, symbols
                )

    def test_local_aspect_never_enters_cpu_helper(self):
        helper = Source((ROOT / "runtime-recomp/src/game/track_select_draw_guest.h").read_text())
        for forbidden in ("SDL", "get_graphics_config", "filesystem", "malloc", "new "):
            self.assertNotIn(forbidden, helper)
        self.assertIn("end-24U", helper)
        self.assertIn("for(unsigned i=0;i<240;i+=4)", helper)
        self.assertIn("state->pending=0", helper)


if __name__ == "__main__":
    unittest.main()
