"""Keep the list of files that read the mutable revision aliases explicit and shrinking.

revision_addresses.hpp keeps one mutable alias per address (GameMode,
CurrentRngSeed, ...) that select() rewrites field by field. New code reads the
selected table through revision_addresses::current() instead, which is one
immutable table behind one atomic pointer.

The files in REMAINING still read aliases. They are whole-file sha256 pinned
by scripts/generate_replay_probe.py (or its siblings), so touching one means
re-pinning it deliberately, and they move in their own reviewed slice. This
test fails when:

* a file outside REMAINING reads an alias (use current() instead);
* a file reads more aliases than its recorded count (the list may only shrink);
* a REMAINING entry no longer reads any alias (delete the entry);
* a REMAINING entry is not hash-pinned (the reason it is deferred is gone).

Once REMAINING is empty the aliases and their select() assignments can go.
"""

from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]
HEADER = ROOT / "runtime-recomp/src/game/revision_addresses.hpp"
SCAN_DIRS = ("runtime-recomp/src", "runtime-recomp/tests")
SUFFIXES = {".cpp", ".hpp", ".inl", ".c", ".h", ".mm"}
PIN_TABLES = (
    "scripts/generate_replay_probe.py",
    "scripts/character_portrait_pipeline.py",
    "runtime-recomp/tests/replay_probe/test_generator.py",
)

# File -> number of alias reads it may still contain (a ceiling, never a target).
REMAINING = {
    "runtime-recomp/src/game/custom_music.cpp": 10,
    "runtime-recomp/src/game/custom_tracks_hooks.cpp": 7,
    "runtime-recomp/src/game/presentation_identity.cpp": 21,
    "runtime-recomp/src/game/runtime_enhancements.cpp": 14,
    "runtime-recomp/src/game/runtime_hud_layout.cpp": 19,
    "runtime-recomp/src/game/runtime_magic_codes.cpp": 2,
    "runtime-recomp/src/game/runtime_netplay.cpp": 27,
    "runtime-recomp/src/game/runtime_stubs.cpp": 28,
    "runtime-recomp/src/game/water_profile.cpp": 1,
}

# Files that read aliases on purpose.
EXEMPT = {
    # The header itself, and the test that proves every alias equals current().
    "runtime-recomp/src/game/revision_addresses.hpp",
    "runtime-recomp/tests/revision_addresses_current_tests.cpp",
}


def alias_names(header_text=None):
    text = HEADER.read_text() if header_text is None else header_text
    return re.findall(r"^inline std::uint32_t (\w+) = kUsV77\.\w+;", text, re.M)


def strip_comments_and_strings(text):
    text = re.sub(r"/\*.*?\*/", lambda m: "\n" * m.group(0).count("\n"), text, flags=re.S)
    text = re.sub(r'"(?:\\.|[^"\\\n])*"', '""', text)
    return re.sub(r"//[^\n]*", "", text)


def count_alias_reads(text, names):
    """Count reads of the aliases in one source file.

    Three spellings read an alias: `revision_addresses::Name` (or through a
    `namespace x = ...revision_addresses;` alias), a using-declaration for one
    name, and a bare `Name` in a file that says `using namespace
    ...revision_addresses`.
    """
    code = strip_comments_and_strings(text)
    alternatives = "|".join(map(re.escape, names))
    namespaces = {"revision_addresses"}
    namespaces |= set(re.findall(r"namespace\s+(\w+)\s*=\s*(?:::)?(?:\w+::)*revision_addresses\s*;", code))
    qualifier = "|".join(sorted(map(re.escape, namespaces), key=len, reverse=True))
    count = len(re.findall(r"\b(?:%s)::(?:%s)\b(?!\s*\()" % (qualifier, alternatives), code))
    # A using-declaration is already counted above; the bare uses it enables are not.
    for name in re.findall(r"using\s+(?:\w+::)*revision_addresses::(%s)\s*;" % alternatives, code):
        count += len(re.findall(r"(?<![\w:.>])%s\b(?!\s*\()" % re.escape(name), code))
    if re.search(r"using\s+namespace\s+(?:\w+::)*revision_addresses\s*;", code):
        count += len(re.findall(r"(?<![\w:.>])(?:%s)\b(?!\s*\()" % alternatives, code))
    return count


def scan():
    names = alias_names()
    counts = {}
    for directory in SCAN_DIRS:
        for path in sorted((ROOT / directory).rglob("*")):
            if path.suffix not in SUFFIXES or not path.is_file():
                continue
            relative = path.relative_to(ROOT).as_posix()
            if relative in EXEMPT:
                continue
            reads = count_alias_reads(path.read_text(errors="replace"), names)
            if reads:
                counts[relative] = reads
    return counts


class AliasReadTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.counts = scan()

    def test_only_the_listed_files_read_aliases(self):
        unlisted = {path: n for path, n in self.counts.items() if path not in REMAINING}
        self.assertEqual({}, unlisted, "read revision_addresses::current().<Field> instead of the alias")

    def test_the_list_only_shrinks(self):
        grown = {
            path: (self.counts[path], ceiling)
            for path, ceiling in REMAINING.items()
            if self.counts.get(path, 0) > ceiling
        }
        self.assertEqual({}, grown)

    def test_no_stale_entries(self):
        stale = sorted(path for path in REMAINING if self.counts.get(path, 0) == 0)
        self.assertEqual([], stale, "these files no longer read aliases; remove them from REMAINING")

    def test_ceilings_are_tight(self):
        slack = {
            path: (self.counts[path], ceiling)
            for path, ceiling in REMAINING.items()
            if self.counts.get(path, 0) < ceiling
        }
        self.assertEqual({}, slack, "lower the ceiling to the current count")

    def test_every_remaining_file_is_hash_pinned(self):
        pins = "\n".join((ROOT / table).read_text() for table in PIN_TABLES)
        for path in REMAINING:
            with self.subTest(path=path):
                self.assertIn(f'"{path}"', pins)


class ScannerTests(unittest.TestCase):
    NAMES = ["GameMode", "CurrentRngSeed"]

    def test_qualified_and_aliased_reads(self):
        text = """
        namespace a = dkr::runtime::revision_addresses;
        void f() { use(a::GameMode); use(revision_addresses::CurrentRngSeed); use(a::current().GameMode); }
        """
        self.assertEqual(2, count_alias_reads(text, self.NAMES))

    def test_using_namespace_counts_bare_names(self):
        text = """
        using namespace dkr::runtime::revision_addresses;
        void f() { use(GameMode); use(x.GameMode); use(other::GameMode); use(current().GameMode); }
        """
        self.assertEqual(1, count_alias_reads(text, self.NAMES))

    def test_bare_names_without_the_directive_are_not_reads(self):
        self.assertEqual(0, count_alias_reads("void f() { use(GameMode); }", self.NAMES))

    def test_comments_and_strings_do_not_count(self):
        text = 'using namespace revision_addresses; // GameMode\nconst char* s = "GameMode"; /* GameMode */'
        self.assertEqual(0, count_alias_reads(text, self.NAMES))

    def test_using_declaration_counts_itself_and_the_bare_uses_it_enables(self):
        self.assertEqual(1, count_alias_reads("using dkr::runtime::revision_addresses::GameMode;", self.NAMES))
        text = "using dkr::runtime::revision_addresses::GameMode;\nvoid f() { use(GameMode); use(CurrentRngSeed); }"
        self.assertEqual(2, count_alias_reads(text, self.NAMES))


if __name__ == "__main__":
    unittest.main()
