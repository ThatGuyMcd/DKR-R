#!/usr/bin/env python3
"""scripts/guest_hooks.py on small recompiler-shaped fixtures (no ROM content):
each edit kind, the exactly-once rule, revision values, and the real spec."""

import importlib.util
from pathlib import Path
import tempfile
import textwrap
import unittest

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("guest_hooks", ROOT / "scripts/guest_hooks.py")
guest_hooks = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(guest_hooks)

GUEST = textwrap.dedent(
    """\
    #include "funcs.h"

    RECOMP_FUNC void draw_thing(uint8_t* rdram, recomp_context* ctx) {
        if (dkr_code_dispatch(rdram, ctx)) return;
        dkr_code_body_draw_thing(rdram, ctx);
    }
    static void dkr_code_body_draw_thing(uint8_t* rdram, recomp_context* ctx) {
        uint64_t hi = 0, lo = 0, result = 0;
        // 0x80001000: beq         $t5, $at, L_80001020
        if (ctx->r13 == ctx->r1) {
            goto L_80001020;
        }
        /* a comment with a } brace */
    L_80001020:
        ctx->r29 = ADD32(ctx->r29, 0X18);
        return;
    }
    RECOMP_FUNC void plain(uint8_t* rdram, recomp_context* ctx) {
        ctx->r2 = 0;
        return;
    }
    """
)


def write(directory: Path, files: dict[str, str]) -> list[Path]:
    for name, text in files.items():
        (directory / name).write_text(text, encoding="utf-8")
    return sorted(directory / name for name in files)


class GuestHookTests(unittest.TestCase):
    def setUp(self):
        self.scratch = tempfile.TemporaryDirectory()
        self.root = Path(self.scratch.name)
        for revision, vram in (("77", 0x8012350C), ("80", 0x80123A8C)):
            kit = self.root / f"modding/reference-us-v{revision}"
            kit.mkdir(parents=True)
            (kit / "data.toml").write_text(
                f'[[section]]\nsymbols = [{{ name = "gCounter", vram = 0x{vram:08X} }}]\n', encoding="utf-8"
            )
        self.guest = self.root / "guest"
        self.guest.mkdir()

    def tearDown(self):
        self.scratch.cleanup()

    def hooks(self, spec: str, revision="77", payload="funcs"):
        path = self.root / "hooks.toml"
        path.write_text("schema = 1\n" + textwrap.dedent(spec), encoding="utf-8")
        return guest_hooks.load(path, revision, payload, self.root)

    def test_entry_returns_and_edits_change_only_the_body(self):
        hooks = self.hooks(
            """
            [[hook]]
            id = "thing"
            function = "draw_thing"
            payloads = ["funcs"]
            entry = '''    int seen=0;
            '''
            returns = 'observe(rdram,{revision}); '
            [[hook.edits]]
            match = "if (ctx->r13 == ctx->r1) {"
            replace = "if (seen || (ctx->r13 == ctx->r1)) {"
            """
        )
        out, problems = guest_hooks.apply(write(self.guest, {"funcs_1.c": GUEST}), hooks)
        self.assertEqual(problems, [])
        text = out["funcs_1.c"]
        self.assertIn(
            "static void dkr_code_body_draw_thing(uint8_t* rdram, recomp_context* ctx) {\n    int seen=0;\n\n", text
        )
        self.assertIn("    observe(rdram,77); return;\n}\nRECOMP_FUNC void plain", text)
        self.assertIn("if (seen || (ctx->r13 == ctx->r1)) {", text)
        # The dispatcher's own early return and the next function are untouched.
        self.assertIn("if (dkr_code_dispatch(rdram, ctx)) return;", text)
        self.assertIn("    ctx->r2 = 0;\n    return;\n}", text)

    def test_function_without_dispatcher_hooks_the_function_itself(self):
        hooks = self.hooks(
            """
            [[hook]]
            id = "plain"
            function = "plain"
            payloads = ["funcs"]
            returns = 'done(); '
            """
        )
        out, problems = guest_hooks.apply(write(self.guest, {"funcs_1.c": GUEST}), hooks)
        self.assertEqual(problems, [])
        self.assertIn("    ctx->r2 = 0;\n    done(); return;\n}", out["funcs_1.c"])

    def test_revision_values_and_symbols(self):
        spec = """
            [[hook]]
            id = "values"
            function = "plain"
            payloads = ["funcs"]
            entry = '''    read({symbol:gCounter}U,{label});
            '''
            [hook.vars.77]
            label = "L_77"
            [hook.vars.80]
            label = "L_80"
            """
        for revision, expected in (("77", "read(0x8012350CU,L_77);"), ("80", "read(0x80123A8CU,L_80);")):
            with self.subTest(revision=revision):
                (hook,) = self.hooks(spec, revision)
                self.assertEqual(hook.entry, f"    {expected}\n")

    def test_unknown_values_fail(self):
        for value in ("{symbol:gMissing}", "{undefined}"):
            with self.subTest(value=value), self.assertRaises(guest_hooks.HookError):
                self.hooks(f"""
                    [[hook]]
                    id = "bad"
                    function = "plain"
                    payloads = ["funcs"]
                    entry = '{value}'
                    """)

    def test_payloads_select_hooks(self):
        spec = """
            [[hook]]
            id = "owned-only"
            function = "plain"
            payloads = ["owned"]
            returns = 'x(); '
            """
        self.assertEqual(self.hooks(spec, payload="funcs"), [])
        self.assertEqual(len(self.hooks(spec, payload="owned")), 1)

    def test_exactly_once(self):
        cases = {
            "missing function": ("absent", {"funcs_1.c": GUEST}, "found 0 times"),
            "function in two files": ("plain", {"funcs_1.c": GUEST, "funcs_2.c": GUEST}, "found 2 times"),
        }
        for label, (function, files, message) in cases.items():
            with self.subTest(label), tempfile.TemporaryDirectory() as directory:
                hooks = self.hooks(f"""
                    [[hook]]
                    id = "once"
                    function = "{function}"
                    payloads = ["funcs"]
                    returns = 'x(); '
                    """)
                _, problems = guest_hooks.apply(write(Path(directory), files), hooks)
                self.assertTrue(any(message in problem for problem in problems), problems)

    def test_edit_must_match_once(self):
        for match, count in (("not there", 0), ("ctx->r", None)):
            with self.subTest(match=match):
                hooks = self.hooks(f"""
                    [[hook]]
                    id = "edit"
                    function = "draw_thing"
                    payloads = ["funcs"]
                    [[hook.edits]]
                    match = "{match}"
                    replace = "y"
                    """)
                _, problems = guest_hooks.apply(write(self.guest, {"funcs_1.c": GUEST}), hooks)
                self.assertTrue(any("not once" in problem for problem in problems), problems)

    def test_returns_needs_a_return(self):
        hooks = self.hooks(
            """
            [[hook]]
            id = "fence"
            function = "draw_thing"
            payloads = ["funcs"]
            returns = 'x(); '
            """
        )
        head, tail = GUEST.split("    return;\n}\nRECOMP_FUNC void plain")
        text = head + "}\nRECOMP_FUNC void plain" + tail
        _, problems = guest_hooks.apply(write(self.guest, {"funcs_1.c": text}), hooks)
        self.assertTrue(any("has no return" in problem for problem in problems), problems)

    def test_apply_writes_hooked_files_and_list(self):
        (self.root / "hooks.toml").write_text(
            'schema = 1\n[[hook]]\nid = "p"\nfunction = "plain"\npayloads = ["funcs"]\nreturns = "x(); "\n',
            encoding="utf-8",
        )
        write(self.guest, {"funcs_1.c": GUEST, "funcs_2.c": "int unrelated;\n"})
        out = self.root / "out"
        original = guest_hooks.ROOT
        guest_hooks.ROOT = self.root
        try:
            status = guest_hooks.main(
                [
                    "apply",
                    "--spec",
                    str(self.root / "hooks.toml"),
                    "--in",
                    str(self.guest),
                    "--out",
                    str(out),
                    "--revision",
                    "77",
                    "--payload",
                    "funcs",
                ]
            )
        finally:
            guest_hooks.ROOT = original
        self.assertEqual(status, 0)
        self.assertEqual((out / "hooked.txt").read_text(encoding="utf-8"), "funcs_1.c\n")
        self.assertFalse((out / "funcs_2.c").exists())
        self.assertEqual((self.guest / "funcs_1.c").read_text(encoding="utf-8"), GUEST)


class RepositorySpecTests(unittest.TestCase):
    def test_spec_loads_for_every_payload_and_revision(self):
        spec = ROOT / "runtime-recomp/guest-hooks/hooks.toml"
        for revision in guest_hooks.REVISIONS:
            for payload in guest_hooks.PAYLOADS:
                with self.subTest(revision=revision, payload=payload):
                    self.assertTrue(guest_hooks.load(spec, revision, payload))


if __name__ == "__main__":
    unittest.main()
