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

    def test_only_online_identity_cmake_writes_the_contracts(self):
        # A bare DKR_OWNED_BUILD_ID_V77/V80 word in a CMake command is the
        # variable being written (set, string(SHA256 ...), file(SHA256 ...),
        # list, ...); reads are ${...} and compile definitions are NAME=.
        # Only OnlineIdentity.cmake may write them, so no glob or file hash
        # can feed them again. Clearing v80 without the Rev A engine is the
        # one exception.
        written = re.compile(r"(?<![{\w])DKR_OWNED_BUILD_ID_V(?:77|80|\$\{\w+\})(?![\w}=])")
        command = re.compile(r"\b(\w+)\s*\(((?:[^()]|\([^()]*\))*)\)")
        allowed = RUNTIME / "cmake/OnlineIdentity.cmake"
        offenders = []
        for path in sorted([*RUNTIME.rglob("*.cmake"), *RUNTIME.rglob("CMakeLists.txt")]):
            if path == allowed:
                continue
            for match in command.finditer(path.read_text(encoding="utf-8", errors="replace")):
                args = " ".join(match.group(2).split())
                if written.search(args) and f"{match.group(1)}({args})" != 'set(DKR_OWNED_BUILD_ID_V80 "")':
                    offenders.append(f"{path.relative_to(ROOT)}: {match.group(1)}({args[:100]})")
        self.assertEqual(offenders, [])

    def test_no_source_hash_feeds_the_contracts(self):
        cmake = (RUNTIME / "CMakeLists.txt").read_text(encoding="utf-8")
        self.assertNotIn("string(SHA256 DKR_OWNED_BUILD_ID_V", cmake)
        self.assertNotIn("dkr_owned_content_identity(", cmake)
        self.assertIn("include(cmake/OnlineIdentity.cmake)", cmake)


if __name__ == "__main__":
    unittest.main()
