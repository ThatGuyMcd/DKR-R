#!/usr/bin/env python3
"""The AI agent hooks in scripts/agent_hooks/ block what they should, allow
what they should, and are wired identically for Claude Code and Codex.

Every hook gets a synthetic stdin payload shaped like the agents' own."""

import importlib.util
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import time
import unittest

ROOT = Path(__file__).resolve().parents[2]
HOOKS = ROOT / "scripts/agent_hooks"
HOOK_SCRIPTS = {"session_start.py", "pre_edit_guard.py", "post_edit_check.py", "stop_check.py"}
# The hooks act only inside a git checkout. Packaged source copies (the macOS
# build kit) have no .git, so the tests that drive them against this repo skip.
IN_CHECKOUT = (ROOT / ".git").exists()
SKIP_OUTSIDE_CHECKOUT = unittest.skipUnless(IN_CHECKOUT, "needs a git checkout of the repository")
SPEC = importlib.util.spec_from_file_location("hooklib", HOOKS / "hooklib.py")
hooklib = importlib.util.module_from_spec(SPEC)
sys.modules["hooklib"] = hooklib  # dataclasses resolve their module by name
SPEC.loader.exec_module(hooklib)


def run_hook(script, payload, env=None):
    # The developer's own git config (a global core.hooksPath, say) must not leak in.
    environment = {key: value for key, value in os.environ.items() if key != "CLAUDE_PROJECT_DIR"}
    environment["GIT_CONFIG_GLOBAL"] = os.devnull
    environment.update(env or {})
    return subprocess.run(
        [sys.executable, str(HOOKS / script)],
        input=json.dumps(payload),
        capture_output=True,
        text=True,
        env=environment,
        timeout=120,
    )


def edit_payload(tool, cwd, **tool_input):
    return {
        "session_id": "agent-hooks-test",
        "cwd": str(cwd),
        "hook_event_name": "PreToolUse",
        "tool_name": tool,
        "tool_input": tool_input,
    }


def git(root, *args):
    subprocess.run(
        ["git", "-C", str(root), *args],
        check=True,
        capture_output=True,
        env={**os.environ, "GIT_CONFIG_GLOBAL": os.devnull},
    )


@SKIP_OUTSIDE_CHECKOUT
class PreEditGuardTests(unittest.TestCase):
    def test_checkout_is_found_from_a_nested_directory(self):
        self.assertEqual(hooklib.checkout_of(HOOKS), ROOT)

    def test_blocks_a_new_overlay_cmake_layer(self):
        result = run_hook(
            "pre_edit_guard.py",
            edit_payload(
                "Write",
                ROOT,
                file_path=str(ROOT / "patches/x/ReleaseOverlay.cmake"),
                content="set(DKR_OVERLAY ON)\n",
            ),
        )
        self.assertEqual(result.returncode, 2, result.stderr)
        self.assertIn("patches/x/ReleaseOverlay.cmake:1: PG001", result.stderr)
        self.assertIn("instead:", result.stderr)
        self.assertFalse((ROOT / "patches/x").exists())

    def test_blocks_the_same_layer_from_a_codex_apply_patch(self):
        patch = "*** Begin Patch\n*** Add File: patches/x/ReleaseOverlay.cmake\n+set(DKR_OVERLAY ON)\n*** End Patch\n"
        result = run_hook("pre_edit_guard.py", edit_payload("apply_patch", ROOT, command=patch))
        self.assertEqual(result.returncode, 2, result.stderr)
        self.assertIn("PG001", result.stderr)

    def test_blocks_new_text_that_breaks_a_rule(self):
        result = run_hook(
            "pre_edit_guard.py",
            edit_payload(
                "Write",
                ROOT,
                file_path=str(ROOT / "runtime-recomp/src/game/agent_hooks_probe.cpp"),
                content='#include "build/generated/funcs.h"\n',
            ),
        )
        self.assertEqual(result.returncode, 2, result.stderr)
        self.assertIn("PG009", result.stderr)

    def test_allows_an_extern_edit(self):
        target = ROOT / "extern/RT64/src/agent_hooks_probe.cpp"
        result = run_hook(
            "pre_edit_guard.py",
            edit_payload("Edit", ROOT, file_path=str(target), old_string="a", new_string='#include "/abs/x.h"'),
        )
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_allows_an_ordinary_edit(self):
        result = run_hook(
            "pre_edit_guard.py",
            edit_payload(
                "Edit", ROOT, file_path=str(ROOT / "AGENTS.md"), old_string="# Working in DKR-R", new_string="# DKR-R"
            ),
        )
        self.assertEqual((result.returncode, result.stderr), (0, ""))

    def test_allows_editing_a_baselined_file(self):
        baseline = (ROOT / "architecture/pattern-guard-baseline.txt").read_text(encoding="utf-8")
        path = next(line.split()[0] for line in baseline.splitlines() if line and not line.startswith("#"))
        current = (ROOT / path).read_text(encoding="utf-8", errors="replace")
        result = run_hook("pre_edit_guard.py", edit_payload("Write", ROOT, file_path=str(ROOT / path), content=current))
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_ignores_files_outside_the_checkout(self):
        with tempfile.TemporaryDirectory() as temp:
            result = run_hook(
                "pre_edit_guard.py",
                edit_payload("Write", temp, file_path=f"{temp}/patches/x/ReleaseOverlay.cmake", content="x\n"),
            )
        self.assertEqual(result.returncode, 0, result.stderr)


class PlannedEditTests(unittest.TestCase):
    def test_edit_multiedit_and_apply_patch_reconstruct_the_new_text(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "a.txt"
            path.write_text("one\ntwo\nthree\n", encoding="utf-8")
            edit = hooklib.planned_edits(
                edit_payload("Edit", temp, file_path=str(path), old_string="two", new_string="2")
            )
            self.assertEqual(edit[0].text, "one\n2\nthree\n")
            multi = hooklib.planned_edits(
                edit_payload(
                    "MultiEdit",
                    temp,
                    file_path=str(path),
                    edits=[{"old_string": "one", "new_string": "1"}, {"old_string": "three", "new_string": "3"}],
                )
            )
            self.assertEqual(multi[0].text, "1\ntwo\n3\n")
            patch = (
                "*** Begin Patch\n*** Update File: a.txt\n*** Move to: b.txt\n@@\n one\n-two\n+zwei\n"
                "*** Delete File: gone.txt\n*** End Patch\n"
            )
            applied = hooklib.planned_edits(edit_payload("apply_patch", temp, command=patch))
            self.assertEqual([(edit.path.name, edit.text) for edit in applied], [("b.txt", "one\nzwei\nthree\n")])


class StopCheckTests(unittest.TestCase):
    """A scratch DKR-R checkout with one baselined layer and its baseline."""

    def setUp(self):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        self.root = Path(temp.name).resolve()
        (self.root / "scripts").mkdir()
        shutil.copy2(ROOT / "scripts/pattern_guard.py", self.root / "scripts/pattern_guard.py")
        self.write("patches/old/ReleaseOverlay.cmake", "set(OLD ON)\n")
        self.write("architecture/pattern-guard-baseline.txt", "patches/old/ReleaseOverlay.cmake PG001\n")
        self.write(".gitignore", "/.claude/session-base\n/.claude/session-base.*.tmp\n")
        git(self.root, "init", "-q")
        git(self.root, "add", ".")
        git(self.root, "-c", "user.name=t", "-c", "user.email=t@example.invalid", "commit", "-qm", "base")

    def write(self, path, text):
        target = self.root / path
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(text, encoding="utf-8")

    def event(self, name, **fields):
        return {"session_id": "s1", "cwd": str(self.root), "hook_event_name": name, **fields}

    def start(self):
        result = run_hook("session_start.py", self.event("SessionStart", source="startup"))
        self.assertEqual(result.returncode, 0, result.stderr)
        return result

    def stop(self, active=False):
        return run_hook("stop_check.py", self.event("Stop", stop_hook_active=active))

    def test_session_start_records_the_base_and_prints_the_pattern(self):
        self.write("AGENTS.md", "# x\n\n## The pattern (non-negotiable)\n- Edit in place.\n\n## Next\nno\n")
        result = self.start()
        self.assertIn("- Edit in place.", result.stdout)
        self.assertNotIn("## Next", result.stdout)
        sessions = json.loads((self.root / ".claude/session-base").read_text(encoding="utf-8"))["sessions"]
        head = subprocess.run(["git", "-C", str(self.root), "rev-parse", "HEAD"], capture_output=True, text=True)
        self.assertEqual(sessions["s1"]["head"], head.stdout.strip())

    def test_pre_existing_baseline_hits_do_not_block_stop(self):
        self.start()
        self.write("patches/old/ReleaseOverlay.cmake", "set(OLD ON)\nset(STILL_OLD ON)\n")
        self.write("docs/notes.md", "unrelated\n")
        result = self.stop()
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_a_new_violation_blocks_stop(self):
        self.start()
        self.write("patches/new/ReleaseOverlay.cmake", "set(NEW ON)\n")
        result = self.stop()
        self.assertEqual(result.returncode, 2, result.stderr)
        self.assertIn("patches/new/ReleaseOverlay.cmake:1: PG001", result.stderr)
        self.assertNotIn("patches/old", result.stderr)

    def test_baselining_a_new_violation_still_blocks_stop(self):
        self.start()
        self.write("patches/new/ReleaseOverlay.cmake", "set(NEW ON)\n")
        self.write(
            "architecture/pattern-guard-baseline.txt",
            "patches/new/ReleaseOverlay.cmake PG001\npatches/old/ReleaseOverlay.cmake PG001\n",
        )
        result = self.stop()
        self.assertEqual(result.returncode, 2, result.stderr)
        self.assertIn("new baseline entry for patches/new/ReleaseOverlay.cmake", result.stderr)

    def test_violations_present_at_session_start_do_not_block_stop(self):
        self.write("patches/dirty/ReleaseOverlay.cmake", "set(DIRTY ON)\n")
        self.start()
        result = self.stop()
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_stop_hook_active_exits_zero(self):
        self.start()
        self.write("patches/new/ReleaseOverlay.cmake", "set(NEW ON)\n")
        self.assertEqual(self.stop(active=False).returncode, 2)
        result = self.stop(active=True)
        self.assertEqual((result.returncode, result.stderr), (0, ""))

    def test_stop_without_a_recorded_session_uses_head(self):
        self.write("patches/new/ReleaseOverlay.cmake", "set(NEW ON)\n")
        self.assertEqual(self.stop().returncode, 2)


class PostEditCheckTests(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        self.root = Path(temp.name).resolve()
        (self.root / "scripts").mkdir()
        (self.root / "scripts/pattern_guard.py").write_text("", encoding="utf-8")
        self.file = self.root / "a.py"
        self.file.write_text("x=1\n", encoding="utf-8")
        git(self.root, "init", "-q")

    def payload(self):
        return {
            "session_id": "s1",
            "cwd": str(self.root),
            "hook_event_name": "PostToolUse",
            "tool_name": "Edit",
            "tool_input": {"file_path": str(self.file), "old_string": "x", "new_string": "x"},
        }

    def test_without_a_pre_commit_config_it_does_nothing(self):
        result = run_hook("post_edit_check.py", self.payload())
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertNotIn("hookSpecificOutput", result.stdout)

    @unittest.skipIf(os.name == "nt", "fake pre-commit is a shell script")
    def test_reports_a_fixer_diff_and_never_rewrites_the_file(self):
        (self.root / ".pre-commit-config.yaml").write_text("repos: []\n", encoding="utf-8")
        hook = self.root / ".git/hooks/pre-commit"
        hook.write_text("#!/bin/sh\n# File generated by pre-commit\n", encoding="utf-8")
        bin_dir = self.root / "bin"
        bin_dir.mkdir()
        fake = bin_dir / "pre-commit"
        fake.write_text(
            '#!/bin/sh\necho "ruff format.............Failed"\necho "- files were modified by this hook"\n'
            'printf "x = 1\\n" > a.py\nexit 1\n',
            encoding="utf-8",
        )
        fake.chmod(0o755)
        before = self.file.stat().st_mtime_ns
        result = run_hook("post_edit_check.py", self.payload(), {"PATH": f"{bin_dir}{os.pathsep}{os.environ['PATH']}"})
        self.assertEqual(result.returncode, 0, result.stderr)
        context = json.loads(result.stdout)["hookSpecificOutput"]
        self.assertEqual(context["hookEventName"], "PostToolUse")
        self.assertIn("+x = 1", context["additionalContext"])
        self.assertIn("ruff format", context["additionalContext"])
        self.assertEqual(self.file.read_text(encoding="utf-8"), "x=1\n")
        self.assertEqual(self.file.stat().st_mtime_ns, before)

    @unittest.skipIf(os.name == "nt", "fake pre-commit is a shell script")
    def test_a_save_made_while_hooks_run_is_kept(self):
        (self.root / ".pre-commit-config.yaml").write_text("repos: []\n", encoding="utf-8")
        (self.root / ".git/hooks/pre-commit").write_text(
            "#!/bin/sh\n# File generated by pre-commit\n", encoding="utf-8"
        )
        bin_dir = self.root / "bin"
        bin_dir.mkdir()
        fake = bin_dir / "pre-commit"
        # A fixer rewrites its copy while another editor saves the real file.
        fake.write_text(
            f'#!/bin/sh\nprintf "x = 1\\n" > a.py\nprintf "x=2\\n" > "{self.file}"\nexit 1\n', encoding="utf-8"
        )
        fake.chmod(0o755)
        result = run_hook("post_edit_check.py", self.payload(), {"PATH": f"{bin_dir}{os.pathsep}{os.environ['PATH']}"})
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("+x = 1", json.loads(result.stdout)["hookSpecificOutput"]["additionalContext"])
        self.assertEqual(self.file.read_text(encoding="utf-8"), "x=2\n")

    def test_without_the_git_hook_it_says_how_to_bootstrap(self):
        (self.root / ".pre-commit-config.yaml").write_text("repos: []\n", encoding="utf-8")
        result = run_hook("post_edit_check.py", self.payload())
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("Bootstrap with", result.stdout)
        self.assertEqual(len(result.stdout.strip().splitlines()), 1)


class ProcessTreeTests(unittest.TestCase):
    def test_timeout_kills_formatters_with_pre_commit(self):
        spec = importlib.util.spec_from_file_location("post_edit_check", HOOKS / "post_edit_check.py")
        module = importlib.util.module_from_spec(spec)
        sys.modules["post_edit_check"] = module  # dataclasses resolve their module by name
        spec.loader.exec_module(module)
        with tempfile.TemporaryDirectory() as temp:
            target = Path(temp) / "edited.py"
            target.write_text("original\n", encoding="utf-8")
            # A "pre-commit" that starts a slow fixer and then hangs past the timeout.
            fixer = f"import time; time.sleep(1.5); open({str(target)!r}, 'w').write('rewritten')"
            parent = (
                f"import subprocess, sys, time; subprocess.Popen([sys.executable, '-c', {fixer!r}]); time.sleep(30)"
            )
            output, failed = module.run_process_tree([sys.executable, "-c", parent], Path(temp), 0.5)
            self.assertTrue(failed)
            self.assertIn("did not finish", output)
            time.sleep(2.5)  # long enough for an orphaned fixer to have written
            self.assertEqual(target.read_text(encoding="utf-8"), "original\n")


class ConfigTests(unittest.TestCase):
    @staticmethod
    def scripts(config):
        commands = [
            handler["command"]
            for groups in config["hooks"].values()
            for group in groups
            for handler in group["hooks"]
            if handler.get("type") == "command"
        ]
        launched = [re.search(r"/scripts/agent_hooks/run (\w+\.py)$", command) for command in commands]
        return commands, launched

    def load(self, path):
        return json.loads((ROOT / path).read_text(encoding="utf-8"))

    def test_claude_and_codex_reference_the_same_existing_scripts(self):
        claude_commands, claude = self.scripts(self.load(".claude/settings.json"))
        codex_commands, codex = self.scripts(self.load(".codex/hooks.json"))
        self.assertTrue(all(claude), claude_commands)
        self.assertTrue(all(codex), codex_commands)
        claude_set = {match.group(1) for match in claude}
        codex_set = {match.group(1) for match in codex}
        self.assertEqual(claude_set, HOOK_SCRIPTS)
        self.assertEqual(codex_set, claude_set)
        for script in claude_set | {"run", "hooklib.py"}:
            self.assertTrue((HOOKS / script).is_file(), script)

    def test_events_line_up(self):
        claude = self.load(".claude/settings.json")["hooks"]
        codex = self.load(".codex/hooks.json")["hooks"]
        self.assertEqual(set(claude), {"SessionStart", "PreToolUse", "PostToolUse", "Stop"})
        self.assertEqual(set(codex), set(claude))
        for event in claude:
            self.assertEqual(
                self.scripts({"hooks": {event: claude[event]}})[0][0].rsplit(" ", 1)[1],
                self.scripts({"hooks": {event: codex[event]}})[0][0].rsplit(" ", 1)[1],
                event,
            )

    def test_claude_commands_are_rooted_on_the_project_dir(self):
        commands, _ = self.scripts(self.load(".claude/settings.json"))
        for command in commands:
            self.assertTrue(command.startswith('"$CLAUDE_PROJECT_DIR"/scripts/agent_hooks/run '), command)

    @SKIP_OUTSIDE_CHECKOUT
    def test_session_state_is_gitignored(self):
        for path in (".claude/session-base", ".claude/settings.local.json"):
            result = subprocess.run(["git", "-C", str(ROOT), "check-ignore", "-q", path])
            self.assertEqual(result.returncode, 0, f"{path} is not ignored")

    @unittest.skipIf(os.name == "nt" or shutil.which("sh") is None, "the launcher is a POSIX sh script")
    def test_launcher_finds_python_and_passes_stdin(self):
        payload = json.dumps(self.stop_payload())
        result = subprocess.run(
            [str(HOOKS / "run"), "stop_check.py"], input=payload, capture_output=True, text=True, timeout=60
        )
        self.assertEqual((result.returncode, result.stderr), (0, ""))
        rejected = subprocess.run([str(HOOKS / "run"), "../x.py"], capture_output=True, text=True)
        self.assertEqual(rejected.returncode, 1)

    @staticmethod
    def stop_payload():
        return {"session_id": "s1", "cwd": str(ROOT), "hook_event_name": "Stop", "stop_hook_active": True}


if __name__ == "__main__":
    unittest.main()
