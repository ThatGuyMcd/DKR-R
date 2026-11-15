"""revision_addresses.hpp keeps no mutable per-address aliases.

The selected revision's addresses are read through revision_addresses::current(),
one atomic pointer to an immutable table. There used to be a mutable alias per
address (GameMode, CurrentRngSeed, ...) that select() rewrote field by field;
every reader has moved, so a reintroduced alias, or a select() that writes
fields again, fails here.
"""

from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]
HEADER = ROOT / "runtime-recomp/src/game/revision_addresses.hpp"


class NoMutableAliases(unittest.TestCase):
    def setUp(self):
        self.text = HEADER.read_text()

    def test_no_mutable_address_variables(self):
        self.assertEqual([], re.findall(r"^inline (?!const|constexpr)std::uint32_t \w+", self.text, re.M))

    def test_select_publishes_only_the_revision_and_the_table_pointer(self):
        body = self.text[self.text.index("inline bool select(") :]
        body = body[: body.index("\n}\n")]
        self.assertEqual([], re.findall(r"^\s+\w+ = table\.\w+;", body, re.M))
        self.assertIn("gSelectedRevision.store(", body)
        self.assertIn("gCurrentTable.store(&table, std::memory_order_release)", body)

    def test_the_revision_and_the_table_are_atomic(self):
        self.assertIn("std::atomic<const AddressTable*> gCurrentTable", self.text)
        self.assertIn("std::atomic<rom::Revision> gSelectedRevision", self.text)


if __name__ == "__main__":
    unittest.main()
