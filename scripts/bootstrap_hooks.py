#!/usr/bin/env python3
"""Install the repository's pre-commit hooks, or say why they were not.

Setup-macOS.sh, Setup-Linux.sh and Setup-Windows.cmd run this after their
prerequisite checks; the Build entry points run it with --check, which only
prints the install command when the hooks are missing or out of date. It never
fails the caller: every problem is a notice and exit status 0. CI is the gate,
hooks are a convenience.

pre-commit itself comes from `uv tool install`, then `pipx install`, then
`pip install --user`, whichever works first.

pre-commit refuses to install while core.hooksPath is set, because git then
runs hooks from that directory and not the repository's own. This installs
into the repository's own hooks directory anyway (blanking core.hooksPath for
the install only). If core.hooksPath points somewhere else, a global hooks
directory say, git won't run them, and this prints the two ways to fix that.
"""

from __future__ import annotations

import argparse
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
CONFIG = ".pre-commit-config.yaml"
# Written next to the installed hooks; holds the sha256 of the config they were
# installed from so --check can tell when they need reinstalling.
STAMP = ".dkr-bootstrap-version"
INSTALLERS = (
    ("uv", "tool", "install", "pre-commit"),
    ("pipx", "install", "pre-commit"),
    (sys.executable, "-m", "pip", "install", "--user", "--quiet", "pre-commit"),
)


def git(*args: str) -> str | None:
    try:
        result = subprocess.run(["git", *args], cwd=ROOT, capture_output=True, text=True)
    except OSError:
        return None
    return result.stdout.strip() if result.returncode == 0 else None


def resolve(path: str) -> Path:
    return (ROOT / path).resolve()


def config_digest() -> str:
    return hashlib.sha256((ROOT / CONFIG).read_bytes()).hexdigest()


def find_pre_commit() -> list[str] | None:
    if found := shutil.which("pre-commit"):
        return [found]
    # `pip install --user` puts it next to this interpreter's user scripts,
    # which are often not on PATH.
    probe = subprocess.run([sys.executable, "-m", "pre_commit", "--version"], capture_output=True)
    return [sys.executable, "-m", "pre_commit"] if probe.returncode == 0 else None


def install_pre_commit() -> list[str] | None:
    for command in INSTALLERS:
        if not shutil.which(command[0]):
            continue
        print(f"bootstrap_hooks: installing pre-commit with {command[0]}", flush=True)
        if subprocess.run(command).returncode == 0 and (found := find_pre_commit()):
            return found
    return None


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=(__doc__ or "").splitlines()[0])
    parser.add_argument("--check", action="store_true", help="only report whether the hooks are installed and current")
    args = parser.parse_args(argv)
    bootstrap = f"{'python' if os.name == 'nt' else 'python3'} scripts/bootstrap_hooks.py"

    if git("rev-parse", "--is-inside-work-tree") != "true":
        if not args.check:
            print("bootstrap_hooks: not a git checkout, skipping git hooks")
        return 0
    common = git("rev-parse", "--git-common-dir")
    active = git("rev-parse", "--git-path", "hooks")
    if common is None or active is None:
        print("bootstrap_hooks: could not locate the git hooks directory, skipping git hooks")
        return 0
    # pre-commit always installs into <common git dir>/hooks. --git-path hooks
    # is where git runs them from: the same directory (in every worktree)
    # unless core.hooksPath points somewhere else.
    own, runs_from = resolve(common) / "hooks", resolve(active)
    stamp = own / STAMP

    if args.check:
        installed = (own / "pre-commit").is_file() and stamp.is_file()
        if not installed or stamp.read_text().strip() != config_digest():
            print(f"Git hooks are not installed or are out of date. Install them with: {bootstrap}")
        elif runs_from != own:
            print(f"Git hooks are installed but core.hooksPath bypasses them here. {bootstrap} prints the fix.")
        return 0

    pre_commit = find_pre_commit() or install_pre_commit()
    if pre_commit is None:
        print(f"bootstrap_hooks: could not install pre-commit. Run `uv tool install pre-commit`, then {bootstrap}")
        return 0
    env = dict(os.environ)
    if git("config", "--get", "core.hooksPath") is not None:
        # Blank core.hooksPath for pre-commit's "Cowardly refusing" check only;
        # it installs into <common git dir>/hooks either way.
        index = int(env.get("GIT_CONFIG_COUNT", "0"))
        env.update(
            {
                "GIT_CONFIG_COUNT": str(index + 1),
                f"GIT_CONFIG_KEY_{index}": "core.hooksPath",
                f"GIT_CONFIG_VALUE_{index}": "",
            }
        )
    result = subprocess.run([*pre_commit, "install", "--install-hooks"], cwd=ROOT, env=env)
    if result.returncode != 0:
        print(f"bootstrap_hooks: pre-commit install failed (exit {result.returncode}); rerun {bootstrap} to retry")
        return 0
    stamp.write_text(config_digest() + "\n")
    if runs_from == own:
        print(f"bootstrap_hooks: hooks installed in {own}")
    else:
        print(not_active(own, runs_from))
    return 0


def not_active(own: Path, runs_from: Path) -> str:
    origin = (git("config", "--show-origin", "--get", "core.hooksPath") or str(runs_from)).replace("\t", " ")
    return (
        f"bootstrap_hooks: hooks installed in {own}, but core.hooksPath ({origin})\n"
        "  makes git run hooks from there instead. To use them in this checkout, either\n"
        f'    git config --local core.hooksPath "{own}"\n'
        "  (your global hooks then stop running here), or call them from your global\n"
        f'  pre-commit and pre-push hooks: [ -x "{own}/pre-commit" ] && "{own}/pre-commit" "$@"'
    )


if __name__ == "__main__":
    sys.exit(main())
