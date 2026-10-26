#!/usr/bin/env python3
"""The owned rollback contracts stay frozen at the v1.0.11 values: the
configure emits exactly those, the contract doc records them, and no source
hash chain feeds them again."""

from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
RUNTIME = ROOT / "runtime-recomp"
HEADER = RUNTIME / "src/game/netplay/legacy_identity.hpp"
DOC = ROOT / "docs/contracts/ONLINE_IDENTITY.md"
# What v1.0.11 shipped (docs/fold-in/REFERENCE.md). Changing these is a
# protocol break with every v1.0.11 peer, never a refactor.
SHIPPED = {
    "77": "5e22da8e6095b362ede17c69a2341aae2df24cbf8561c3af57fc86566d11784e",
    "80": "32c82fea3111440434926d5782e174c9f2d036d198f039c5424393a835f948b0",
}


class OnlineIdentityTests(unittest.TestCase):
    def test_header_holds_the_shipped_contracts(self):
        text = HEADER.read_text(encoding="utf-8")
        for revision, value in SHIPPED.items():
            with self.subTest(revision=revision):
                found = re.findall(rf'^#define DKR_LEGACY_OWNED_BUILD_ID_V{revision} "([0-9a-f]+)"$', text, re.M)
                self.assertEqual(found, [value])

    @unittest.skipUnless(shutil.which("cmake"), "needs cmake")
    def test_configure_emits_the_shipped_contracts(self):
        with tempfile.TemporaryDirectory() as scratch:
            script = Path(scratch) / "emit.cmake"
            script.write_text(
                f'set(CMAKE_CURRENT_LIST_DIR "{(RUNTIME / "cmake").as_posix()}")\n'
                f'include("{(RUNTIME / "cmake/OnlineIdentity.cmake").as_posix()}")\n'
                'message("V77=${DKR_OWNED_BUILD_ID_V77}")\nmessage("V80=${DKR_OWNED_BUILD_ID_V80}")\n',
                encoding="utf-8",
            )
            done = subprocess.run(["cmake", "-P", str(script)], capture_output=True, text=True, check=True)
        emitted = dict(re.findall(r"^V(77|80)=(\S*)$", done.stderr, re.M))
        self.assertEqual(emitted, SHIPPED)

    def test_contract_doc_matches(self):
        text = DOC.read_text(encoding="utf-8")
        for revision, value in SHIPPED.items():
            with self.subTest(revision=revision):
                self.assertRegex(text, rf"\| `DKR_OWNED_BUILD_ID_V{revision}` \| `{value}` \|")

    def test_no_source_hash_feeds_the_contracts(self):
        cmake = (RUNTIME / "CMakeLists.txt").read_text(encoding="utf-8")
        self.assertNotIn("string(SHA256 DKR_OWNED_BUILD_ID_V", cmake)
        self.assertNotIn("dkr_owned_content_identity(", cmake)
        self.assertIn("include(cmake/OnlineIdentity.cmake)", cmake)


if __name__ == "__main__":
    unittest.main()
