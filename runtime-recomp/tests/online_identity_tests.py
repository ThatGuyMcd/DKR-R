#!/usr/bin/env python3
"""The owned rollback contracts stay frozen at the approved v1.0.12 values: the
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
# Shipped v1.0.12 values, verified against official Windows/Linux artifacts.
# Both owners approved their adoption; see docs/notes/2026-10-31-v1012-identity-proposal.md.
SHIPPED = {
    "77": "dc883843594ccb02931dbab4071080c0dcf0d7c0a960ee501529dd37230651c7",
    "80": "105df80b777b20df308fede4235575dce5881e643130ebabf8cc60d71ff000e0",
}
# The digests v1.0.11 appended to the owned adapter identity, in order
# (experimental_runtime.cpp runtime_engine_identity).
ENGINE_SALTS = (
    "dcb1e8df777d341f2e96d4ee78cdc4337d32a6ddc86b4b60dbcc46fabfd31efc",
    "893bbedb02a3c87771e75befab107a2ce54641073742be69e97b995a9d0a3bbf",
    "2a82142dc00619b29b80f5f0b6485c3ffe8c0a33e89a7a6c48d0a6ac8eeb4a66",
    "c4d77b07ed3b7f9aabcf7ce1430780fc65a1ce086e62b66a0af672aac40eb5ac",
)


class OnlineIdentityTests(unittest.TestCase):
    def test_header_holds_the_shipped_contracts(self):
        text = HEADER.read_text(encoding="utf-8")
        for revision, value in SHIPPED.items():
            with self.subTest(revision=revision):
                found = re.findall(rf'^#define DKR_LEGACY_OWNED_BUILD_ID_V{revision} "([0-9a-f]+)"$', text, re.M)
                self.assertEqual(found, [value])

    def test_header_holds_the_shipped_engine_salts(self):
        text = HEADER.read_text(encoding="utf-8")
        found = re.findall(r'^#define DKR_LEGACY_ENGINE_SALT_(\d) "([0-9a-f]+)"$', text, re.M)
        self.assertEqual(found, [(str(i), salt) for i, salt in enumerate(ENGINE_SALTS, 1)])

    def test_engine_salts_are_defined_only_in_the_header(self):
        # Each salt value appears in source only in legacy_identity.hpp, and the
        # engine identity appends all four by name, in order.
        offenders = []
        for path in sorted(RUNTIME.rglob("*")):
            if path == HEADER or not path.is_file() or path.suffix not in (".c", ".cc", ".cpp", ".h", ".hpp", ".inl"):
                continue
            text = path.read_text(encoding="utf-8", errors="replace")
            offenders += [f"{path.relative_to(ROOT)}: {salt[:12]}" for salt in ENGINE_SALTS if salt in text]
        self.assertEqual(offenders, [])
        runtime = (RUNTIME / "src/game/netplay/experimental_runtime.cpp").read_text(encoding="utf-8")
        uses = re.findall(r"base\s*\+=\s*(DKR_LEGACY_ENGINE_SALT_\d);", runtime)
        self.assertEqual(uses, [f"DKR_LEGACY_ENGINE_SALT_{i}" for i in range(1, 5)])

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
