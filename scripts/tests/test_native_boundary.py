"""The recorded owned-engine native boundary matches the sources, and the rules fire.

scripts/native_boundary.py compares per-function guest stores, guest calls, hooks
and a few token digests of the pinned first-party sources with
architecture/owned-native-boundary.toml. This runs it on the tree (no ROM, no
extern) and seeds one violation per rule into a real or synthetic source to
show the rules fire, and that layout and comments don't.
"""

from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))

import character_portrait_pipeline as portrait  # noqa: E402
import generate_replay_probe as probe  # noqa: E402
import native_boundary as nb  # noqa: E402

GUEST = nb.Guest(frozenset({"get_settings", "cam_set_fov"}), frozenset({"get_settings"}))
VOCABULARY = nb.Vocabulary(frozenset({"write_word"}), frozenset({"write"}))

BASE = b"""
namespace dkr {
namespace {
std::uint32_t read_word(std::uint8_t* rdram, std::uint32_t a);
}
extern "C" {
void dkr_hook(std::uint8_t* rdram, recomp_context* ctx) {
    const auto x = MEM_W(0, ctx->r4);
    if (x == 1) {
        MEM_W(0, ctx->r5) = 2;
    }
    auto again = [&](int v) { MEM_B(1, ctx->r6) += v; };
    again(1);
}
}
void helper(std::uint8_t* rdram) { write_word(rdram, 4, 5); }
class Box {
  public:
    void method(const Guest& g) const { g.write(1, 2); }
};
} // namespace dkr
"""


def facts(source: bytes = BASE) -> nb.Facts:
    return nb.analyse(source, GUEST, VOCABULARY)


def entry_for(source: bytes = BASE, digests: tuple[str, ...] = ()) -> nb.Entry:
    found = facts(source)
    return nb.Entry(
        "synthetic.cpp",
        found.stores,
        found.guest_calls,
        found.hooks,
        [],
        {name: nb.token_digest(found.function_tokens[name]) for name in digests},
    )


class TreeTests(unittest.TestCase):
    def test_the_record_matches_the_tree(self):
        self.assertEqual([], nb.check())

    def test_every_pinned_first_party_file_is_recorded(self):
        recorded = {entry.path for entry in nb.load_record().entries}
        pinned: set[str] = set()
        for table in (probe.INPUT_NATIVE_HASH, probe.FULL_SCENE_NATIVE_HASH, probe.REVIEWED_MAIN_MERGE_HASH):
            pinned |= {path for path in table if not path.startswith("extern/")}
        pinned |= {f"runtime-recomp/src/game/mods/{name}" for name in portrait.NATIVE_FILES}
        self.assertEqual([], sorted(pinned - recorded))
        for path in recorded:
            self.assertTrue((ROOT / path).is_file(), path)


class ParserTests(unittest.TestCase):
    def test_functions_found_through_namespaces_extern_c_classes_and_lambdas(self):
        names = [f.name for f in nb.functions(nb.tokens(BASE))]
        self.assertEqual(["dkr_hook", "helper", "method"], names)

    def test_a_lambda_store_belongs_to_the_enclosing_function(self):
        self.assertEqual({"dkr_hook": 2, "helper": 1, "method": 1}, facts().stores)

    def test_reads_and_comparisons_are_not_stores(self):
        source = b"void f(std::uint8_t* r){ auto a = MEM_W(0, 4); if (MEM_W(0, 8) == 3 || MEM_B(0, 1) >= 2) {} }"
        self.assertEqual({}, facts(source).stores)

    def test_hooks_are_extern_c_dkr_and_recomp_definitions(self):
        source = (
            b'extern "C" void dkr_a(); extern "C" void dkr_b() {}\n'
            b'extern "C" { void x_recomp() {} void other() {} }\nvoid dkr_c() {}'
        )
        self.assertEqual(["dkr_b", "x_recomp"], facts(source).hooks)

    def test_guest_calls_by_payload_member_pointer_and_plain_name(self):
        source = (
            b"void f(){ payload->get_settings(rdram, &c); use(p->get_settings); "
            b"cam_set_fov(rdram, c); other->cam_set_fov(); x.y(); }"
        )
        self.assertEqual(["cam_set_fov", "get_settings"], facts(source).guest_calls)


class RuleTests(unittest.TestCase):
    def problems(self, source: bytes, digests: tuple[str, ...] = ()) -> list[str]:
        return nb.compare(entry_for(BASE, digests), facts(source), set())

    def test_the_unchanged_source_is_clean(self):
        self.assertEqual([], self.problems(BASE))

    def test_layout_and_comments_do_not_matter(self):
        reflowed = BASE.replace(b"if (x == 1) {", b"// audited\n    if ( x == 1 )\n    {") + b"\n/* trailing */\n"
        self.assertEqual([], self.problems(reflowed, ("dkr_hook",)))

    def test_a_refactor_that_keeps_the_facts_is_clean(self):
        renamed = BASE.replace(b"const auto x", b"const auto value").replace(b"x == 1", b"value == 1")
        self.assertEqual([], self.problems(renamed))

    def test_new_store_in_an_existing_function(self):
        changed = BASE.replace(b"again(1);", b"again(1); MEM_W(0, ctx->r7) = 9;")
        self.assertEqual(["synthetic.cpp: guest stores in dkr_hook: recorded 2, found 3"], self.problems(changed))

    def test_new_store_through_a_vocabulary_call_and_member(self):
        changed = BASE.replace(
            b"void helper(std::uint8_t* rdram) {", b"void helper(std::uint8_t* rdram) { write_word(rdram, 1, 2);"
        )
        self.assertEqual(["synthetic.cpp: guest stores in helper: recorded 1, found 2"], self.problems(changed))
        changed = BASE.replace(b"g.write(1, 2);", b"g.write(1, 2); g.write(3, 4);")
        self.assertEqual(["synthetic.cpp: guest stores in method: recorded 1, found 2"], self.problems(changed))

    def test_address_of_a_guest_macro_counts(self):
        changed = BASE.replace(b"again(1);", b"again(1); auto& ref = MEM_W(0, ctx->r7);")
        self.assertEqual(3, facts(changed).stores["dkr_hook"])

    def test_new_guest_call(self):
        changed = BASE.replace(b"again(1);", b"again(1); get_settings(rdram, ctx);")
        self.assertEqual(["synthetic.cpp: guest function get_settings newly called"], self.problems(changed))

    def test_new_and_removed_hook(self):
        added = BASE.replace(b"void helper", b'extern "C" void dkr_new_hook() {}\nvoid helper')
        self.assertEqual(['synthetic.cpp: extern "C" hook dkr_new_hook added'], self.problems(added))
        removed = BASE.replace(b"void dkr_hook(", b"void quiet_hook(")
        self.assertIn('synthetic.cpp: extern "C" hook dkr_hook removed', self.problems(removed))

    def test_probe_naming_is_cross_checked(self):
        entry = entry_for(BASE)
        self.assertEqual(
            ["synthetic.cpp: hook dkr_hook is now named in probe_native.c"],
            nb.compare(entry, facts(BASE), {"dkr_hook"}),
        )

    def test_edit_inside_a_digested_function(self):
        changed = BASE.replace(b"= 2;", b"= 3;")
        self.assertEqual([], self.problems(changed))
        self.assertEqual(["synthetic.cpp: tokens of dkr_hook changed"], self.problems(changed, ("dkr_hook",)))

    def test_missing_digested_function(self):
        changed = BASE.replace(b"void helper", b"void renamed")
        self.assertIn("synthetic.cpp: digested function helper not found", self.problems(changed, ("helper",)))


class RealSourceTests(unittest.TestCase):
    """Seed one violation per rule into a real pinned file."""

    @classmethod
    def setUpClass(cls):
        cls.record = nb.load_record()
        cls.guest = nb.guest_names()
        cls.named = nb.probe_names()

    def entry(self, name: str) -> nb.Entry:
        return next(e for e in self.record.entries if e.path.endswith("/" + name))

    def problems(self, name: str, edit) -> list[str]:
        entry = self.entry(name)
        source = edit((ROOT / entry.path).read_text(encoding="utf-8"))
        return nb.compare(entry, nb.analyse(source.encode(), self.guest, self.record.vocabulary), self.named)

    def test_a_new_store_in_runtime_stubs_is_found(self):
        def edit(text: str) -> str:
            marker = "ResumeNativeSequence"
            at = text.index("{", text.index(marker, text.index("ResumeNativeSequence(")))
            return text[: at + 1] + "\n    MEM_W(0, 0x80000000) = 1;\n" + text[at + 1 :]

        found = self.problems("runtime_stubs.cpp", edit)
        self.assertTrue(any("guest stores in ResumeNativeSequence" in p for p in found), found)
        self.assertTrue(any("tokens of ResumeNativeSequence changed" in p for p in found), found)

    def test_a_new_guest_call_in_runtime_netplay_is_found(self):
        def edit(text: str) -> str:
            at = text.index("{", text.index("call_payload_query("))
            return text[: at + 1] + "\n    payload->cam_set_fov(rdram, nullptr);\n" + text[at + 1 :]

        found = self.problems("runtime_netplay.cpp", edit)
        self.assertTrue(any("guest function cam_set_fov newly called" in p for p in found), found)

    def test_a_new_hook_in_custom_music_is_found(self):
        found = self.problems("custom_music.cpp", lambda text: text + '\nextern "C" void dkr_seeded_hook() {}\n')
        self.assertEqual(['runtime-recomp/src/game/custom_music.cpp: extern "C" hook dkr_seeded_hook added'], found)

    def test_a_comment_edit_in_a_real_file_is_silent(self):
        self.assertEqual([], self.problems("runtime_hud_layout.cpp", lambda text: "// seeded comment\n" + text))


if __name__ == "__main__":
    unittest.main()
