#!/usr/bin/env python3
"""Refuse new configure-time patch layers on first-party code.

DKR-R used to ship from runtime-recomp/src plus a chain of first-party layers
(patches/<layer>/ReleaseOverlay.cmake, Python string-replace pipelines, a frozen
source capsule). The fold replaces that chain with real source. This guard
keeps any of those mechanisms from coming back, while third-party dependency
patches registered in patches/manifest.json stay allowed.

Every finding names a rule (PG001...), prints as ``path:line: PGnnn message``
and is followed by a one-line pointer to the supported alternative. Existing
hits are inventoried in architecture/pattern-guard-baseline.txt, one
``<path> <rule>`` line per pair. The baseline only ever shrinks:

* a finding not in the baseline fails;
* a baseline entry whose file no longer produces that finding fails (delete it);
* with --diff, a baseline that gained entries since the base fails unless
  --transition-layer is passed (CI passes it for PRs labelled
  ``transition-layer`` until ``fold-in-base`` is tagged).

Scopes: --tree (default) scans tracked and untracked, non-ignored files;
--diff BASE scans files changed since merge-base(BASE, HEAD) including local
edits; --staged scans the index's changed paths (contents are read from the
work tree, which is what pre-commit stashes to). extern/, the untracked build/
directory and the guard's own fixtures are never scanned.

The guard is deliberately small: globs, regexes, a CMake command tokenizer and
an AST walk. Standard library only.

Exit status:
    0  no new findings, and the baseline matches the scanned files
    1  new findings, stale baseline entries, or unlabelled baseline growth
    2  usage or environment error (bad arguments, unknown base ref, not a git
       work tree where one is needed, malformed baseline)
"""

from __future__ import annotations

import argparse
import ast
from collections.abc import Callable, Iterable, Iterator, Mapping
import dataclasses
import fnmatch
import functools
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
BASELINE = PurePosixPath("architecture/pattern-guard-baseline.txt")
# Never scanned: submodules, and the guard, its tests and fixtures, which spell
# out every violation on purpose. The local build/ tree is skipped too, unless
# git tracks a file there (PG011 reports it).
EXCLUDED_PREFIXES = ("extern/", "tests/fixtures/pattern-guard/")
EXCLUDED_FILES = {"scripts/pattern_guard.py", "runtime-recomp/tests/pattern_guard_tests.py"}
LOCAL_BUILD = "build/"

BASELINE_HEADER = """\
# Pattern guard baseline: first-party patch-layer mechanisms that predate the
# guard. One "<path> <rule>" line per finding, sorted, enumerated (never a glob).
#
# This file only ever shrinks. scripts/pattern_guard.py fails on a finding that
# is not listed here, and on a line here whose file no longer produces it. New
# lines are accepted only on PRs labelled transition-layer, until fold-in-base
# is tagged; the fold drives this file to empty.
#
# Regenerate after removing hits: python3 scripts/pattern_guard.py --tree --baseline write
# Explain a rule: python3 scripts/pattern_guard.py --explain PG001
"""


@dataclasses.dataclass(frozen=True)
class Rule:
    """One guarded mechanism, with the supported alternative."""

    id: str
    name: str
    detects: str
    instead: str


RULES = {
    rule.id: rule
    for rule in (
        Rule(
            "PG001",
            "overlay-cmake",
            "a CMake file under patches/ (a configure-time overlay)",
            "change runtime-recomp/CMakeLists.txt or runtime-recomp/cmake/*.cmake directly",
        ),
        Rule(
            "PG002",
            "project-include",
            "CMAKE_PROJECT_INCLUDE or cmake_language(DEFER outside GuestHooks.cmake and ProjectTests.cmake",
            "declare targets and sources in runtime-recomp/CMakeLists.txt; no injected configure hooks",
        ),
        Rule(
            "PG003",
            "source-swap",
            "rewriting a first-party target's SOURCES or pointing its sources at build/generated",
            "edit the source file in runtime-recomp/src and list it with target_sources()",
        ),
        Rule(
            "PG004",
            "string-replace-pipeline",
            "Python that reads first-party text, rewrites it with replace/re.sub/once/region and writes it out",
            "edit the source file itself; generators must emit new files, not rewritten copies",
        ),
        Rule(
            "PG005",
            "capsule",
            "a frozen source capsule: *capsule* paths, manifest.json with roles, patches/**/*-receipt.json, "
            "source-capsule .gitattributes lines",
            "commit the real source; git history is the archive",
        ),
        Rule(
            "PG006",
            "first-party-under-patches",
            "a C/C++/shader/TOML source file under patches/",
            "move it into runtime-recomp/src and add it to its target",
        ),
        Rule(
            "PG007",
            "unregistered-dependency-patch",
            "a dependency *.patch, or a dkr_stage_patch() patch, missing from patches/manifest.json "
            "or with a stale sha256; or a stagedPatches entry no runtime-recomp CMake applies",
            "register the patch with its sha256 in patches/manifest.json (stagedPatches for "
            "dkr_stage_patch), or drop the entry with the call",
        ),
        Rule(
            "PG008",
            "patch-against-first-party",
            "a *.patch whose paths land in runtime-recomp/, modding/ or scripts/",
            "commit the change to the first-party file instead of a patch",
        ),
        Rule(
            "PG009",
            "build-dir-include",
            "#include of an absolute path or of a build/ directory",
            "include a path relative to the source tree or a target include directory",
        ),
        Rule(
            "PG010",
            "identity-literal",
            "a digest literal or ledger spliced into an online identity string",
            "change identity only in netplay/legacy_identity.hpp",
        ),
        Rule(
            "PG011",
            "generated-committed",
            "a committed file under a build/ directory, or a committed *_symbols.hpp generator output",
            "generate it at build time and keep it out of git",
        ),
        Rule(
            "PG012",
            "guest-rewrite",
            "Python or CMake rewriting recompiled guest code (RecompiledFuncs, funcs_*.c, guest_*.c) in place",
            "declare the hook in runtime-recomp/guest-hooks/hooks.toml (scripts/guest_hooks.py applies it)",
        ),
        Rule(
            "PG013",
            "patches-layout",
            "anything else under patches/: only manifest.json, README.md and registered "
            "dependency *.patch files belong there",
            "put first-party code, tooling and notes in runtime-recomp/, scripts/ or docs/",
        ),
    )
}

# Explicit allow-lists. Each entry is a reviewed, permanent exception.
PROJECT_INCLUDE_ALLOWED = {
    "runtime-recomp/cmake/GuestHooks.cmake",
    "runtime-recomp/cmake/ProjectTests.cmake",  # deferred test assertions and metadata
}
SOURCE_SWAP_ALLOWED = {
    "runtime-recomp/cmake/StagedPatch.cmake",  # dependency-patch staging
    "runtime-recomp/cmake/GuestHooks.cmake",
}
PIPELINE_ALLOWED = {
    "scripts/gen_guest_symbols.py": "writes new symbol headers",
    "scripts/guest_hooks.py": "applies declared hooks to generated guest code",
    "scripts/generate_replay_probe.py": "reads first-party headers only to collect probe symbol names",
    # Fold-only (step B1): writes the shipped closure into runtime-recomp/ once,
    # respelling staged #include paths. Deleted with scripts/fold/ after C11.
    "scripts/fold/apply_closure.py": "writes the shipped v1.0.11 closure back into the tree once",
    # Fold-only (step B3): diffs the stage chain against the shipped dependency
    # transforms into registered stage patches, respelling the same includes.
    "scripts/fold/dependency_stages.py": "writes the v1.0.11 dependency transforms as stage patches",
}
GUEST_REWRITE_ALLOWED = {"scripts/guest_hooks.py", "runtime-recomp/cmake/GuestHooks.cmake"}
IDENTITY_ALLOWED_SUFFIX = "netplay/legacy_identity.hpp"

CMAKE_SUFFIXES = (".cmake",)
SCRIPT_SUFFIXES = (".cmake", ".py", ".sh", ".ps1", ".cmd", ".bat", ".yml", ".yaml")
C_FAMILY_SUFFIXES = (".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx", ".inl", ".inc", ".m", ".mm")
NATIVE_SUFFIXES = C_FAMILY_SUFFIXES + (".hlsl", ".hlsli")
CODE_SUFFIXES = NATIVE_SUFFIXES + SCRIPT_SUFFIXES
FIRST_PARTY_SOURCE_SUFFIXES = (".c", ".cc", ".cpp", ".h", ".hpp", ".inl", ".mm")
# Where a patch target is our own code; src/game/ is runtime-recomp-relative.
FIRST_PARTY_PATCH_TARGETS = ("runtime-recomp/", "modding/", "scripts/", "src/game/")

PROJECT_INCLUDE = re.compile(r"CMAKE_PROJECT_INCLUDE|cmake_language\s*\(\s*DEFER\b", re.IGNORECASE)
INCLUDE = re.compile(r'^\s*#\s*include\s*[<"]([^">]+)[">]')
ABSOLUTE_OR_BUILD = re.compile(r"^(?:/|\\|[A-Za-z]:[/\\])|(?:^|[/\\])build[/\\]")
# Identity splices, not every 64-hex string: mods carry content hashes.
IDENTITY_SPLICE = re.compile(
    r"""\bbase\s*\+=\s*"(?:[0-9a-fA-F]{64}|'+\s*\+)"""  # base+="<digest>" or code emitting one
    r"""|\|(?:sdk2|shared-saves):"""  # salted identity joins
    r"""|string\s*\(\s*SHA256\s+\S+\s+"[^"]*\$\{ledger\}"""
)
GUEST_CODE = re.compile(r"RecompiledFuncs|(?:^|[/\\*])(?:funcs|guest)_(?:\d+|\*|\$?\{[^}]*\})\.c\b")
# A string naming first-party source or a patch layer, as a pipeline would.
FIRST_PARTY_TEXT = re.compile(r"(?:^|[/\\])(?:runtime-recomp[/\\]src|src[/\\]game|modding|patches)[/\\]")
STABLE_PATH = re.compile(r"dkr_\w+_stable_path")
SWAP_COMMANDS = {"dkr_repair_replace_source", "dkr_sdk2_rebind"}
LIST_MUTATIONS = {"TRANSFORM", "FILTER", "REMOVE_ITEM", "REMOVE_AT", "INSERT"}


@dataclasses.dataclass(frozen=True, order=True)
class Finding:
    """A rule hit at a path and 1-based line."""

    path: str
    line: int
    rule: str
    message: str

    def render(self) -> str:
        return f"{self.path}:{self.line}: {self.rule} {self.message}\n    instead: {RULES[self.rule].instead}"


class GuardError(Exception):
    """A usage or environment problem (exit status 2)."""


# --- CMake tokenizer --------------------------------------------------------


@dataclasses.dataclass(frozen=True)
class Command:
    """One CMake command invocation; quoted arguments are unquoted."""

    name: str
    args: tuple[str, ...]
    line: int


_BRACKET_OPEN = re.compile(r"\[(=*)\[")


def parse_cmake(text: str) -> list[Command]:
    """Tokenize CMake into commands. Good enough for guard checks, not a
    full CMake parser: it understands comments, quoting, bracket arguments
    and nested parentheses, and nothing about evaluation."""
    commands: list[Command] = []
    i, line, size = 0, 1, len(text)

    def skip_bracket(start: int, equals: str) -> int:
        nonlocal line
        end = text.find(f"]{equals}]", start)
        end = size if end < 0 else end + len(equals) + 2
        line += text.count("\n", start, end)
        return end

    while i < size:
        char = text[i]
        if char == "\n":
            line, i = line + 1, i + 1
        elif char == "#":
            bracket = _BRACKET_OPEN.match(text, i + 1)
            if bracket:
                i = skip_bracket(bracket.end(), bracket.group(1))
            else:
                newline = text.find("\n", i)
                i = size if newline < 0 else newline
        elif char.isalpha() or char == "_":
            start = i
            while i < size and (text[i].isalnum() or text[i] == "_"):
                i += 1
            name, opener = text[start:i], i
            while opener < size and text[opener] in " \t":
                opener += 1
            if opener < size and text[opener] == "(":
                command_line = line
                args, i = _parse_args(text, opener + 1)
                commands.append(Command(name.lower(), tuple(args), command_line))
                line += text.count("\n", opener, i)
        else:
            i += 1
    return commands


def _parse_args(text: str, i: int) -> tuple[list[str], int]:
    """Arguments from just after ``(`` to the matching ``)``; returns the
    index after it."""
    args: list[str] = []
    token: list[str] = []
    depth, size = 1, len(text)

    def flush() -> None:
        if token:
            args.append("".join(token))
            token.clear()

    while i < size:
        char = text[i]
        if char == '"':
            flush()
            i += 1
            quoted: list[str] = []
            while i < size and text[i] != '"':
                if text[i] == "\\" and i + 1 < size:
                    quoted.append(text[i : i + 2])
                    i += 2
                    continue
                quoted.append(text[i])
                i += 1
            args.append("".join(quoted))
            i += 1
        elif char == "#":
            flush()
            newline = text.find("\n", i)
            i = size if newline < 0 else newline
        elif char == "[" and _BRACKET_OPEN.match(text, i):
            flush()
            bracket = _BRACKET_OPEN.match(text, i)
            assert bracket
            close = f"]{bracket.group(1)}]"
            end = text.find(close, bracket.end())
            end = size if end < 0 else end
            args.append(text[bracket.end() : end])
            i = end + len(close)
        elif char.isspace():
            flush()
            i += 1
        elif char == "(":
            flush()
            depth += 1
            i += 1
        elif char == ")":
            flush()
            depth -= 1
            i += 1
            if depth == 0:
                break
        else:
            token.append(char)
            i += 1
    return args, i


# --- The tree under scan ----------------------------------------------------


class Tree:
    """Files of one checkout plus the context rules need (manifest, targets).

    A git work tree is enumerated with git; any other directory (the test
    fixtures) is walked, and every file in it counts as committed.

    ``overlay`` maps paths to text that replaces (or creates) the file on
    disk, so rules can judge an edit before it is written. Overlay paths count
    as committed outside build/, as they will be once the edit lands."""

    def __init__(self, root: Path, overlay: Mapping[str, str] | None = None) -> None:
        self.root = root.resolve()
        self.overlay = dict(overlay or {})
        self.is_git = _git_toplevel(self.root) == self.root
        if self.is_git:
            self.tracked = set(_git_lines(self.root, "ls-files", "--cached"))
            listed = self.tracked | set(_git_lines(self.root, "ls-files", "--others", "--exclude-standard"))
        else:
            relative = (path.relative_to(self.root) for path in self.root.rglob("*") if path.is_file())
            self.tracked = listed = {
                path.as_posix() for path in relative if ".git" not in path.parts and path.parts[0] != LOCAL_BUILD[:-1]
            }
        self.tracked |= {path for path in self.overlay if not path.startswith(LOCAL_BUILD)}
        self.files = sorted(
            path for path in listed | set(self.overlay) if self.is_scanned(path) and self.has_file(path)
        )

    def is_scanned(self, path: str) -> bool:
        """Outside the excluded paths, and not untracked local build output."""
        return (
            not path.startswith(EXCLUDED_PREFIXES)
            and path not in EXCLUDED_FILES
            and (path in self.tracked or not path.startswith(LOCAL_BUILD))
        )

    def has_file(self, path: str) -> bool:
        return path in self.overlay or (self.root / path).is_file()

    @functools.cache
    def text(self, path: str) -> str | None:
        """Decoded file text, or None for binary or unreadable files."""
        if path in self.overlay:
            return self.overlay[path]
        try:
            data = (self.root / path).read_bytes()
        except OSError:
            return None
        if b"\0" in data[:8192]:
            return None
        return data.decode("utf-8", errors="replace")

    @functools.cache
    def cmake(self, path: str) -> list[Command]:
        return parse_cmake(self.text(path) or "")

    @functools.cache
    def python(self, path: str) -> ast.Module | None:
        try:
            return ast.parse(self.text(path) or "", filename=path)
        except (SyntaxError, ValueError):
            return None

    @functools.cached_property
    def registered_patches(self) -> dict[str, str]:
        """patches/manifest.json path -> sha256 for every dependency patch:
        the ones applied to a checkout (``dependencies[].patches[]``) and the
        ones dkr_stage_patch applies to a staged copy (``stagedPatches[]``)."""
        try:
            manifest = json.loads(self.text("patches/manifest.json") or "{}")
        except json.JSONDecodeError:
            return {}
        entries = [
            *(patch for dependency in manifest.get("dependencies", []) for patch in dependency.get("patches", [])),
            *manifest.get("stagedPatches", []),
        ]
        return {patch["path"]: patch.get("sha256", "") for patch in entries if "path" in patch}

    @functools.cached_property
    def staged_patch_entries(self) -> list[str]:
        try:
            manifest = json.loads(self.text("patches/manifest.json") or "{}")
        except json.JSONDecodeError:
            return []
        return [patch["path"] for patch in manifest.get("stagedPatches", []) if "path" in patch]

    @functools.cached_property
    def cmake_patch_references(self) -> set[str]:
        """patches/....patch paths named anywhere in runtime-recomp's CMake."""
        return {
            match.group(1)
            for path in self.files
            if path.startswith("runtime-recomp/") and is_cmake(path)
            for match in re.finditer(r"(?:^|[/}\s\"])(patches/[^\s\")]+\.patch)", self.text(path) or "")
        }

    @functools.cached_property
    def dependency_dirs(self) -> set[str]:
        """patches/<dir> directories that hold at least one registered patch."""
        return {"/".join(path.split("/")[:2]) for path in self.registered_patches}

    @functools.cached_property
    def first_party_targets(self) -> set[str]:
        """Targets declared by add_library/add_executable under runtime-recomp/."""
        return {
            command.args[0]
            for path in self.files
            if path.startswith("runtime-recomp/") and is_cmake(path)
            for command in self.cmake(path)
            if command.name in ("add_library", "add_executable") and command.args and "${" not in command.args[0]
        }

    @functools.cached_property
    def first_party_sources(self) -> set[str]:
        """Basenames of first-party C/C++ sources, for spotting rewritten copies."""
        return {
            PurePosixPath(path).name
            for path in self.files
            if path.startswith("runtime-recomp/src/") and path.endswith(FIRST_PARTY_SOURCE_SUFFIXES)
        }

    def is_first_party_target(self, name: str) -> bool:
        """Literal targets are looked up; a variable target cannot be proven
        third-party, so it counts as first-party."""
        return "${" in name or name in self.first_party_targets


def _git_toplevel(root: Path) -> Path | None:
    result = subprocess.run(
        ["git", "-C", str(root), "rev-parse", "--show-toplevel"],
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
        text=True,
    )
    return Path(result.stdout.strip()).resolve() if result.returncode == 0 else None


def _git_lines(root: Path, *args: str) -> list[str]:
    result = subprocess.run(
        ["git", "-C", str(root), *args, "-z"], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True
    )
    if result.returncode:
        raise GuardError(f"git {' '.join(args)} failed: {result.stderr.strip()}")
    return [line for line in result.stdout.split("\0") if line]


def is_cmake(path: str) -> bool:
    return path.endswith(CMAKE_SUFFIXES) or PurePosixPath(path).name == "CMakeLists.txt"


def is_workflow(path: str) -> bool:
    return path.startswith(".github/workflows/")


# --- Rules -----------------------------------------------------------------

FileCheck = Callable[[Tree, str], Iterator[Finding]]
FILE_CHECKS: list[FileCheck] = []


def file_check(function: FileCheck) -> FileCheck:
    FILE_CHECKS.append(function)
    return function


def _lines(tree: Tree, path: str) -> Iterator[tuple[int, str]]:
    yield from enumerate((tree.text(path) or "").splitlines(), 1)


@file_check
def overlay_cmake(tree: Tree, path: str) -> Iterator[Finding]:
    """PG001: CMake under patches/ is a configure-time overlay."""
    if path.startswith("patches/") and is_cmake(path):
        yield Finding(path, 1, "PG001", "CMake overlay under patches/")


@file_check
def project_include(tree: Tree, path: str) -> Iterator[Finding]:
    """PG002: injected configure hooks in build scripts and workflows."""
    if path in PROJECT_INCLUDE_ALLOWED:
        return
    if not (is_cmake(path) or path.endswith(SCRIPT_SUFFIXES) or is_workflow(path)):
        return
    for number, line in _lines(tree, path):
        if not line.lstrip().startswith("#") and PROJECT_INCLUDE.search(line):
            yield Finding(path, number, "PG002", "injects a configure hook")


@file_check
def source_swap(tree: Tree, path: str) -> Iterator[Finding]:
    """PG003: swapping first-party target sources at configure time.

    Third-party SOURCES swaps (rt64, plume, SDL2 ...) are the staged
    dependency-patch mechanism and stay allowed."""
    if not is_cmake(path) or path in SOURCE_SWAP_ALLOWED:
        return
    commands = tree.cmake(path)
    first_party = tree.is_first_party_target
    for index, command in enumerate(commands):
        args = command.args
        if command.name in SWAP_COMMANDS or STABLE_PATH.fullmatch(command.name):
            yield Finding(path, command.line, "PG003", f"source swap helper {command.name}()")
        elif (
            command.name in ("function", "macro")
            and args
            and (args[0].lower() in SWAP_COMMANDS or STABLE_PATH.fullmatch(args[0].lower()))
        ):
            yield Finding(path, command.line, "PG003", f"defines source swap helper {args[0]}()")
        elif command.name == "set_target_properties" and "PROPERTIES" in args:
            split = args.index("PROPERTIES")
            if "SOURCES" in args[split + 1 :: 2] and any(map(first_party, args[:split])):
                yield Finding(path, command.line, "PG003", "replaces a first-party target's SOURCES")
        elif command.name == "set_property" and args[:1] == ("TARGET",) and "PROPERTY" in args:
            split = args.index("PROPERTY")
            targets = [arg for arg in args[1:split] if arg not in ("APPEND", "APPEND_STRING")]
            if args[split + 1 : split + 2] == ("SOURCES",) and any(map(first_party, targets)):
                yield Finding(path, command.line, "PG003", "replaces a first-party target's SOURCES")
        elif command.name == "get_target_property" and len(args) == 3 and args[2] == "SOURCES":
            variable, target = args[0], args[1]
            mutated = any(
                later.name == "list"
                and len(later.args) > 1
                and later.args[0] in LIST_MUTATIONS
                and later.args[1] == variable
                for later in commands[index + 1 :]
            )
            if mutated and first_party(target):
                yield Finding(path, command.line, "PG003", f"edits the SOURCES list of first-party {target}")
        elif _generated_include_dirs(command, first_party):
            yield Finding(path, command.line, "PG003", "points first-party sources at build/generated includes")


def _generated_include_dirs(command: Command, first_party: Callable[[str], bool]) -> bool:
    """set_source_files_properties / set_property(SOURCE) giving sources an
    INCLUDE_DIRECTORIES entry in the generated build tree."""
    args = command.args
    if command.name == "set_source_files_properties":
        keyword = "PROPERTIES"
    elif command.name == "set_property" and args[:1] == ("SOURCE",):
        keyword = "PROPERTY"
    else:
        return False
    if keyword not in args:
        return False
    split = args.index(keyword)
    values = args[split + 1 :]
    if "INCLUDE_DIRECTORIES" not in values[:1] + values[::2] or not any(
        "${CMAKE_BINARY_DIR}/generated" in value for value in values
    ):
        return False
    scope = args[:split]
    if "TARGET_DIRECTORY" not in scope:
        return True  # the calling directory is first-party
    targets = scope[scope.index("TARGET_DIRECTORY") + 1 :]
    return any(map(first_party, targets))


@dataclasses.dataclass
class _PythonFacts:
    """What a Python module does with text, gathered in one AST walk."""

    reads: list[ast.Call] = dataclasses.field(default_factory=list[ast.Call])
    rewrites: list[ast.Call] = dataclasses.field(default_factory=list[ast.Call])
    writes: list[ast.Call] = dataclasses.field(default_factory=list[ast.Call])
    strings: list[str] = dataclasses.field(default_factory=list[str])
    read_targets: set[str] = dataclasses.field(default_factory=set[str])
    write_targets: list[tuple[str, int, ast.AST]] = dataclasses.field(default_factory=list[tuple[str, int, ast.AST]])


def _open_mode(call: ast.Call) -> str | None:
    """Mode of an open()/Path.open() call, or None when it is not one."""
    function = call.func
    if isinstance(function, ast.Name) and function.id == "open":
        position = 1
    elif isinstance(function, ast.Attribute) and function.attr == "open":
        builtin_like = isinstance(function.value, ast.Name) and function.value.id in ("io", "codecs")
        position = 1 if builtin_like else 0
    else:
        return None
    mode = (
        call.args[position]
        if len(call.args) > position
        else next((keyword.value for keyword in call.keywords if keyword.arg == "mode"), None)
    )
    if mode is None:
        return "r"
    return mode.value if isinstance(mode, ast.Constant) and isinstance(mode.value, str) else "?"


def _open_target(call: ast.Call) -> ast.AST | None:
    function = call.func
    if isinstance(function, ast.Name):
        return call.args[0] if call.args else None
    assert isinstance(function, ast.Attribute)
    return function.value


def _is_rewrite(call: ast.Call) -> bool:
    function = call.func
    if isinstance(function, ast.Name):
        return function.id in ("once", "region")
    if not isinstance(function, ast.Attribute) or len(call.args) < 2:
        return False
    if function.attr == "replace":
        return not (isinstance(function.value, ast.Name) and function.value.id == "os")
    return function.attr in ("sub", "subn")


def _walk_scope(node: ast.AST) -> Iterator[ast.AST]:
    """Nodes of one scope, without descending into nested functions or classes."""
    for child in ast.iter_child_nodes(node):
        yield child
        if not isinstance(child, (ast.FunctionDef, ast.AsyncFunctionDef, ast.ClassDef, ast.Lambda)):
            yield from _walk_scope(child)


def _python_facts(nodes: Iterable[ast.AST]) -> _PythonFacts:
    facts = _PythonFacts()
    for node in nodes:
        if isinstance(node, ast.Constant) and isinstance(node.value, str):
            facts.strings.append(node.value)
        if not isinstance(node, ast.Call):
            continue
        function, mode = node.func, _open_mode(node)
        attribute, receiver = (function.attr, function.value) if isinstance(function, ast.Attribute) else (None, None)
        if mode is not None:
            target = _open_target(node)
            if any(flag in mode for flag in "wax+"):
                facts.writes.append(node)
                if target is not None:
                    facts.write_targets.append((ast.dump(target), node.lineno, target))
            else:
                facts.reads.append(node)
                if target is not None:
                    facts.read_targets.add(ast.dump(target))
        elif receiver is not None and attribute in ("read_text", "read_bytes", "readlines", "read"):
            facts.reads.append(node)
            facts.read_targets.add(ast.dump(receiver))
        elif receiver is not None and (
            attribute in ("write_text", "write_bytes", "writelines") or (attribute == "write" and node.args)
        ):
            facts.writes.append(node)
            facts.write_targets.append((ast.dump(receiver), node.lineno, receiver))
        elif _is_rewrite(node):
            facts.rewrites.append(node)
    return facts


def _docstrings(module: ast.Module) -> set[int]:
    """ids of docstring constants, which describe code rather than drive it."""
    owners = [module] + [
        node for node in ast.walk(module) if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef, ast.ClassDef))
    ]
    return {
        id(owner.body[0].value)
        for owner in owners
        if owner.body and isinstance(owner.body[0], ast.Expr) and isinstance(owner.body[0].value, ast.Constant)
    }


@file_check
def string_replace_pipeline(tree: Tree, path: str) -> Iterator[Finding]:
    """PG004: read text, rewrite it, write it, with first-party source or a
    patch layer named as data. File-level on purpose: pipelines spread the
    read, the edits and the write over helpers."""
    if not path.endswith(".py") or path in PIPELINE_ALLOWED:
        return
    module = tree.python(path)
    if module is None:
        return
    docstrings = _docstrings(module)
    facts = _python_facts(node for node in ast.walk(module) if id(node) not in docstrings)
    if not (facts.reads and facts.rewrites and facts.writes):
        return
    named = next((text for text in facts.strings if _names_first_party(tree, text)), None)
    if named is not None:
        yield Finding(
            path, facts.rewrites[0].lineno, "PG004", f"string-replace pipeline over first-party text ({named[:60]!r})"
        )


def _names_first_party(tree: Tree, text: str) -> bool:
    """A first-party source tree or patch layer path, or a first-party source
    file name (which is what a staged rewritten copy is called)."""
    return (
        bool(FIRST_PARTY_TEXT.search(text)) or PurePosixPath(text.replace("\\", "/")).name in tree.first_party_sources
    )


@file_check
def capsule(tree: Tree, path: str) -> Iterator[Finding]:
    """PG005: a frozen source capsule in any of its forms."""
    posix = PurePosixPath(path)
    if posix.name == ".gitattributes":
        for number, line in _lines(tree, path):
            if "source-capsule" in line:
                yield Finding(path, number, "PG005", "capsule attributes")
        return
    if posix.suffix != ".md" and any("capsule" in part.lower() for part in posix.parts):
        yield Finding(path, 1, "PG005", "capsule file")
    elif path.startswith("patches/") and posix.name.endswith("-receipt.json"):
        yield Finding(path, 1, "PG005", "capsule receipt")
    elif posix.name == "manifest.json":
        try:
            manifest = json.loads(tree.text(path) or "")
        except json.JSONDecodeError:
            return
        if isinstance(manifest, dict) and "roles" in manifest:
            yield Finding(path, 1, "PG005", "capsule manifest (has roles)")


@file_check
def first_party_under_patches(tree: Tree, path: str) -> Iterator[Finding]:
    """PG006: source files living in a patch layer."""
    if path.startswith("patches/") and path.endswith(NATIVE_SUFFIXES + (".toml",)):
        yield Finding(path, 1, "PG006", "first-party source under patches/")


@file_check
def unregistered_dependency_patch(tree: Tree, path: str) -> Iterator[Finding]:
    """PG007: dependency patches and staged patches go through one registry."""
    registered = tree.registered_patches
    if path == "patches/manifest.json":
        for entry in tree.staged_patch_entries:
            if entry not in tree.cmake_patch_references:
                yield Finding(path, 1, "PG007", f"staged patch {entry} is not applied by runtime-recomp CMake")
    if path.endswith(".patch") and "/".join(path.split("/")[:2]) in tree.dependency_dirs:
        if path not in registered:
            yield Finding(path, 1, "PG007", "dependency patch is not in patches/manifest.json")
        elif registered[path] != hashlib.sha256((tree.root / path).read_bytes()).hexdigest():
            yield Finding(path, 1, "PG007", "sha256 differs from patches/manifest.json")
    if is_cmake(path):
        for command in tree.cmake(path):
            if command.name != "dkr_stage_patch" or len(command.args) < 4:
                continue
            # Patches outside patches/ (build-iteration test data) are not dependency patches.
            match = re.search(r"(?:^|[/}])(patches/.+\.patch)$", command.args[3])
            if match and match.group(1) not in registered:
                yield Finding(
                    path, command.line, "PG007", f"staged patch {match.group(1)} is not in patches/manifest.json"
                )


@file_check
def patch_against_first_party(tree: Tree, path: str) -> Iterator[Finding]:
    """PG008: a diff whose target is our own code."""
    if not path.endswith(".patch"):
        return
    for number, line in _lines(tree, path):
        if line.startswith(("--- ", "+++ ")):
            target = line[4:].split("\t")[0].strip()
            target = target[2:] if target.startswith(("a/", "b/")) else target
            if target.startswith(FIRST_PARTY_PATCH_TARGETS):
                yield Finding(path, number, "PG008", f"patches first-party file {target}")
                return


@file_check
def build_dir_include(tree: Tree, path: str) -> Iterator[Finding]:
    """PG009: sources that only compile inside one configured build tree."""
    if not path.endswith(NATIVE_SUFFIXES):
        return
    for number, line in _lines(tree, path):
        include = INCLUDE.match(line)
        if include and ABSOLUTE_OR_BUILD.search(include.group(1)):
            yield Finding(path, number, "PG009", f"includes {include.group(1)}")


@file_check
def identity_literal(tree: Tree, path: str) -> Iterator[Finding]:
    """PG010: identity built by splicing digests instead of named constants."""
    if not (path.endswith(CODE_SUFFIXES) or is_cmake(path)) or path.endswith(IDENTITY_ALLOWED_SUFFIX):
        return
    for number, line in _lines(tree, path):
        if IDENTITY_SPLICE.search(line):
            yield Finding(path, number, "PG010", "digest spliced into an identity string")


@file_check
def generated_committed(tree: Tree, path: str) -> Iterator[Finding]:
    """PG011: generator output in git."""
    posix = PurePosixPath(path)
    if path not in tree.tracked:
        return
    if "build" in posix.parts[:-1]:
        yield Finding(path, 1, "PG011", "committed file under a build/ directory")
    elif fnmatch.fnmatch(posix.name, "*_symbols.hpp"):
        yield Finding(path, 1, "PG011", "committed generated symbol header")


@file_check
def guest_rewrite(tree: Tree, path: str) -> Iterator[Finding]:
    """PG012: read-modify-write of recompiled guest code. Naming those paths
    is fine (build scripts and generators do); rewriting a file in place whose
    path derives from one is not."""
    if path in GUEST_REWRITE_ALLOWED or not GUEST_CODE.search(tree.text(path) or ""):
        return
    if path.endswith(".py"):
        module = tree.python(path)
        if module is None:
            return
        functions = (node for node in ast.walk(module) if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef)))
        for scope in (module, *functions):
            nodes = list(_walk_scope(scope))
            facts = _python_facts(nodes)
            if not facts.rewrites:
                continue
            guest_names = _guest_bound_names(nodes)
            for target, number, expression in facts.write_targets:
                if target in facts.read_targets and _names_guest_code(expression, guest_names):
                    yield Finding(path, number, "PG012", "rewrites guest code in place")
                    return
    elif is_cmake(path):
        commands = tree.cmake(path)
        guest_vars = _guest_bound_variables(commands)
        read: dict[str, int] = {}
        for index, command in enumerate(commands):
            if command.name != "file" or len(command.args) < 2:
                continue
            mode, target = command.args[0], command.args[1]
            if mode == "READ":
                read[target] = index
            elif (
                mode in ("WRITE", "APPEND")
                and target in read
                and (GUEST_CODE.search(target) or _references(target, guest_vars))
                and any(
                    later.name == "string" and later.args[:1] in (("REPLACE",), ("REGEX",))
                    for later in commands[read[target] : index]
                )
            ):
                yield Finding(path, command.line, "PG012", "rewrites guest code in place")
                return


def _guest_bound_names(nodes: list[ast.AST]) -> set[str]:
    """Names in one scope whose value derives from a guest code path, to a
    fixpoint (``funcs = build / "RecompiledFuncs"; for f in funcs.glob(...)``)."""
    bindings: list[tuple[list[str], ast.AST]] = []
    for node in nodes:
        if isinstance(node, ast.Assign):
            bindings.append(([name for target in node.targets for name in _bound(target)], node.value))
        elif isinstance(node, (ast.AnnAssign, ast.AugAssign, ast.NamedExpr)) and node.value is not None:
            bindings.append((_bound(node.target), node.value))
        elif isinstance(node, (ast.For, ast.AsyncFor, ast.comprehension)):
            bindings.append((_bound(node.target), node.iter))
        elif isinstance(node, ast.withitem) and node.optional_vars is not None:
            bindings.append((_bound(node.optional_vars), node.context_expr))
    names: set[str] = set()
    while True:
        grown = {name for targets, value in bindings if _names_guest_code(value, names) for name in targets}
        if grown <= names:
            return names
        names |= grown


def _bound(target: ast.AST) -> list[str]:
    return [node.id for node in ast.walk(target) if isinstance(node, ast.Name)]


def _names_guest_code(expression: ast.AST, guest_names: set[str]) -> bool:
    return any(
        (isinstance(node, ast.Constant) and isinstance(node.value, str) and GUEST_CODE.search(node.value))
        or (isinstance(node, ast.Name) and node.id in guest_names)
        for node in ast.walk(expression)
    )


def _guest_bound_variables(commands: list[Command]) -> set[str]:
    """CMake variables set (set, list, file(GLOB), foreach) from guest code paths."""
    bindings: list[tuple[str, tuple[str, ...]]] = []
    for command in commands:
        args = command.args
        if command.name in ("set", "foreach") and args:
            bindings.append((args[0], args[1:]))
        elif command.name in ("file", "list") and len(args) > 1 and args[0] in ("GLOB", "GLOB_RECURSE", "APPEND"):
            bindings.append((args[1], args[2:]))
    variables: set[str] = set()
    while True:
        grown = {
            name
            for name, values in bindings
            if any(GUEST_CODE.search(value) or _references(value, variables) for value in values)
        }
        if grown <= variables:
            return variables
        variables |= grown


def _references(value: str, variables: set[str]) -> bool:
    """``${name}`` anywhere in the value, or a bare name (``IN LISTS name``)."""
    return value in variables or any(f"${{{name}}}" in value for name in variables)


def patches_layout(tree: Tree, path: str, flagged: set[str]) -> Iterator[Finding]:
    """PG013: catch-all for patches/ contents no specific rule claimed."""
    if not path.startswith("patches/") or path in flagged:
        return
    parts = PurePosixPath(path).parts
    top = "/".join(parts[:2])
    allowed = (
        path == "patches/manifest.json"
        or path == "patches/README.md"
        or (top in tree.dependency_dirs and (path.endswith(".patch") or parts[-1] == "README.md"))
    )
    if not allowed:
        yield Finding(path, 1, "PG013", "patches/ holds only manifest.json, READMEs and registered dependency patches")


def scan(tree: Tree, paths: Iterable[str]) -> list[Finding]:
    """Every finding for the given tree-relative paths, sorted."""
    findings: list[Finding] = []
    scoped = [path for path in paths if tree.has_file(path)]
    for path in scoped:
        for check in FILE_CHECKS:
            findings.extend(check(tree, path))
    flagged = {finding.path for finding in findings}
    for path in scoped:
        findings.extend(patches_layout(tree, path, flagged))
    return sorted(set(findings))


def check_content(root: Path, path: str, text: str) -> list[Finding]:
    """Findings for one root-relative path if its content were ``text``.

    Public entry point for scripts/agent_hooks/pre_edit_guard.py. It applies
    every rule but does not consult the baseline; callers filter. Excluded
    paths (extern/, the guard's own files) yield nothing."""
    tree = Tree(root, {path: text})
    return scan(tree, [path]) if tree.is_scanned(path) else []


# --- Baseline ----------------------------------------------------------------

Entry = tuple[str, str]  # (path, rule)


def parse_baseline(text: str, source: str) -> dict[Entry, int]:
    """Baseline entries mapped to their line numbers; malformed input raises."""
    entries: dict[Entry, int] = {}
    previous: Entry | None = None
    for number, raw in enumerate(text.splitlines(), 1):
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        path, _, rule = line.rpartition(" ")
        if rule not in RULES or not path or any(char in path for char in "*?["):
            raise GuardError(f"{source}:{number}: expected '<path> <PGnnn>' with an enumerated path")
        entry = (path, rule)
        if previous is not None and entry <= previous:
            raise GuardError(f"{source}:{number}: entries must be sorted and unique")
        entries[entry] = number
        previous = entry
    return entries


def render_baseline(findings: Iterable[Finding]) -> str:
    entries = sorted({(finding.path, finding.rule) for finding in findings})
    return BASELINE_HEADER + "\n" + "".join(f"{path} {rule}\n" for path, rule in entries)


# --- Scopes ------------------------------------------------------------------


@dataclasses.dataclass(frozen=True)
class Scope:
    """What one invocation checks: paths to scan, paths whose baseline entries
    must still hold (None: all of them), and the revision the baseline may not
    grow beyond (None: no growth check)."""

    paths: list[str]
    touched: set[str] | None = None
    base: str | None = None


def select_scope(tree: Tree, args: argparse.Namespace) -> Scope:
    if not (args.diff or args.staged):
        return Scope(tree.files)
    if not tree.is_git:
        raise GuardError(f"{tree.root} is not a git work tree; use --tree")
    if args.staged:
        touched = set(_git_lines(tree.root, "diff", "--cached", "--name-only", "--no-renames"))
        base = None
    else:
        result = subprocess.run(
            ["git", "-C", str(tree.root), "merge-base", args.diff, "HEAD"],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
        if result.returncode:
            raise GuardError(f"unknown base {args.diff!r}: {result.stderr.strip()}")
        base = result.stdout.strip()
        touched = set(_git_lines(tree.root, "diff", "--name-only", "--no-renames", base))
        touched |= set(_git_lines(tree.root, "ls-files", "--others", "--exclude-standard"))
    return Scope(sorted(path for path in touched if tree.is_scanned(path)), touched, base)


def committed_baseline(tree: Tree, revision: str) -> dict[Entry, int] | None:
    """The baseline as committed at a revision; None before it existed."""
    result = subprocess.run(
        ["git", "-C", str(tree.root), "show", f"{revision}:{BASELINE}"],
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
        text=True,
    )
    return parse_baseline(result.stdout, f"{revision}:{BASELINE}") if result.returncode == 0 else None


# --- Command line ------------------------------------------------------------


def explain(rule_id: str) -> str:
    if rule_id == "all":
        return "\n".join(f"{rule.id} {rule.name}: {rule.detects}" for rule in RULES.values())
    rule = RULES.get(rule_id.upper())
    if rule is None:
        raise GuardError(f"unknown rule {rule_id}; try --explain all")
    return f"{rule.id} {rule.name}\n  detects: {rule.detects}\n  instead: {rule.instead}"


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    scope = parser.add_mutually_exclusive_group()
    scope.add_argument("--tree", action="store_true", help="scan the whole checkout (default)")
    scope.add_argument("--diff", metavar="BASE", help="scan files changed since merge-base(BASE, HEAD)")
    scope.add_argument("--staged", action="store_true", help="scan staged paths (pre-commit)")
    parser.add_argument(
        "--baseline",
        choices=("check", "write", "ignore"),
        default="check",
        help="check findings against the baseline (default), rewrite it, or report every hit",
    )
    parser.add_argument(
        "--transition-layer", action="store_true", help="accept new baseline entries (PRs labelled transition-layer)"
    )
    parser.add_argument("--explain", metavar="PGnnn", help="describe a rule ('all' lists them) and exit")
    parser.add_argument("--root", type=Path, default=ROOT, help=argparse.SUPPRESS)
    args = parser.parse_args(argv)
    try:
        if args.explain:
            print(explain(args.explain))
            return 0
        return run(args)
    except GuardError as error:
        print(f"pattern-guard: {error}", file=sys.stderr)
        return 2


def run(args: argparse.Namespace) -> int:
    tree = Tree(args.root)
    baseline_path = tree.root / BASELINE
    if args.baseline == "write" and (args.diff or args.staged):
        raise GuardError("--baseline write needs the whole tree (--tree)")
    scope = select_scope(tree, args)
    findings = scan(tree, scope.paths)

    if args.baseline == "write":
        baseline_path.parent.mkdir(parents=True, exist_ok=True)
        baseline_path.write_text(render_baseline(findings), encoding="utf-8")
        print(f"pattern-guard: wrote {len({(f.path, f.rule) for f in findings})} entries to {BASELINE}")
        return 0
    if args.baseline == "ignore":
        for finding in findings:
            print(finding.render())
        print(f"pattern-guard: {len(findings)} finding(s)", file=sys.stderr)
        return 1 if findings else 0

    try:
        baseline = parse_baseline(baseline_path.read_text(encoding="utf-8"), str(BASELINE))
    except FileNotFoundError:
        baseline = {}
    except OSError as error:
        raise GuardError(f"cannot read {BASELINE}: {error}") from error

    new = [finding for finding in findings if (finding.path, finding.rule) not in baseline]
    problems = [finding.render() for finding in new]
    found = {(finding.path, finding.rule) for finding in findings}
    for (path, rule), number in baseline.items():
        if (scope.touched is None or path in scope.touched) and (path, rule) not in found:
            problems.append(
                f"{BASELINE}:{number}: {rule} stale entry, {path} no longer matches\n"
                "    instead: delete the line (the baseline only shrinks)"
            )
    before = committed_baseline(tree, scope.base) if scope.base and not args.transition_layer else None
    if before is not None:
        problems.extend(
            f"{BASELINE}:{number}: {rule} new baseline entry for {path}\n"
            "    instead: fix the finding; only transition-layer PRs may add entries"
            for (path, rule), number in baseline.items()
            if (path, rule) not in before
        )

    for problem in problems:
        print(problem)
    print(
        f"pattern-guard: {len(problems)} problem(s), {len(findings) - len(new)} baselined finding(s) tolerated",
        file=sys.stderr,
    )
    return 1 if problems else 0


if __name__ == "__main__":
    raise SystemExit(main())
