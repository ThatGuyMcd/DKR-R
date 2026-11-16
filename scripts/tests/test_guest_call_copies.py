"""Keep raw recomp_context copies and hand-written f_odd repairs from spreading.

recomp_context::f_odd points inside the context that owns it, so a copied
context needs f_odd repaired before it reaches guest code (see
runtime-recomp/src/game/guest_call_context.hpp). Native-to-guest calls build
their context with GuestCallContext, which does that in one place.

This test lists the files that still spell `f_odd =`, copy a context by value
(`recomp_context x = y`, `auto x = *ctx`, `x = *ctx`), outside that header, with
a ceiling each, and fails when:

* a file outside the lists does either (use GuestCallContext);
* a file does it more often than its ceiling (the counts may only shrink; lower
  the ceiling when you convert a site);
* a file that stores a recomp_context (a member or global in runtime-recomp/src that
  outlives its source) does not repair f_odd where it uses it.

Scanned: runtime-recomp/src, guest, guest-hooks, tests/replay_probe (the owned
engine's C and C++) and scripts (which emit guest C). Other tests build fresh
contexts as fixtures and are not scanned.

Why each remaining file is allowed is in WHY below. Files that also appear in
generate_replay_probe.py's pins, or are identity inputs, change only in a
reviewed slice of their own.
"""

from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]
SCAN = (
    "runtime-recomp/src",
    "runtime-recomp/guest",
    "runtime-recomp/guest-hooks",
    "runtime-recomp/tests/replay_probe",
    "scripts",
)
SUFFIXES = {".cpp", ".hpp", ".inl", ".c", ".h", ".inc", ".toml", ".py"}
EXEMPT = {
    "runtime-recomp/src/game/guest_call_context.hpp",  # the one implementation
}

F_ODD_SET = re.compile(r"\bf_odd\s*=[^=]")
COPY_INIT = re.compile(
    r"\brecomp_context\s+\w+\s*=[ \t]*(?![{0 \t]|\*\s*(?:ctx|context)\s*;)[^;]|\bauto\s+\w+\s*=\s*saved\s*[;,]"
)
DEREF_COPY = re.compile(r"=\s*\*\s*(?:ctx|context)\s*[,;]")
STORED = re.compile(r"^\s*recomp_context\s+\w+\s*(?:\{\s*\})?\s*;|^\s*recomp_context\s+\w+\s*\{\s*\}\s*;", re.M)
REPAIR = re.compile(r"repair_float_(?:odd|register)_pointer|GuestCallContext|\bf_odd\s*=[^=]")

# File -> ceiling on (f_odd assignments + by-value context copies).
ALLOWED = {
    # Fresh zeroed contexts for the private draw-only kernels (C, so no GuestCallContext).
    "runtime-recomp/src/game/camera_kernel.c": 1,
    "runtime-recomp/src/game/visual_model_kernel.c": 1,
    # Guest C emitted into the payload: copy then repair inline, or (hooks.toml)
    # copy without repair, which changes payload bytes and identity.
    "runtime-recomp/guest/world_portrait.inc": 2,
    "runtime-recomp/guest-hooks/hooks.toml": 15,
    "scripts/legacy_memory_safety_policy.py": 4,
    "scripts/save_layout_pipeline.py": 2,
    "scripts/generate_replay_probe.py": 1,
    # The owned engine's own C and C++ (identity inputs): copy then repair.
    "runtime-recomp/tests/replay_probe/probe_audio_events.c": 2,
    "runtime-recomp/tests/replay_probe/probe_native.c": 3,
    "runtime-recomp/tests/replay_probe/probe_driver.cpp": 1,
    # Restores a context from a stored mode, then repairs it.
    "runtime-recomp/src/game/netplay/runtime_state.cpp": 1,
    # A stored context, repaired where it is used (see STORED below).
    "runtime-recomp/src/game/replay_probe_capture.cpp": 1,
    # Mod code, which the mod guest-call change converts; its ceilings fall with it.
    "runtime-recomp/src/game/runtime_legacy_mods.cpp": 18,
    "runtime-recomp/src/game/mods/legacy_character_menu_render.cpp": 2,
    "runtime-recomp/src/game/mods/legacy_character_presentation.cpp": 2,
    "runtime-recomp/src/game/mods/legacy_character_stage.cpp": 3,
    "runtime-recomp/src/game/mods/legacy_runtime_character.cpp": 2,
    "runtime-recomp/src/game/mods/magic_code_game_world.cpp": 1,
}


def count_copies(text: str) -> int:
    return len(F_ODD_SET.findall(text)) + len(COPY_INIT.findall(text)) + len(DEREF_COPY.findall(text))


def stored_contexts(text: str) -> int:
    return len(STORED.findall(text))


def scan() -> tuple[dict[str, int], dict[str, str]]:
    counts: dict[str, int] = {}
    texts: dict[str, str] = {}
    for directory in SCAN:
        for path in sorted((ROOT / directory).rglob("*")):
            if path.suffix not in SUFFIXES or not path.is_file():
                continue
            relative = path.relative_to(ROOT).as_posix()
            if relative in EXEMPT or "/tests/" in relative and "replay_probe" not in relative:
                continue
            text = path.read_text(errors="replace")
            texts[relative] = text
            found = count_copies(text)
            if found:
                counts[relative] = found
    return counts, texts


class CopyGuard(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.counts, cls.texts = scan()

    def test_only_listed_files_copy_or_repair_by_hand(self):
        unlisted = {path: n for path, n in self.counts.items() if path not in ALLOWED}
        self.assertEqual({}, unlisted, "build the context with GuestCallContext instead")

    def test_the_counts_only_shrink(self):
        grown = {p: (self.counts[p], c) for p, c in ALLOWED.items() if self.counts.get(p, 0) > c}
        self.assertEqual({}, grown)

    def test_stored_contexts_repair_where_they_are_used(self):
        for path, text in self.texts.items():
            if path.startswith("runtime-recomp/src/") and stored_contexts(text):
                with self.subTest(path=path):
                    self.assertRegex(text, REPAIR, f"{path} stores a recomp_context but never repairs f_odd")


class ScannerTests(unittest.TestCase):
    def test_f_odd_assignment(self):
        self.assertEqual(1, count_copies("c.f_odd = c.mips3_float_mode ? &c.f1.u32l : &c.f0.u32h;"))
        self.assertEqual(0, count_copies("check(c.f_odd == p);"))

    def test_by_value_copies(self):
        self.assertEqual(1, count_copies("recomp_context call = *ctx;"))
        self.assertEqual(2, count_copies("auto a = *ctx, b = *ctx;"))
        self.assertEqual(1, count_copies("parked = *context;"))

    def test_fresh_contexts_and_references_are_not_copies(self):
        self.assertEqual(0, count_copies("recomp_context ctx = {0}; recomp_context c{}; const recomp_context& r = x;"))

    def test_guest_call_context_is_not_a_raw_copy(self):
        self.assertEqual(0, count_copies("dkr::runtime::GuestCallContext call(*ctx);"))

    def test_stored_contexts(self):
        self.assertEqual(1, stored_contexts("recomp_context parked_cpu{};"))
        self.assertEqual(1, stored_contexts("    recomp_context saved;"))
        self.assertEqual(0, stored_contexts("void f(recomp_context* c);"))


if __name__ == "__main__":
    unittest.main()
